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

STATIC void cbor_dump_buffer(mp_obj_t obj_data, vstr_t *data_vstr);
STATIC mp_obj_t cbor_dumps(mp_obj_t obj_data, vstr_t *data_vstr);
STATIC mp_obj_t cbor_loads(vstr_t *data_vstr);

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
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("Invalid additional information")));
    }

    return val;
}

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
    mp_int_t load_int_len = mp_obj_get_int(cbor_load_int(ai, data_vstr));
    mp_obj_t val = mp_obj_new_bytes((const byte *)data_vstr->buf, load_int_len);
    vstr_cut_head_bytes(data_vstr, load_int_len);
    return val;
}

STATIC mp_obj_t cbor_load_text(const byte ai, vstr_t *data_vstr)
{
    mp_int_t load_int_len = mp_obj_get_int(cbor_load_int(ai, data_vstr));
    mp_obj_t val = mp_obj_new_str(data_vstr->buf, load_int_len);
    vstr_cut_head_bytes(data_vstr, load_int_len);
    return val;
}

STATIC mp_obj_t cbor_load_list(const byte ai, vstr_t *data_vstr)
{
    size_t load_int_len = mp_obj_get_int(cbor_load_int(ai, data_vstr));
    mp_obj_t items = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < load_int_len; i++)
    {
        mp_obj_t item = cbor_loads(data_vstr);
        mp_obj_list_append(items, item);
    }
    return items;
}

STATIC mp_obj_t cbor_load_dict(const byte ai, vstr_t *data_vstr)
{
    size_t load_int_len = mp_obj_get_int(cbor_load_int(ai, data_vstr));
    mp_obj_t dict = mp_obj_new_dict(0);
    for (size_t i = 0; i < load_int_len; i++)
    {
        mp_obj_t key = cbor_loads(data_vstr);
        mp_obj_t value = cbor_loads(data_vstr);
        mp_obj_dict_store(dict, key, value);
    }
    return dict;
}

STATIC mp_obj_t cbor_loads(vstr_t *data_vstr)
{
    mp_obj_t val = mp_const_none;

    byte fb = data_vstr->buf[0];
    vstr_cut_head_bytes(data_vstr, 1);

    switch ((fb >> 5))
    {
    case 0:
    {
        val = cbor_load_int((fb & 0x1f), data_vstr);
        break;
    }
    case 1:
    {
        val = cbor_load_uint((fb & 0x1f), data_vstr);
        break;
    }
    case 2:
    {
        val = cbor_load_bytes((fb & 0x1f), data_vstr);
        break;
    }
    case 3:
    {
        val = cbor_load_text((fb & 0x1f), data_vstr);
        break;
    }
    case 4:
    {
        val = cbor_load_list((fb & 0x1f), data_vstr);
        break;
    }
    case 5:
    {
        val = cbor_load_dict((fb & 0x1f), data_vstr);
        break;
    }
    case 7:
    {
        val = cbor_load_bool((fb & 0x1f), data_vstr);
        break;
    }
    default:
    {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported major type: %d"), (fb >> 5)));
    }
    }

    return val;
}

STATIC mp_obj_t cbor_decode(mp_obj_t obj_data)
{
    vstr_t data_vstr;
    vstr_init(&data_vstr, 16);
    cbor_dump_buffer(obj_data, &data_vstr);
    mp_obj_t val = cbor_loads(&data_vstr);
    vstr_clear(&data_vstr);
    return val;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_decode_obj, cbor_decode);

#if defined(MICROPY_PY_UCBOR_CANONICAL)
STATIC mp_obj_t cbor_sort_key(mp_obj_t entry)
{
    mp_obj_tuple_t *entry_tuple = MP_OBJ_TO_PTR(entry);
    mp_obj_t key = entry_tuple->items[0];
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(key, &bufinfo, MP_BUFFER_READ);
    mp_obj_t sort_tuple[3] = {mp_obj_new_bytes(bufinfo.buf, 1), mp_obj_new_int(bufinfo.len), key};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(sort_tuple), sort_tuple);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_sort_key_obj, cbor_sort_key);
#endif

STATIC void cbor_dump_int(mp_obj_t obj_data, mp_int_t mt, vstr_t *data_vstr)
{
    mp_int_t data = mp_obj_get_int(obj_data);
    if (data < 0)
    {
        mt = 1;
        data = -1 - data;
    }

    mt = mt << 5;
    if (data <= 23)
    {
        vstr_add_byte(data_vstr, (byte)(mt | data));
    }
    else if (data <= 0xff)
    {
        vstr_add_byte(data_vstr, (byte)(mt | 24));
        vstr_add_byte(data_vstr, (byte)(data));
    }
    else if (data <= 0xffff)
    {
        vstr_add_byte(data_vstr, (byte)(mt | 25));
        vstr_add_byte(data_vstr, (byte)((data >> 8) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 0) & 0xff));
    }
#if UINT32_MAX >= 0xffffffff
    else if (data <= 0xffffffff)
    {
        vstr_add_byte(data_vstr, (byte)(mt | 26));
        vstr_add_byte(data_vstr, (byte)((data >> 24) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 16) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 8) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 0) & 0xff));
    }
#if UINT32_MAX > 0xffffffff
    else
    {
        vstr_add_byte(data_vstr, (byte)(mt | 27));
        vstr_add_byte(data_vstr, (byte)((data >> 56) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 48) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 40) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 32) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 24) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 16) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 8) & 0xff));
        vstr_add_byte(data_vstr, (byte)((data >> 0) & 0xff));
    }
#endif
#endif
}

STATIC void cbor_dump_buffer_with_optional_major_type(mp_obj_t obj_data, vstr_t *data_vstr, mp_int_t mt)
{
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(obj_data, &bufinfo, MP_BUFFER_READ);
    if (mt != -1)
    {
        cbor_dump_int(mp_obj_new_int(bufinfo.len), mt, data_vstr);
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
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(obj_data, &len, &items);
    cbor_dump_int(mp_obj_new_int(len), 4, data_vstr);

    for (size_t i = 0; i < len; i++)
    {
        cbor_dumps(items[i], data_vstr);
    }
}

STATIC void cbor_dump_dict(mp_obj_t obj_data, vstr_t *data_vstr)
{
    mp_map_t *map = mp_obj_dict_get_map(obj_data);
    cbor_dump_int(mp_obj_new_int(map->used), 5, data_vstr);

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

    size_t array_len;
    mp_obj_t *array_items;
    mp_obj_get_array(items, &array_len, &array_items);
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

STATIC mp_obj_t cbor_dumps(mp_obj_t obj_data, vstr_t *data_vstr)
{
    const mp_obj_type_t *obj_data_type = mp_obj_get_type(obj_data);
    bool new_data_vstr = (data_vstr == NULL);
    if (new_data_vstr && (data_vstr = vstr_new(16)) == NULL)
    {
        mp_raise_msg_varg(&mp_type_MemoryError, MP_ERROR_TEXT("memory allocation failed, allocating %u bytes"), 16);
    }

    if (obj_data_type == &mp_type_int)
    {
        cbor_dump_int(obj_data, 0, data_vstr);
    }
    else if (obj_data_type == &mp_type_bool)
    {
        cbor_dump_bool(obj_data, data_vstr);
    }
    else if (obj_data_type == &mp_type_str)
    {
        cbor_dump_text(obj_data, data_vstr);
    }
    else if (obj_data_type == &mp_type_bytes || obj_data_type == &mp_type_bytearray || obj_data_type == &mp_type_memoryview)
    {
        cbor_dump_bytes(obj_data, data_vstr);
    }
    else if (obj_data_type == &mp_type_list || obj_data_type == &mp_type_tuple)
    {
        cbor_dump_list(obj_data, data_vstr);
    }
    else if (obj_data_type == &mp_type_dict)
    {
        cbor_dump_dict(obj_data, data_vstr);
    }
    else
    {
        if (new_data_vstr)
        {
            vstr_free(data_vstr);
        }
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported value: %s"), mp_obj_get_type_str(obj_data)));
    }

    mp_obj_t val = mp_obj_new_bytes((byte *)data_vstr->buf, data_vstr->len);
    if (new_data_vstr)
    {
        vstr_free(data_vstr);
    }
    return val;
}

STATIC mp_obj_t cbor_encode(mp_obj_t obj_data)
{
    vstr_t data_vstr;
    vstr_init(&data_vstr, 16);
    mp_obj_t val = cbor_dumps(obj_data, &data_vstr);
    vstr_clear(&data_vstr);
    return val;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_encode_obj, cbor_encode);

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