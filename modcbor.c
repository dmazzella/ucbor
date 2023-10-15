/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Damiano Mazzella
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

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "py/runtime.h"
#include "py/binary.h"
#include "py/objstr.h"
#include "py/objint.h"

#define VSTR_INIT(vstr, alloc) \
    vstr_t vstr;               \
    vstr_init(&vstr, (alloc));

#define GET_ARRAY(array_obj) \
    size_t array_len;        \
    mp_obj_t *array_items;   \
    mp_obj_get_array(array_obj, &array_len, &array_items);

uint16_t ucbor_bswap16(uint16_t x)
{
    return (x >> 8) | (x << 8);
}

uint32_t ucbor_bswap32(uint32_t x)
{
    return (x >> 24) | ((x >> 8) & 0xff00UL) | ((x << 8) & 0xff0000UL) | (x << 24);
}

#if 0
uint64_t ucbor_bswap64(uint64_t x) {
    /* XXX */
}
#endif

STATIC mpz_t *mp_mpz_for_int(mp_obj_t arg, mpz_t *temp)
{
    if (MP_OBJ_IS_SMALL_INT(arg))
    {
        mpz_init_from_int(temp, MP_OBJ_SMALL_INT_VALUE(arg));
        return temp;
    }
    else
    {
        mp_obj_int_t *arp_p = MP_OBJ_TO_PTR(arg);
        return &(arp_p->mpz);
    }
}

STATIC mp_obj_t int_bit_length(mp_obj_t x)
{
    mpz_t n_temp;
    mpz_t *n = mp_mpz_for_int(x, &n_temp);
    if (mpz_is_zero(n))
    {
        return mp_obj_new_int_from_uint(0);
    }
    mpz_t *dest = m_new_obj(mpz_t);
    dest->neg = n->neg;
    dest->fixed_dig = 0;
    dest->alloc = n->alloc;
    dest->len = n->len;
    dest->dig = m_new(mpz_dig_t, n->alloc);
    memcpy(dest->dig, n->dig, n->alloc * sizeof(mpz_dig_t));
    mpz_abs_inpl(dest, dest);
    mp_uint_t num_bits = 0;
    while (dest->len > 0)
    {
        mpz_shr_inpl(dest, dest, 1);
        num_bits++;
    }
    if (dest != NULL)
    {
        m_del(mpz_dig_t, dest->dig, dest->alloc);
        m_del_obj(mpz_t, dest);
    }
    if (n == &n_temp)
    {
        mpz_deinit(n);
    }
    return mp_obj_new_int_from_ull(num_bits);
}

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

#if MICROPY_PY_BUILTINS_FLOAT
STATIC mp_obj_t cbor_load_half_float(const byte ai, vstr_t *data_vstr)
{
    if (data_vstr->len < sizeof(uint16_t))
    {
        mp_raise_ValueError(MP_ERROR_TEXT("Buffer to small"));
    }

    union
    {
        uint8_t i8[8];
        uint16_t i16[4];
        uint32_t i32[2];
        uint64_t i64[1];
        double f;
    } fp_dp;

    uint16_t u16 = ((uint8_t)data_vstr->buf[0] << 8) + (uint8_t)data_vstr->buf[1];
    int16_t exp = (int16_t)((u16 >> 10) & 0x1fU) - 15;

    /* Reconstruct IEEE double into little endian order first, then convert
     * to host order.
     */

    memset((void *)&fp_dp, 0, sizeof(fp_dp));

    if (exp == -15)
    {
        /* Zero or denormal; but note that half float
         * denormals become double normals.
         */
        if ((u16 & 0x03ffU) == 0)
        {
            fp_dp.i8[7] = data_vstr->buf[0] & 0x80U;
        }
        else
        {
            /* Create denormal by first creating a double that
             * contains the denormal bits and a leading implicit
             * 1-bit.  Then subtract away the implicit 1-bit.
             *
             *    0.mmmmmmmmmm * 2^-14
             *    1.mmmmmmmmmm 0.... * 2^-14
             *   -1.0000000000 0.... * 2^-14
             *
             * Double exponent: -14 + 1023 = 0x3f1
             */
            fp_dp.i8[7] = 0x3fU;
            fp_dp.i8[6] = 0x10U + (uint8_t)((u16 >> 6) & 0x0fU);
            fp_dp.i8[5] = (uint8_t)((u16 << 2) & 0xffU); /* Mask is really 0xfcU */

            fp_dp.f = fp_dp.f - (double)0.00006103515625; /* 2^(-14) */
            if (u16 & 0x8000U)
            {
                fp_dp.f = -fp_dp.f;
            }
            vstr_cut_head_bytes(data_vstr, sizeof(uint16_t));
            return mp_obj_new_float((mp_float_t)fp_dp.f);
        }
    }
    else if (exp == 16)
    {
        /* +/- Inf or NaN. */
        if ((u16 & 0x03ffU) == 0)
        {
            fp_dp.i8[7] = (data_vstr->buf[0] & 0x80U) + 0x7fU;
            fp_dp.i8[6] = 0xf0U;
        }
        else
        {
            /* Create a 'quiet NaN' with highest
             * bit set (there are some platforms
             * where the NaN payload convention is
             * the opposite).  Keep sign.
             */
            fp_dp.i8[7] = (data_vstr->buf[0] & 0x80U) + 0x7fU;
            fp_dp.i8[6] = 0xf8U;
        }
    }
    else
    {
        /* Normal. */
        uint32_t tmp = 0;
        tmp = (data_vstr->buf[0] & 0x80U) ? 0x80000000UL : 0UL;
        tmp += (uint32_t)(exp + 1023) << 20;
        tmp += (uint32_t)(data_vstr->buf[0] & 0x03U) << 18;
        tmp += (uint32_t)(data_vstr->buf[1] & 0xffU) << 10;
        fp_dp.i8[7] = (tmp >> 24) & 0xffU;
        fp_dp.i8[6] = (tmp >> 16) & 0xffU;
        fp_dp.i8[5] = (tmp >> 8) & 0xffU;
        fp_dp.i8[4] = (tmp >> 0) & 0xffU;
    }

    vstr_cut_head_bytes(data_vstr, sizeof(uint16_t));
    return mp_obj_new_float((mp_float_t)fp_dp.f);
}

STATIC mp_obj_t cbor_load_float(const byte ai, vstr_t *data_vstr)
{
    if (data_vstr->len < sizeof(uint32_t))
    {
        mp_raise_ValueError(MP_ERROR_TEXT("Buffer to small"));
    }

    union
    {
        uint8_t i8[4];
        uint16_t i16[2];
        uint32_t i32[1];
        float f;
    } fp_sp;

    memset((void *)&fp_sp, 0, sizeof(fp_sp));
    // memcpy((void *)&fp_dp.i8, (const void *)data_vstr->buf, sizeof(uint32_t));

    long long val = mp_binary_get_int(sizeof(uint32_t), true, 1, (const byte *)data_vstr->buf);
    fp_sp.i32[0] = val;

    vstr_cut_head_bytes(data_vstr, sizeof(uint32_t));
    return mp_obj_new_float((mp_float_t)fp_sp.f);
}

STATIC mp_obj_t cbor_load_double(const byte ai, vstr_t *data_vstr)
{
    if (data_vstr->len < sizeof(uint64_t))
    {
        mp_raise_ValueError(MP_ERROR_TEXT("Buffer to small"));
    }

    union
    {
        uint8_t i8[8];
        uint16_t i16[4];
        uint32_t i32[2];
        uint64_t i64[1];
        double f;
    } fp_dp;

    memset((void *)&fp_dp, 0, sizeof(fp_dp));
    long long val = mp_binary_get_int(sizeof(uint64_t), true, 1, (const byte *)data_vstr->buf);
    fp_dp.i64[0] = val;

    vstr_cut_head_bytes(data_vstr, sizeof(uint64_t));
    return mp_obj_new_float((mp_float_t)fp_dp.f);
}
#endif

STATIC mp_obj_t cbor_load_special(const byte ai, vstr_t *data_vstr)
{
    switch (ai)
    {
    case 20:
    {
        return mp_const_false;
    }
    case 21:
    {
        return mp_const_true;
    }
    case 22:
    case 23:
    {
        return mp_const_none;
    }
    case 24:
    {
        break;
    }
    case 25:
    {
/* half-float (2 bytes) */
#if MICROPY_PY_BUILTINS_FLOAT
        return cbor_load_half_float(ai, data_vstr);
#else
        break;
#endif
    }
    case 26:
    {
/* float (4 bytes) */
#if MICROPY_PY_BUILTINS_FLOAT
        return cbor_load_float(ai, data_vstr);
#else
        break;
#endif
    }
    case 27:
    {
/* double (8 bytes) */
#if MICROPY_PY_BUILTINS_FLOAT
        return cbor_load_double(ai, data_vstr);
#else
        break;
#endif
    }
    default:
    {
        break;
    }
    }

    nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported additional information: %d"), ai));
}

STATIC mp_cbor_load_func_t load_functions_map[] = {
    {0, cbor_load_int},
    {1, cbor_load_uint},
    {2, cbor_load_bytes},
    {3, cbor_load_text},
    {4, cbor_load_list},
    {5, cbor_load_dict},
    {6, cbor_unsupported_major_type},
    {7, cbor_load_special},
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
    if (MP_OBJ_IS_SMALL_INT(obj_data))
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
        else
        {
            mp_int_t size = 0;
            if (data <= 0xffff)
            {
                vstr_add_byte(data_vstr, (byte)(mt | 25));
                size = sizeof(uint16_t);
            }
            else if (data <= 0xffffffff)
            {
                vstr_add_byte(data_vstr, (byte)(mt | 26));
                size = sizeof(uint32_t);
            }
            else
            {
                vstr_add_byte(data_vstr, (byte)(mt | 27));
                size = sizeof(uint64_t);
            }

            vstr_add_len(data_vstr, size);
            byte *p = (byte *)data_vstr->buf + 1;
            mp_binary_set_int(size, 1, p, data);
        }
    }
    else
    {
        mp_int_t size = ((mp_obj_get_int(int_bit_length(obj_data)) + 7) / 8);
        mpz_t o_temp;
        mpz_t *o_temp_p = mp_mpz_for_int(obj_data, &o_temp);

        vstr_add_byte(data_vstr, (byte)(mt | 27));
        vstr_add_len(data_vstr, size);
        mpz_as_bytes(o_temp_p, 1, data_vstr->len - 1, (byte *)data_vstr->buf + 1);

        if (o_temp_p == &o_temp)
        {
            mpz_deinit(o_temp_p);
        }
    }
}

STATIC void cbor_dump_int(mp_obj_t obj_data, vstr_t *data_vstr)
{
    cbor_dump_int_with_major_type(obj_data, data_vstr, 0);
}

#if MICROPY_PY_BUILTINS_FLOAT
STATIC void cbor_dump_double_big(mp_obj_t obj_data, vstr_t *data_vstr)
{
    vstr_add_byte(data_vstr, (byte)0xfb);
    vstr_add_len(data_vstr, sizeof(uint64_t));

    byte *p = (byte *)data_vstr->buf + 1;

    union
    {
        uint8_t i8[8];
        uint16_t i16[4];
        uint32_t i32[2];
        uint64_t i64[1];
        double f;
    } fp_dp;
    fp_dp.f = mp_obj_get_float_to_d(obj_data);

    mp_binary_set_int(sizeof(uint32_t), 1, p, fp_dp.i32[1]);
    mp_binary_set_int(sizeof(uint32_t), 1, p + sizeof(uint32_t), fp_dp.i32[0]);
}

STATIC void cbor_dump_float_big(mp_obj_t obj_data, vstr_t *data_vstr)
{
    vstr_add_byte(data_vstr, (byte)0xfa);
    vstr_add_len(data_vstr, sizeof(uint32_t));

    byte *p = (byte *)data_vstr->buf + 1;

    union
    {
        uint8_t i8[4];
        uint16_t i16[2];
        uint32_t i32[1];
        float f;
    } fp_sp;
    fp_sp.f = mp_obj_get_float_to_f(obj_data);

    mp_binary_set_int(sizeof(uint32_t), 1, p, fp_sp.i32[0]);
}

STATIC void cbor_dump_float(mp_obj_t obj_data, vstr_t *data_vstr)
{
    union
    {
        uint8_t i8[8];
        uint16_t i16[4];
        uint32_t i32[2];
        uint64_t i64[1];
        double f;
    } fp_dp;
    fp_dp.f = mp_obj_get_float_to_d(obj_data);

    /* Check if 'd' can represented as a normal half-float.
     * Denormal half-floats could also be used, but that check
     * isn't done now (denormal half-floats are decoded of course).
     * So just check exponent range and that at most 10 significant
     * bits (excluding implicit leading 1) are used in 'd'.
     */
    uint16_t u16 = (((uint16_t)fp_dp.i8[7]) << 8) | ((uint16_t)fp_dp.i8[6]);
    int16_t exp = (int16_t)((u16 & 0x7ff0U) >> 4) - 1023;

    /* identity if d is +/- 0.0
     */
    if (exp == -1023)
    {
        vstr_add_byte(data_vstr, (byte)0xf9);
        vstr_add_byte(data_vstr, (byte)((signbit(fp_dp.f)) ? 0x80 : 00));
        vstr_add_byte(data_vstr, (byte)0x00);
        return;
    }

    if (exp >= -14 && exp <= 15)
    {
        /* Half-float normal exponents (excl. denormals).
         *
         *          7        6        5        4        3        2        1        0  (LE index)
         * double: seeeeeee eeeemmmm mmmmmmmm mmmmmmmm mmmmmmmm mmmmmmmm mmmmmmmm mmmmmmmm
         * half:         seeeee mmmm mmmmmm00 00000000 00000000 00000000 00000000 00000000
         */
        int use_half_float = (fp_dp.i8[0] == 0 && fp_dp.i8[1] == 0 && fp_dp.i8[2] == 0 && fp_dp.i8[3] == 0 && fp_dp.i8[4] == 0 && (fp_dp.i8[5] & 0x03U) == 0);
        if (use_half_float)
        {
            uint16_t t = 0;
            exp += 15;
            t += (uint16_t)(fp_dp.i8[7] & 0x80U) << 8;
            t += (uint16_t)exp << 10;
            t += ((uint16_t)fp_dp.i8[6] & 0x0fU) << 6;
            t += ((uint16_t)fp_dp.i8[5]) >> 2;

            vstr_add_byte(data_vstr, (byte)0xf9);
            vstr_add_len(data_vstr, sizeof(uint16_t));

            byte *p = (byte *)data_vstr->buf + 1;
            mp_binary_set_int(sizeof(uint16_t), 1, p, t);
            return;
        }
    }

    /* Same check for plain float.  Also no denormal support here. */
    if (exp >= -126 && exp <= 127)
    {
        /* Float normal exponents (excl. denormals).
         *
         * double: seeeeeee eeeemmmm mmmmmmmm mmmmmmmm mmmmmmmm mmmmmmmm mmmmmmmm mmmmmmmm
         * float:     seeee eeeemmmm mmmmmmmm mmmmmmmm mmm00000 00000000 00000000 00000000
         */

        /* We could do this explicit mantissa check, but doing
         * a double-float-double cast is fine because we've
         * already verified that the exponent is in range so
         * that the narrower cast is not undefined behavior.
         */
        float d_float = (float)fp_dp.f;
        if (((double)d_float == fp_dp.f))
        {
            cbor_dump_float_big(obj_data, data_vstr);
            return;
        }
    }

    /* Special handling for NaN and Inf which we want to encode as
     * half-floats.  They share the same (maximum) exponent.
     */
    if (exp == 1024)
    {
        if (isnan(fp_dp.f))
        {
            vstr_add_byte(data_vstr, (byte)0xf9);
            vstr_add_byte(data_vstr, (byte)0x7e);
            vstr_add_byte(data_vstr, (byte)0x00);
        }
        else if (isinf(fp_dp.f))
        {
            vstr_add_byte(data_vstr, (byte)0xf9);
            vstr_add_byte(data_vstr, (byte)(signbit(fp_dp.f) ? 0xfc : 0x7c));
            vstr_add_byte(data_vstr, (byte)0x00);
        }
        return;
    }

    /* Cannot use half-float or float, encode as full IEEE double. */
    cbor_dump_double_big(obj_data, data_vstr);
}
#endif

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

STATIC void cbor_dump_none(mp_obj_t obj_data, vstr_t *data_vstr)
{
    vstr_add_byte(data_vstr, (byte)0xf6);
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
#if MICROPY_PY_BUILTINS_FLOAT
    {&mp_type_float, cbor_dump_float},
#endif
    {&mp_type_bool, cbor_dump_bool},
    {&mp_type_NoneType, cbor_dump_none},
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
            VSTR_INIT(temp_data_vstr, 16);
            if (need_temp_data_vstr)
            {
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
MP_REGISTER_MODULE(MP_QSTR_cbor, mp_module_ucbor);
