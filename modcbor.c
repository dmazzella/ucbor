/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Damiano Mazzella
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

#include "py/objstr.h"
#include "py/objint.h"
#include "py/runtime.h"

#include "cbor.h"

void *malloc(size_t n)
{
    void *ptr = m_malloc(n);
    return ptr;
}

void free(void *ptr)
{
    m_free(ptr);
}

STATIC mp_obj_t cbor_it_to_mp_obj_recursive(CborValue *it, mp_obj_t parent_obj)
{
    bool dict_value_next = false;
    mp_obj_t dict_key = mp_const_none;
    CborError err;

    while (!cbor_value_at_end(it))
    {

        CborType type = cbor_value_get_type(it);

        mp_obj_t next_element = mp_const_none;

        switch (type)
        {
        case CborIntegerType:
        {
            int64_t val;
            cbor_value_get_int64(it, &val);
            next_element = mp_obj_new_int(val);
            break;
        }

        case CborByteStringType:
        {
            uint8_t *buf;
            size_t n;
            err = cbor_value_dup_byte_string(it, &buf, &n, it);
            if (err)
                mp_raise_ValueError("parse bytestring failed");
            next_element = mp_obj_new_bytes(buf, n);
            m_free(buf);
            break;
        }

        case CborTextStringType:
        {
            char *buf;
            size_t n;
            err = cbor_value_dup_text_string(it, &buf, &n, it);
            if (err)
                mp_raise_ValueError("parse string failed");
            next_element = mp_obj_new_str(buf, n);
            m_free(buf);
            break;
        }

        case CborTagType:
        {
            mp_raise_ValueError("unknown tag present");
            break;
        }

        case CborSimpleType:
        {
            mp_raise_ValueError("unknown simple value present");
            break;
        }

        case CborNullType:
            next_element = mp_const_none;
            break;

        case CborUndefinedType:
            mp_raise_ValueError("undefined type encountered");
            break;

        case CborBooleanType:
        {
            bool val;
            cbor_value_get_boolean(it, &val);
            next_element = mp_obj_new_bool(val);
            break;
        }

        case CborDoubleType:
        {
            double val;
            cbor_value_get_double(it, &val);
            next_element = mp_obj_new_float(val);
            break;
        }

        case CborFloatType:
        {
            float val;
            cbor_value_get_float(it, &val);
            next_element = mp_obj_new_float(val);
            break;
        }

        case CborHalfFloatType:
            mp_raise_NotImplementedError("half float type not supported");
            break;

        case CborInvalidType:
            mp_raise_ValueError("invalid type encountered");
            break;

        case CborArrayType:
        case CborMapType:
        {

            CborValue recursed;
            assert(cbor_value_is_container(it));

            err = cbor_value_enter_container(it, &recursed);
            if (err)
                mp_raise_ValueError("parse error");

            if (type == CborArrayType)
            {
                next_element = mp_obj_new_list(0, NULL);
            }
            else
            {
                next_element = mp_obj_new_dict(0);
            }

            cbor_it_to_mp_obj_recursive(&recursed, next_element);
            err = cbor_value_leave_container(it, &recursed);
            if (err)
                mp_raise_ValueError("parse error");

            break;
        }

        default:
            assert(false); // should never happen
        }

        if (parent_obj == mp_const_none)
        {
            return next_element;
        }
        else
        {
            const mp_obj_type_t *parent_type = mp_obj_get_type(parent_obj);

            if (parent_type == &mp_type_list)
            {
                mp_obj_list_append(parent_obj, next_element);
            }
            else if (parent_type == &mp_type_dict)
            {
                if (dict_value_next)
                {
                    mp_obj_dict_store(parent_obj, dict_key, next_element);
                    dict_value_next = false;
                    dict_key = mp_const_none;
                }
                else
                {
                    dict_key = next_element;
                    dict_value_next = true;
                }
            }
            else
            {
                assert(false); // should never happen
            }
        }

        // some element types can have a variable length, so the iterator is advanced during processing.
        // here we advance it for all other types
        if (type != CborArrayType &&
            type != CborMapType &&
            type != CborTextStringType &&
            type != CborByteStringType)
        {
            err = cbor_value_advance_fixed(it);
            if (err)
                mp_raise_ValueError("parse error");
        }
    }

    if (dict_value_next)
        mp_raise_ValueError("key with no value in map");

    return parent_obj;
}

STATIC mp_obj_t cbor_loads(mp_obj_t buf_obj)
{
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_obj, &bufinfo, MP_BUFFER_READ);

    CborParser parser;
    CborValue it;
    CborError err = cbor_parser_init(bufinfo.buf, bufinfo.len, 0, &parser, &it);
    if (err != CborNoError)
    {
        mp_raise_ValueError("tinycbor init failed");
    }

    mp_obj_t result = cbor_it_to_mp_obj_recursive(&it, mp_const_none);

    return result;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_loads_obj, cbor_loads);

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
    return mp_obj_new_tuple(3, sort_tuple);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_sort_key_obj, cbor_sort_key);

STATIC uint8_t *mp_obj_to_cbor_text_recursive(mp_obj_t x_obj, CborEncoder *parent_enc, size_t *encoded_len)
{
    CborEncoder new_enc;
    CborEncoder *enc;

    if (parent_enc == NULL)
    {
        enc = &new_enc;
    }
    else
    {
        enc = parent_enc;
    }

    CborError err = CborNoError;
    const mp_obj_type_t *parent_type = mp_obj_get_type(x_obj);

    size_t bufsize = 64;
    uint8_t *buf = NULL;

    // this uses either 1 or 2 pass encoding. If the first pass runs out of memory, it continues to encode in order to
    // count the final encoded size. Then it reallocates the buffer to the appropriate size and encodes again.
    while (true)
    {
        if (parent_enc == NULL)
        {
            buf = m_realloc(buf, bufsize);
            cbor_encoder_init(enc, buf, bufsize, 0);
        }

        if (parent_type == &mp_type_NoneType)
        {
            err = cbor_encode_null(enc);
        }
        else if (parent_type == &mp_type_int)
        {
            err = cbor_encode_int(enc, mp_obj_get_int(x_obj));
        }
        else if (parent_type == &mp_type_bool)
        {
            err = cbor_encode_boolean(enc, x_obj == mp_const_true);
        }
        else if (parent_type == &mp_type_float)
        {
            err = cbor_encode_double(enc, mp_obj_get_float(x_obj));
        }
        else if (parent_type == &mp_type_str)
        {
            size_t len = 0;
            const char *text = mp_obj_str_get_data(x_obj, &len);
            err = cbor_encode_text_string(enc, text, len);
        }
        else if (parent_type == &mp_type_bytes || parent_type == &mp_type_memoryview)
        {
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(x_obj, &bufinfo, MP_BUFFER_READ);
            err = cbor_encode_byte_string(enc, (byte *)bufinfo.buf, bufinfo.len);
        }
        else if (parent_type == &mp_type_list || parent_type == &mp_type_tuple)
        {
            size_t len;
            mp_obj_t *items;
            mp_obj_get_array(x_obj, &len, &items);

            CborEncoder list_enc;
            err = cbor_encoder_create_array(enc, &list_enc, len);
            if (err != CborNoError && err != CborErrorOutOfMemory)
            {
                mp_raise_ValueError("Failed to encode array");
            }

            for (size_t i = 0; i < len; ++i)
            {
                mp_obj_to_cbor_text_recursive(items[i], &list_enc, NULL);
            }
            err = cbor_encoder_close_container(enc, &list_enc);
        }
        else if (parent_type == &mp_type_dict)
        {
            mp_map_t *map = mp_obj_dict_get_map(x_obj);
            CborEncoder dict_enc;
            err = cbor_encoder_create_map(enc, &dict_enc, map->used);
            if (err != CborNoError && err != CborErrorOutOfMemory)
            {
                mp_raise_ValueError("Failed to encode map");
            }

            bool canonical = true;
            if (canonical)
            {
                mp_obj_t list_key_sort = mp_obj_new_list(0, NULL);

                mp_map_elem_t *elem = map->table;
                for (size_t i = 0; i < map->used; i++, elem++)
                {
                    if (mp_map_slot_is_filled(map, i))
                    {
                        CborEncoder dict_key_enc;
                        CborEncoder *p_dict_key_enc = &dict_key_enc;
                        size_t dict_key_bufsize = 16;
                        uint8_t *dict_key_buf = (uint8_t *)m_malloc(dict_key_bufsize);
                        cbor_encoder_init(p_dict_key_enc, dict_key_buf, dict_key_bufsize, 0);

                        mp_obj_to_cbor_text_recursive(elem->key, p_dict_key_enc, &dict_key_bufsize);
                        mp_obj_t key_bytes = mp_obj_new_bytes(dict_key_buf, dict_key_bufsize);
                        mp_obj_t key_index = mp_obj_new_int(i);
                        mp_obj_t kv[2] = {key_bytes, key_index};
                        mp_obj_list_append(list_key_sort, mp_obj_new_tuple(2, kv));

                        m_free(dict_key_buf);
                    }
                }
                mp_obj_t kwa = mp_obj_new_dict(0);
                mp_obj_dict_store(kwa, MP_ROM_QSTR(MP_QSTR_key), MP_OBJ_FROM_PTR(&cbor_sort_key_obj));
                mp_obj_list_sort(1, &list_key_sort, mp_obj_dict_get_map(kwa));

                size_t len;
                mp_obj_t *items;
                mp_obj_get_array(list_key_sort, &len, &items);
                for (int i = 0; i < len; i++)
                {
                    mp_obj_tuple_t *entry_tuple = MP_OBJ_TO_PTR(items[i]);
                    mp_int_t map_index = mp_obj_get_int(entry_tuple->items[1]);
                    if (mp_map_slot_is_filled(map, map_index))
                    {
                        mp_map_elem_t *e = &map->table[map_index];
                        mp_obj_to_cbor_text_recursive(e->key, &dict_enc, NULL);
                        mp_obj_to_cbor_text_recursive(e->value, &dict_enc, NULL);
                    }
                }
            }
            else
            {
                mp_map_elem_t *elem = map->table;
                for (size_t i = 0; i < map->used; i++, elem++)
                {
                    if (mp_map_slot_is_filled(map, i))
                    {
                        mp_obj_to_cbor_text_recursive(elem->key, &dict_enc, NULL);
                        mp_obj_to_cbor_text_recursive(elem->value, &dict_enc, NULL);
                    }
                }
            }
            err = cbor_encoder_close_container(enc, &dict_enc);
        }
        else
        {
            nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "Found object which cannot be encoded, got %s", mp_obj_get_type_str(x_obj)));
        }

        if (err == CborNoError)
        {
            break;
        }
        else if (err == CborErrorOutOfMemory)
        {
            if (parent_enc == NULL)
            {
                bufsize += cbor_encoder_get_extra_bytes_needed(enc);
                continue;
            }
            else
            {
                if (buf != NULL)
                {
                    m_free(buf);
                }
                return NULL;
            }
        }
        else
        {
            printf("else err: %d\n", err);
            if (buf != NULL)
            {
                m_free(buf);
            }
            mp_raise_ValueError("CBOR encoding failed");
        }
    }

    if (parent_enc == NULL)
    {
        *encoded_len = cbor_encoder_get_buffer_size(enc, buf);
        return buf;
    }
    else
    {
        return NULL;
    }
}

STATIC mp_obj_t cbor_dumps(mp_obj_t x_obj)
{
    size_t len = 0;
    uint8_t *buf = mp_obj_to_cbor_text_recursive(x_obj, NULL, &len);

    if (buf == NULL)
    {
        printf("ebuf == NULL\n");
        mp_raise_ValueError("CBOR encoding failed");
    }

    mp_obj_t result = mp_obj_new_bytes(buf, len);
    m_free(buf);

    return result;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(cbor_dumps_obj, cbor_dumps);

STATIC const mp_rom_map_elem_t mp_module_ucbor_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__cbor)},
    {MP_ROM_QSTR(MP_QSTR_loads), MP_ROM_PTR(&cbor_loads_obj)},
    {MP_ROM_QSTR(MP_QSTR_dumps), MP_ROM_PTR(&cbor_dumps_obj)},
};

STATIC MP_DEFINE_CONST_DICT(mp_module_ucbor_globals, mp_module_ucbor_globals_table);

const mp_obj_module_t mp_module_ucbor = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_ucbor_globals,
};

// Register the module to make it available in Python
MP_REGISTER_MODULE(MP_QSTR__cbor, mp_module_ucbor, MICROPY_PY_UCBOR);

#endif // MICROPY_PY_UCBOR