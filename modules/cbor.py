# -*- coding: utf-8 -*-
# pylint: disable=import-error
try:
    import _cbor

    def encode(data):
        r = _cbor.dumps(data)
        return r

    def decode(data):
        r = _cbor.loads(data)
        return r
except ImportError:
    import micropython
    import ustruct

    @micropython.native
    def dumps(data):
        def _dump_int(data, mt=0):
            if data < 0:
                mt = 1
                data = -1 - data

            mt = mt << 5
            if data <= 23:
                args = ('>B', mt | data)
            elif data <= 0xff:
                args = ('>BB', mt | 24, data)
            elif data <= 0xffff:
                args = ('>BH', mt | 25, data)
            elif data <= 0xffffffff:
                args = ('>BI', mt | 26, data)
            else:
                args = ('>BQ', mt | 27, data)
            return ustruct.pack(*args)

        def _dump_bool(data):
            return b'\xf5' if data else b'\xf4'

        def _dump_list(data):
            return _dump_int(len(data), mt=4) + b''.join([dumps(x) for x in data])

        def _sort_keys(entry):
            key = entry[0]
            return key[0], len(key), key

        def _dump_dict(data):
            items = [(dumps(k), dumps(v)) for k, v in data.items()]
            items.sort(key=_sort_keys)
            return _dump_int(len(items), mt=5) + b''.join([k+v for (k, v) in items])

        def _dump_bytes(data):
            return _dump_int(len(data), mt=2) + data

        def _dump_text(data):
            data_bytes = data.encode('utf8')
            return _dump_int(len(data_bytes), mt=3) + data_bytes

        _SERIALIZERS = {
            bool: _dump_bool,
            int: _dump_int,
            dict: _dump_dict,
            list: _dump_list,
            str: _dump_text,
            bytes: _dump_bytes,
            memoryview: _dump_bytes,
        }

        if type(data) in _SERIALIZERS:
            return _SERIALIZERS[type(data)](data)
        raise ValueError('Unsupported value: {!r}'.format(data))

    def encode(data):
        return dumps(data)

    @micropython.native
    def loads(data):
        def _load_int(ai, data):
            if ai < 24:
                return ai, data
            elif ai == 24:
                return data[0], data[1:]
            elif ai == 25:
                return ustruct.unpack_from('>H', data)[0], data[2:]
            elif ai == 26:
                return ustruct.unpack_from('>I', data)[0], data[4:]
            elif ai == 27:
                return ustruct.unpack_from('>Q', data)[0], data[8:]
            raise ValueError('Invalid additional information')

        def _load_nint(ai, data):
            val, rest = _load_int(ai, data)
            return -1 - val, rest

        def _load_bool(ai, data):
            return ai == 21, data

        def _load_bytes(ai, data):
            l, data = _load_int(ai, data)
            return data[:l], data[l:]

        def _load_text(ai, data):
            enc, rest = _load_bytes(ai, data)
            return enc.decode('utf8'), rest

        def _load_array(ai, data):
            l, data = _load_int(ai, data)
            values = []
            for _ in range(l):
                val, data = loads(data)
                values.append(val)
            return values, data

        def _load_map(ai, data):
            l, data = _load_int(ai, data)
            values = {}
            for _ in range(l):
                k, data = loads(data)
                v, data = loads(data)
                values[k] = v
            return values, data

        _DESERIALIZERS = {
            0: _load_int,
            1: _load_nint,
            2: _load_bytes,
            3: _load_text,
            4: _load_array,
            5: _load_map,
            7: _load_bool
        }

        if not isinstance(data, (bytes, bytearray, memoryview)) and not len(data):
            raise TypeError
        fb = data[0]
        if fb >> 5 in _DESERIALIZERS:
            return _DESERIALIZERS[fb >> 5](fb & 0b11111, data[1:])
        raise ValueError('Unsupported major type: {!r}'.format(fb >> 5))

    def decode(data):
        value, _ = loads(data)
        return value
