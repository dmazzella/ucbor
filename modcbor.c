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

STATIC mp_obj_t cbor_loads(vstr_t *data_vstr);

STATIC mp_obj_t cbor_load_int(const byte ai, vstr_t *data_vstr)
{
    mp_obj_t val = mp_const_none;
    mp_obj_t data = mp_const_none;

    if (ai < 24)
    {
        val = mp_obj_new_int(ai);
    }
    else if (ai == 24)
    {
        val = mp_obj_int_from_bytes_impl(true, 1, (const byte *)data_vstr->buf);
        vstr_cut_head_bytes(data_vstr, 1);
    }
    else if (ai == 25)
    {
        val = mp_obj_int_from_bytes_impl(true, 2, (const byte *)data_vstr->buf);
        vstr_cut_head_bytes(data_vstr, 2);
    }
    else if (ai == 26)
    {
        val = mp_obj_int_from_bytes_impl(true, 4, (const byte *)data_vstr->buf);
        vstr_cut_head_bytes(data_vstr, 4);
    }
    else if (ai == 27)
    {
        val = mp_obj_int_from_bytes_impl(true, 8, (const byte *)data_vstr->buf);
        vstr_cut_head_bytes(data_vstr, 8);
    }

    if (!mp_obj_is_int(val))
    {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, MP_ERROR_TEXT("Invalid additional information")));
    }

    data = mp_obj_new_bytes((const byte *)data_vstr->buf, data_vstr->len);
    vstr_cut_head_bytes(data_vstr, mp_obj_get_int(val));

    mp_obj_t result_tuple[2] = {val, data};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(result_tuple), result_tuple);
}

STATIC mp_obj_t cbor_load_bool(const byte ai, vstr_t *data_vstr)
{
    mp_obj_t val = mp_obj_new_bool(ai == 21);
    mp_obj_t data = mp_obj_new_bytes((const byte *)data_vstr->buf, data_vstr->len);
    mp_obj_t result_tuple[2] = {val, data};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(result_tuple), result_tuple);
}

STATIC mp_obj_t cbor_load_bytes(const byte ai, vstr_t *data_vstr)
{
    mp_obj_tuple_t *load_int_tuple = MP_OBJ_TO_PTR(cbor_load_int(ai, data_vstr));
    mp_buffer_info_t bufinfo_data;
    mp_get_buffer_raise(load_int_tuple->items[1], &bufinfo_data, MP_BUFFER_READ);
    mp_obj_t val = mp_obj_new_bytes((const byte *)bufinfo_data.buf, mp_obj_get_int(load_int_tuple->items[0]));
    mp_obj_t data = mp_obj_new_bytes((const byte *)data_vstr->buf, data_vstr->len);
    mp_obj_t result_tuple[2] = {val, data};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(result_tuple), result_tuple);
}

STATIC mp_obj_t cbor_load_text(const byte ai, vstr_t *data_vstr)
{
    mp_obj_tuple_t *load_int_tuple = MP_OBJ_TO_PTR(cbor_load_int(ai, data_vstr));
    mp_buffer_info_t bufinfo_data;
    mp_get_buffer_raise(load_int_tuple->items[1], &bufinfo_data, MP_BUFFER_READ);
    mp_obj_t val = mp_obj_new_str((const char *)bufinfo_data.buf, mp_obj_get_int(load_int_tuple->items[0]));
    mp_obj_t data = mp_obj_new_bytes((const byte *)data_vstr->buf, data_vstr->len);
    mp_obj_t result_tuple[2] = {val, data};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(result_tuple), result_tuple);
}

STATIC mp_obj_t cbor_load_list(const byte ai, vstr_t *data_vstr)
{
    mp_obj_tuple_t *load_int_tuple = MP_OBJ_TO_PTR(cbor_load_int(ai, data_vstr));
    size_t result_int_len = mp_obj_get_int(load_int_tuple->items[0]);
    mp_obj_t data = load_int_tuple->items[1];

    mp_obj_t items = mp_obj_new_list(0, NULL);
    for (size_t i = 0; i < result_int_len; i++)
    {
        mp_buffer_info_t bufinfo_data;
        mp_get_buffer_raise(data, &bufinfo_data, MP_BUFFER_READ);

        vstr_t data_vstr;
        vstr_init(&data_vstr, bufinfo_data.len);
        vstr_add_strn(&data_vstr, (const char *)bufinfo_data.buf, bufinfo_data.len);

        mp_obj_tuple_t *result_tuple = MP_OBJ_TO_PTR(cbor_loads(&data_vstr));
        mp_obj_list_append(items, result_tuple->items[0]);
        data = result_tuple->items[1];

        vstr_clear(&data_vstr);
    }
    mp_obj_t result_tuple[2] = {items, data};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(result_tuple), result_tuple);
}

STATIC mp_obj_t cbor_load_dict(const byte ai, vstr_t *data_vstr)
{
    mp_obj_tuple_t *load_int_tuple = MP_OBJ_TO_PTR(cbor_load_int(ai, data_vstr));
    size_t result_int_len = mp_obj_get_int(load_int_tuple->items[0]);
    mp_obj_t data = load_int_tuple->items[1];

    mp_obj_t dict = mp_obj_new_dict(0);
    for (size_t i = 0; i < result_int_len; i++)
    {
        mp_buffer_info_t bufinfo_key;
        mp_get_buffer_raise(data, &bufinfo_key, MP_BUFFER_READ);

        vstr_t vstr_key;
        vstr_init(&vstr_key, bufinfo_key.len);
        vstr_add_strn(&vstr_key, (const char *)bufinfo_key.buf, bufinfo_key.len);
        mp_obj_tuple_t *result_key_tuple = MP_OBJ_TO_PTR(cbor_loads(&vstr_key));
        mp_obj_t value = result_key_tuple->items[1];

        mp_buffer_info_t bufinfo_value;
        mp_get_buffer_raise(value, &bufinfo_value, MP_BUFFER_READ);

        vstr_t vstr_value;
        vstr_init(&vstr_value, bufinfo_value.len);
        vstr_add_strn(&vstr_value, (const char *)bufinfo_value.buf, bufinfo_value.len);
        mp_obj_tuple_t *result_value_tuple = MP_OBJ_TO_PTR(cbor_loads(&vstr_value));

        mp_obj_dict_store(dict, result_key_tuple->items[0], result_value_tuple->items[0]);

        data = result_value_tuple->items[1];

        vstr_clear(&vstr_key);
        vstr_clear(&vstr_value);
    }
    mp_obj_t result_tuple[2] = {dict, data};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(result_tuple), result_tuple);
}

STATIC mp_obj_t cbor_loads(vstr_t *data_vstr)
{
    mp_obj_t val = mp_const_none;
    mp_obj_t data = mp_const_none;

    byte fb = data_vstr->buf[0];
    vstr_cut_head_bytes(data_vstr, 1);

    switch ((fb >> 5))
    {
    case 0:
    {
        mp_obj_tuple_t *load_int_tuple = MP_OBJ_TO_PTR(cbor_load_int((fb & 0x1f), data_vstr));
        val = load_int_tuple->items[0];
        data = load_int_tuple->items[1];
        break;
    }
    case 1:
    {
        mp_obj_tuple_t *load_nint_tuple = MP_OBJ_TO_PTR(cbor_load_int((fb & 0x1f), data_vstr));
        val = mp_binary_op(MP_BINARY_OP_SUBTRACT, mp_obj_new_int(-1), load_nint_tuple->items[0]);
        data = load_nint_tuple->items[1];
        break;
    }
    case 2:
    {
        mp_obj_tuple_t *load_bytes_tuple = MP_OBJ_TO_PTR(cbor_load_bytes((fb & 0x1f), data_vstr));
        val = load_bytes_tuple->items[0];
        data = load_bytes_tuple->items[1];
        break;
    }
    case 3:
    {
        mp_obj_tuple_t *load_text_tuple = MP_OBJ_TO_PTR(cbor_load_text((fb & 0x1f), data_vstr));
        val = load_text_tuple->items[0];
        data = load_text_tuple->items[1];
        break;
    }
    case 4:
    {
        mp_obj_tuple_t *load_list_tuple = MP_OBJ_TO_PTR(cbor_load_list((fb & 0x1f), data_vstr));
        val = load_list_tuple->items[0];
        data = load_list_tuple->items[1];
        break;
    }
    case 5:
    {
        mp_obj_tuple_t *load_dict_tuple = MP_OBJ_TO_PTR(cbor_load_dict((fb & 0x1f), data_vstr));
        val = load_dict_tuple->items[0];
        data = load_dict_tuple->items[1];
        break;
    }
    case 7:
    {
        mp_obj_tuple_t *load_bool_tuple = MP_OBJ_TO_PTR(cbor_load_bool((fb & 0x1f), data_vstr));
        val = load_bool_tuple->items[0];
        data = load_bool_tuple->items[1];
        break;
    }
    default:
    {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported major type: %d"), (fb >> 5)));
    }
    }

    mp_obj_t result_tuple[2] = {val, data};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(result_tuple), result_tuple);
}

STATIC mp_obj_t cbor_decode(mp_obj_t obj_data)
{
    mp_buffer_info_t bufinfo_data;
    mp_get_buffer_raise(obj_data, &bufinfo_data, MP_BUFFER_READ);

    vstr_t data_vstr;
    vstr_init(&data_vstr, bufinfo_data.len);
    vstr_add_strn(&data_vstr, (const char *)bufinfo_data.buf, bufinfo_data.len);
    mp_obj_tuple_t *loads_tuple = MP_OBJ_TO_PTR(cbor_loads(&data_vstr));
    vstr_clear(&data_vstr);
    return loads_tuple->items[0];
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_decode_obj, cbor_decode);

#if defined(MICROPY_PY_UCBOR_CANONICAL)
STATIC mp_obj_t cbor_sort_key(mp_obj_t entry)
{
    mp_obj_tuple_t *entry_tuple = MP_OBJ_TO_PTR(entry);
    mp_obj_t key = entry_tuple->items[0];
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(key, &bufinfo, MP_BUFFER_READ);
    mp_obj_t sort_tuple[3] = {
        mp_obj_new_bytes(bufinfo.buf, 1),
        mp_obj_new_int(bufinfo.len),
        key};
    return mp_obj_new_tuple(MP_ARRAY_SIZE(sort_tuple), sort_tuple);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_sort_key_obj, cbor_sort_key);
#endif

STATIC mp_obj_t cbor_encode(mp_obj_t obj_data);

STATIC vstr_t *cbor_dump_int(mp_obj_t obj_data, mp_int_t mt)
{
    mp_int_t data = mp_obj_get_int(obj_data);
    if (data < 0)
    {
        mt = 1;
        data = -1 - data;
    }

    mt = mt << 5;
    vstr_t *vstr = NULL;
    if ((vstr = vstr_new(16)) != NULL)
    {
        if (data <= 23)
        {
            vstr_add_byte(vstr, (byte)(mt | data));
        }
        else if (data <= 0xff)
        {
            vstr_add_byte(vstr, (byte)(mt | 24));
            vstr_add_byte(vstr, (byte)(data));
        }
        else if (data <= 0xffff)
        {
            vstr_add_byte(vstr, (byte)(mt | 25));
            vstr_add_byte(vstr, (byte)((data >> 8) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 0) & 0xff));
        }
#if UINT32_MAX >= 0xffffffff
        else if (data <= 0xffffffff)
        {
            vstr_add_byte(vstr, (byte)(mt | 26));
            vstr_add_byte(vstr, (byte)((data >> 24) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 16) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 8) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 0) & 0xff));
        }
#if UINT32_MAX > 0xffffffff
        else
        {
            vstr_add_byte(vstr, (byte)(mt | 27));
            vstr_add_byte(vstr, (byte)((data >> 56) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 48) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 40) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 32) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 24) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 16) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 8) & 0xff));
            vstr_add_byte(vstr, (byte)((data >> 0) & 0xff));
        }
#endif
#endif
    }
    return vstr;
}

STATIC vstr_t *cbor_dump_bool(mp_obj_t obj_data)
{
    vstr_t *vstr = NULL;
    if ((vstr = vstr_new(16)) != NULL)
    {
        vstr_add_byte(vstr, (byte)(mp_obj_get_int(obj_data) ? 0xf5 : 0xf4));
    }
    return vstr;
}

STATIC vstr_t *cbor_dump_text(mp_obj_t obj_data)
{
    vstr_t *vstr = NULL;
    if ((vstr = vstr_new(16)) != NULL)
    {
        size_t len = 0;
        const char *text = mp_obj_str_get_data(obj_data, &len);

        vstr_t *vstr_type = cbor_dump_int(mp_obj_new_int(len), 3);
        if (vstr_type != NULL)
        {
            vstr_add_strn(vstr, vstr_type->buf, vstr_type->len);
            vstr_free(vstr_type);
        }

        vstr_add_strn(vstr, text, len);
    }
    return vstr;
}

STATIC vstr_t *cbor_dump_bytes(mp_obj_t obj_data)
{
    vstr_t *vstr = NULL;
    if ((vstr = vstr_new(16)) != NULL)
    {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(obj_data, &bufinfo, MP_BUFFER_READ);

        vstr_t *vstr_type = cbor_dump_int(mp_obj_new_int(bufinfo.len), 2);
        if (vstr_type != NULL)
        {
            vstr_add_strn(vstr, vstr_type->buf, vstr_type->len);
            vstr_free(vstr_type);
        }

        vstr_add_strn(vstr, (const char *)bufinfo.buf, bufinfo.len);
    }
    return vstr;
}

STATIC vstr_t *cbor_dump_list(mp_obj_t obj_data)
{
    vstr_t *vstr = NULL;
    if ((vstr = vstr_new(16)) != NULL)
    {
        size_t len;
        mp_obj_t *items;
        mp_obj_get_array(obj_data, &len, &items);
        vstr_t *vstr_type = cbor_dump_int(mp_obj_new_int(len), 4);
        if (vstr_type != NULL)
        {
            vstr_add_strn(vstr, vstr_type->buf, vstr_type->len);
            vstr_free(vstr_type);
        }

        for (size_t i = 0; i < len; i++)
        {
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(cbor_encode(items[i]), &bufinfo, MP_BUFFER_READ);
            vstr_add_strn(vstr, (const char *)bufinfo.buf, bufinfo.len);
        }
    }
    return vstr;
}

STATIC vstr_t *cbor_dump_dict(mp_obj_t obj_data)
{
    vstr_t *vstr = NULL;
    if ((vstr = vstr_new(16)) != NULL)
    {
        mp_map_t *map = mp_obj_dict_get_map(obj_data);
        vstr_t *vstr_type = cbor_dump_int(mp_obj_new_int(map->used), 5);
        if (vstr_type != NULL)
        {
            vstr_add_strn(vstr, vstr_type->buf, vstr_type->len);
            vstr_free(vstr_type);
        }

#if defined(MICROPY_PY_UCBOR_CANONICAL)
        mp_obj_t items = mp_obj_new_list(0, NULL);
        for (size_t i = 0; i < map->used; i++)
        {
            if (mp_map_slot_is_filled(map, i))
            {
                mp_buffer_info_t bufinfo_key;
                mp_get_buffer_raise(cbor_encode(map->table[i].key), &bufinfo_key, MP_BUFFER_READ);

                mp_buffer_info_t bufinfo_value;
                mp_get_buffer_raise(cbor_encode(map->table[i].value), &bufinfo_value, MP_BUFFER_READ);

                mp_obj_t items_items[2] = {
                    mp_obj_new_bytes(bufinfo_key.buf, bufinfo_key.len),
                    mp_obj_new_bytes(bufinfo_value.buf, bufinfo_value.len)};
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

            mp_buffer_info_t bufinfo_key;
            mp_get_buffer_raise(array_items_tuple->items[0], &bufinfo_key, MP_BUFFER_READ);
            vstr_add_strn(vstr, (const char *)bufinfo_key.buf, bufinfo_key.len);

            mp_buffer_info_t bufinfo_value;
            mp_get_buffer_raise(array_items_tuple->items[1], &bufinfo_value, MP_BUFFER_READ);
            vstr_add_strn(vstr, (const char *)bufinfo_value.buf, bufinfo_value.len);
        }
#else
        for (size_t i = 0; i < map->used; i++)
        {
            if (mp_map_slot_is_filled(map, i))
            {
                mp_buffer_info_t bufinfo_key;
                mp_get_buffer_raise(cbor_encode(map->table[i].key), &bufinfo_key, MP_BUFFER_READ);
                vstr_add_strn(vstr, (const char *)bufinfo_key.buf, bufinfo_key.len);

                mp_buffer_info_t bufinfo_value;
                mp_get_buffer_raise(cbor_encode(map->table[i].value), &bufinfo_value, MP_BUFFER_READ);
                vstr_add_strn(vstr, (const char *)bufinfo_value.buf, bufinfo_value.len);
            }
        }
#endif
    }
    return vstr;
}

STATIC mp_obj_t cbor_encode(mp_obj_t obj_data)
{
    vstr_t result_vstr;
    vstr_init(&result_vstr, 16);
    const mp_obj_type_t *obj_data_type = mp_obj_get_type(obj_data);

    vstr_t *vstr = NULL;
    if (obj_data_type == &mp_type_int)
    {
        vstr = cbor_dump_int(obj_data, 0);
    }
    else if (obj_data_type == &mp_type_bool)
    {
        vstr = cbor_dump_bool(obj_data);
    }
    else if (obj_data_type == &mp_type_str)
    {
        vstr = cbor_dump_text(obj_data);
    }
    else if (obj_data_type == &mp_type_bytes || obj_data_type == &mp_type_bytearray || obj_data_type == &mp_type_memoryview)
    {
        vstr = cbor_dump_bytes(obj_data);
    }
    else if (obj_data_type == &mp_type_list || obj_data_type == &mp_type_tuple)
    {
        vstr = cbor_dump_list(obj_data);
    }
    else if (obj_data_type == &mp_type_dict)
    {
        vstr = cbor_dump_dict(obj_data);
    }
    else
    {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported value: %s"), mp_obj_get_type_str(obj_data)));
    }

    if (vstr != NULL)
    {
        vstr_add_strn(&result_vstr, vstr->buf, vstr->len);
        vstr_free(vstr);
    }

    return mp_obj_new_bytes((byte *)result_vstr.buf, result_vstr.len);
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