/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Damiano Mazzella
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#if defined(MICROPY_PY_UCBOR)

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objint.h"

#define VSTR_INIT(vstr, alloc) \
    vstr_t vstr;               \
    vstr_init(&vstr, (alloc));

#define GET_ARRAY(array_obj) \
    size_t array_len;        \
    mp_obj_t *array_items;   \
    mp_obj_get_array(array_obj, &array_len, &array_items);

typedef mp_obj_t (*mp_cbor_load_function_t)(const byte _ai, vstr_t *_data_vstr);
typedef struct _mp_cbor_load_func_t
{
    const byte _type;
    mp_cbor_load_function_t _func;
} mp_cbor_load_func_t;

typedef void (*mp_cbor_dump_function_t)(mp_obj_t _obj_data, vstr_t *_data_vstr);
typedef struct _mp_cbor_dump_func_t
{
    const mp_obj_type_t *_type;
    mp_cbor_dump_function_t _func;
} mp_cbor_dump_func_t;

STATIC void cbor_dump_buffer(mp_obj_t obj_data, vstr_t *data_vstr);
STATIC mp_obj_t cbor_dumps(mp_obj_t obj_data, vstr_t *data_vstr);
STATIC mp_obj_t cbor_loads(vstr_t *data_vstr);

/*
 ██████     █████████     ███        █████      ██████     █████████
 ███   ███  ███        ███   ███   ███    ███   ███   ███  ███      
 ███    ███ ███       ███        ███        ███ ███    ███ ███      
 ███    ███ ███████   ███        ███        ███ ███    ███ ███████  
 ███    ███ ███       ███        ███        ███ ███    ███ ███      
 ███   ███  ███        ███   ███   ███     ███  ███   ███  ███      
 ██████     █████████    █████       █████      ██████     █████████
*/

STATIC mp_obj_t cbor_load_int(const byte ai, vstr_t *data_vstr)
{
    mp_obj_t val = mp_const_none;

    if (ai < 24)
    {
        val = mp_obj_new_int(ai);
    }
    else if (ai >= 24 && ai <= 27)
    {
        uint8_t n_bytes = (1 << (ai - 24));
        val = mp_obj_int_from_bytes_impl(true, n_bytes, (const byte *)data_vstr->buf);
        vstr_cut_head_bytes(data_vstr, n_bytes);
    }

    if (!mp_obj_is_int(val))
    {
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid additional information"));
    }

    return val;
}

#define LOAD_INT(ai, data_vstr) \
    size_t loaded_int = mp_obj_get_int(cbor_load_int(ai, data_vstr));

STATIC mp_obj_t cbor_load_uint(const byte ai, vstr_t *data_vstr)
{
    return mp_binary_op(MP_BINARY_OP_SUBTRACT, mp_obj_new_int(-1), cbor_load_int(ai, data_vstr));
}

STATIC mp_obj_t cbor_load_bool(const byte ai, vstr_t *data_vstr)
{
    return mp_obj_new_bool(ai == 21);
}

STATIC mp_obj_t cbor_load_bytes(const byte ai, vstr_t *data_vstr)
{
    LOAD_INT(ai, data_vstr);
    mp_obj_t val = mp_obj_new_bytes((const byte *)data_vstr->buf, loaded_int);
    vstr_cut_head_bytes(data_vstr, loaded_int);
    return val;
}

STATIC mp_obj_t cbor_load_text(const byte ai, vstr_t *data_vstr)
{
    LOAD_INT(ai, data_vstr);
    mp_obj_t val = mp_obj_new_str(data_vstr->buf, loaded_int);
    vstr_cut_head_bytes(data_vstr, loaded_int);
    return val;
}

STATIC mp_obj_t cbor_load_list(const byte ai, vstr_t *data_vstr)
{
    LOAD_INT(ai, data_vstr);
    mp_obj_t items = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < loaded_int; i++)
    {
        mp_obj_t item = cbor_loads(data_vstr);
        mp_obj_list_append(items, item);
    }
    return items;
}

STATIC mp_obj_t cbor_load_dict(const byte ai, vstr_t *data_vstr)
{
    LOAD_INT(ai, data_vstr);
    mp_obj_t dict = mp_obj_new_dict(0);
    for (size_t i = 0; i < loaded_int; i++)
    {
        mp_obj_t key = cbor_loads(data_vstr);
        mp_obj_t value = cbor_loads(data_vstr);
        mp_obj_dict_store(dict, key, value);
    }
    return dict;
}

STATIC mp_obj_t cbor_unsupported_major_type(const byte ai, vstr_t *data_vstr)
{
    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported major type: %d"), (ai >> 5)));
}

STATIC mp_cbor_load_func_t load_functions_map[] = {
    {0, cbor_load_int},
    {1, cbor_load_uint},
    {2, cbor_load_bytes},
    {3, cbor_load_text},
    {4, cbor_load_list},
    {5, cbor_load_dict},
    {6, cbor_unsupported_major_type},
    {7, cbor_load_bool},
};

STATIC mp_obj_t cbor_loads(vstr_t *data_vstr)
{
    byte fb = data_vstr->buf[0];
    vstr_cut_head_bytes(data_vstr, 1);
    byte mt = (fb >> 5);
    byte ai = (fb & 0x1f);
    if (mt > 7)
    {
        cbor_unsupported_major_type(ai, data_vstr);
    }
    return load_functions_map[mt]._func(ai, data_vstr);
}

STATIC mp_obj_t cbor_decode(mp_obj_t obj_data)
{
    VSTR_INIT(data_vstr, 16);
    cbor_dump_buffer(obj_data, &data_vstr);
    mp_obj_t val = cbor_loads(&data_vstr);
    vstr_clear(&data_vstr);
    return val;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_decode_obj, cbor_decode);

/*
 █████████ ████     ███     ███        █████      ██████     █████████
 ███       ██ ███   ███  ███   ███   ███    ███   ███   ███  ███      
 ███       ███ ███  ███ ███        ███        ███ ███    ███ ███      
 ███████   ███  ███ ███ ███        ███        ███ ███    ███ ███████  
 ███       ███   ██ ███ ███        ███        ███ ███    ███ ███      
 ███       ███    ██ ██  ███   ███   ███     ███  ███   ███  ███      
 █████████ ███      ███    █████       █████      ██████     █████████
*/

#if defined(MICROPY_PY_UCBOR_CANONICAL)
STATIC mp_obj_t cbor_sort_key(mp_obj_t entry)
{
    mp_obj_tuple_t *entry_tuple = MP_OBJ_TO_PTR(entry);
    mp_obj_t key = entry_tuple->items[0];
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(key, &bufinfo, MP_BUFFER_READ);
    mp_obj_t sort_tuple[3] = {mp_obj_new_bytearray_by_ref(1, (byte *)bufinfo.buf), mp_obj_new_int(bufinfo.len), key};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(sort_tuple), sort_tuple);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_sort_key_obj, cbor_sort_key);
#endif

STATIC void cbor_dump_int_with_major_type(mp_obj_t obj_data, vstr_t *data_vstr, mp_int_t mt)
{
    mp_int_t data = mp_obj_get_int(obj_data);
    byte ai = 0;

    if (data < 0)
    {
        mt = 1;
        data = -1 - data;
    }

    mt = mt << 5;
    if (data <= 23)
    {
        ai = (byte)(mt | data);
    }
    else if (data <= 0xff)
    {
        ai = (byte)(mt | 24);
    }
    else if (data <= 0xffff)
    {
        ai = (byte)(mt | 25);
    }
    else if (data <= 0xffffffff)
    {
        ai = (byte)(mt | 26);
    }
#if UINT32_MAX > 0xffffffff
    else
    {
        ai = (byte)(mt | 27);
    }
#endif

    vstr_add_byte(data_vstr, ai);
    byte n_bytes = (1 << (ai - 24));
    while (n_bytes)
    {
        vstr_add_byte(data_vstr, (byte)((data >> ((--n_bytes) * 8)) & 0xff));
    }
}

STATIC void cbor_dump_int(mp_obj_t obj_data, vstr_t *data_vstr)
{
    cbor_dump_int_with_major_type(obj_data, data_vstr, 0);
}

STATIC void cbor_dump_buffer_with_optional_major_type(mp_obj_t obj_data, vstr_t *data_vstr, mp_int_t mt)
{
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(obj_data, &bufinfo, MP_BUFFER_READ);
    if (mt != -1)
    {
        cbor_dump_int_with_major_type(mp_obj_new_int(bufinfo.len), data_vstr, mt);
    }
    vstr_add_strn(data_vstr, (const char *)bufinfo.buf, bufinfo.len);
}

STATIC void cbor_dump_buffer(mp_obj_t obj_data, vstr_t *data_vstr)
{
    cbor_dump_buffer_with_optional_major_type(obj_data, data_vstr, -1);
}

STATIC void cbor_dump_bool(mp_obj_t obj_data, vstr_t *data_vstr)
{
    vstr_add_byte(data_vstr, (byte)(mp_obj_is_true(obj_data) ? 0xf5 : 0xf4));
}

STATIC void cbor_dump_bytes(mp_obj_t obj_data, vstr_t *data_vstr)
{
    cbor_dump_buffer_with_optional_major_type(obj_data, data_vstr, 2);
}

STATIC void cbor_dump_text(mp_obj_t obj_data, vstr_t *data_vstr)
{
    cbor_dump_buffer_with_optional_major_type(obj_data, data_vstr, 3);
}

STATIC void cbor_dump_list(mp_obj_t obj_data, vstr_t *data_vstr)
{
    GET_ARRAY(obj_data);
    cbor_dump_int_with_major_type(mp_obj_new_int(array_len), data_vstr, 4);

    for (size_t i = 0; i < array_len; i++)
    {
        cbor_dumps(array_items[i], data_vstr);
    }
}

STATIC void cbor_dump_dict(mp_obj_t obj_data, vstr_t *data_vstr)
{
    mp_map_t *map = mp_obj_dict_get_map(obj_data);
    cbor_dump_int_with_major_type(mp_obj_new_int(map->used), data_vstr, 5);

#if defined(MICROPY_PY_UCBOR_CANONICAL)
    mp_obj_t items = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < map->alloc; i++)
    {
        if (mp_map_slot_is_filled(map, i))
        {
            mp_obj_t items_items[2] = {cbor_dumps(map->table[i].key, NULL), cbor_dumps(map->table[i].value, NULL)};
            mp_obj_list_append(items, mp_obj_new_tuple(MP_ARRAY_SIZE(items_items), items_items));
        }
    }
    mp_obj_t items_kwargs = mp_obj_new_dict(0);
    mp_obj_dict_store(items_kwargs, MP_ROM_QSTR(MP_QSTR_key), MP_OBJ_FROM_PTR(&cbor_sort_key_obj));
    mp_obj_list_sort(1, &items, mp_obj_dict_get_map(items_kwargs));

    GET_ARRAY(items);
    for (size_t i = 0; i < array_len; i++)
    {
        mp_obj_tuple_t *array_items_tuple = MP_OBJ_TO_PTR(array_items[i]);
        cbor_dump_buffer(array_items_tuple->items[0], data_vstr);
        cbor_dump_buffer(array_items_tuple->items[1], data_vstr);
    }
#else
    for (size_t i = 0; i < map->alloc; i++)
    {
        if (mp_map_slot_is_filled(map, i))
        {
            cbor_dumps(map->table[i].key, data_vstr);
            cbor_dumps(map->table[i].value, data_vstr);
        }
    }
#endif
}

STATIC mp_cbor_dump_func_t dump_functions_map[] = {
    {&mp_type_int, cbor_dump_int},
    {&mp_type_bool, cbor_dump_bool},
    {&mp_type_str, cbor_dump_text},
    {&mp_type_bytes, cbor_dump_bytes},
    {&mp_type_bytearray, cbor_dump_bytes},
    {&mp_type_memoryview, cbor_dump_bytes},
    {&mp_type_list, cbor_dump_list},
    {&mp_type_tuple, cbor_dump_list},
    {&mp_type_dict, cbor_dump_dict},
};

STATIC mp_obj_t cbor_dumps(mp_obj_t obj_data, vstr_t *data_vstr)
{
    const mp_obj_type_t *obj_data_type = mp_obj_get_type(obj_data);
    bool need_temp_data_vstr = (data_vstr == NULL);

    for (size_t i = 0; i < MP_ARRAY_SIZE(dump_functions_map); i++)
    {
        mp_cbor_dump_func_t current_dump_func = dump_functions_map[i];
        if (current_dump_func._type == obj_data_type)
        {
            if (need_temp_data_vstr)
            {
                VSTR_INIT(temp_data_vstr, 16);
                data_vstr = &temp_data_vstr;
            }
            current_dump_func._func(obj_data, data_vstr);
            mp_obj_t val = mp_obj_new_bytes((byte *)data_vstr->buf, data_vstr->len);
            if (need_temp_data_vstr)
            {
                vstr_clear(data_vstr);
            }
            return val;
        }
    }

    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported value: %s"), mp_obj_get_type_str(obj_data)));
}

STATIC mp_obj_t cbor_encode(mp_obj_t obj_data)
{
    return cbor_dumps(obj_data, NULL);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_encode_obj, cbor_encode);

/*
       ██        ████████   ███
      ██ ██      ███    ███ ███
     ██  ███     ███    ███ ███
    ███   ███    ████████   ███
   ███████ ███   ███        ███
  ███       ███  ███        ███
 ███         ███ ███        ███
*/

STATIC const mp_rom_map_elem_t mp_module_ucbor_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__cbor)},
    {MP_ROM_QSTR(MP_QSTR_decode), MP_ROM_PTR(&cbor_decode_obj)},
    {MP_ROM_QSTR(MP_QSTR_encode), MP_ROM_PTR(&cbor_encode_obj)},
};

STATIC MP_DEFINE_CONST_DICT(mp_module_ucbor_globals, mp_module_ucbor_globals_table);

const mp_obj_module_t mp_module_ucbor = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_ucbor_globals,
};

// Register the module to make it available in Python
MP_REGISTER_MODULE(MP_QSTR_cbor, mp_module_ucbor, MICROPY_PY_UCBOR);

#endif // MICROPY_PY_UCBOR