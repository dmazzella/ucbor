# -*- coding: utf-8 -*-
# pylint:disable=unresolved-import
import gc
import cbor
import ucbor

import utime


def cbor_encode(d):
    gc.collect()
    m0 = gc.mem_alloc()
    t0 = utime.ticks_us()
    e = cbor.encode(d)
    t1 = utime.ticks_us()
    gc.collect()
    m1 = gc.mem_alloc()
    print(f"cbor: {utime.ticks_diff(t1, t0):.2f} {m1 - m0}")
    return e


def ucbor_encode(d):
    gc.collect()
    m0 = gc.mem_alloc()
    t0 = utime.ticks_us()
    e = ucbor.encode(d)
    t1 = utime.ticks_us()
    gc.collect()
    m1 = gc.mem_alloc()
    print(f"ucbor: {utime.ticks_diff(t1, t0):.2f} {m1 - m0}")
    return e


def cbor_decode(e):
    gc.collect()
    m0 = gc.mem_alloc()
    t0 = utime.ticks_us()
    d = cbor.decode(e)
    t1 = utime.ticks_us()
    gc.collect()
    m1 = gc.mem_alloc()
    print(f"cbor: {utime.ticks_diff(t1, t0):.2f} {m1 - m0}")
    return d


def ucbor_decode(e):
    gc.collect()
    m0 = gc.mem_alloc()
    t0 = utime.ticks_us()
    d = ucbor.decode(e)
    t1 = utime.ticks_us()
    gc.collect()
    m1 = gc.mem_alloc()
    print(f"ucbor: {utime.ticks_diff(t1, t0):.2f} {m1 - m0}")
    return d


def encode():
    print("encode")

    # bool
    for d in (True, False):
        assert cbor_encode(d) == ucbor_encode(d), d

    # int
    for d in (0, 23, 24, 255, 256, 65535, 65536, 4294967295, 4294967296, -1, -24, -25):
        try:
            assert cbor_encode(d) == ucbor_encode(d), d
        except OverflowError:
            pass
        except AssertionError:
            if d < 4294967296:
                raise

    # bytes
    for d in (
        b"OF:2\xa6~=[\x87\xf3N\xb6T\xd9\xd26\n=X\x94\x1e\xac2\xb2o\xf1\xcd{\xb1\x1e\xb4,\x0f\xf2\xfe\xba\x97\xecYS\x13\xd7\x00\xb4\\\xb5\x1aX\x9aF_",
        b"",
    ):
        assert cbor_encode(d) == ucbor_encode(d), d

    # str
    for d in (
        "dma@lemmeen.onmicrosoft.com",
        "Damiano Mazzella",
        "\1\2\3\4",
        "ü",
        "水",
        "𐅑",
    ):
        assert cbor_encode(d) == ucbor_encode(d), d

    # list
    l1 = ["usb", "nfc", "ble"]
    l2 = [1, True, False, 0xffffffff, {
        u"foo": b"\x80\x01\x02", u"bar": [1, 2, 3, {u"a": [1, 2, 3, {}]}]}, -1]
    for d in (l1, l2):
        assert cbor_encode(d) == ucbor_encode(d), d

    # dict
    d1 = {
        4: {
            "name": "dma@lemmeen.onmicrosoft.com",
            "displayName": "Damiano Mazzella",
            "id": b"OF:2\xa6~=[\x87\xf3N\xb6T\xd9\xd26\n=X\x94\x1e\xac2\xb2o\xf1\xcd{\xb1\x1e\xb4,\x0f\xf2\xfe\xba\x97\xecYS\x13\xd7\x00\xb4\\\xb5\x1aX\x9aF_",
        },
        1: {
            "transports": ["usb", "nfc", "ble"],
            "id": b"\x92\xb3\xf4\xa0\xfa\x8a\xe5\x92j\xe3\xc8\xf4\x8e\xddO\x8f\x1a\x91\r&\x8e|\x9fm\x84\xc5!\x9d\x97\xe3\x8f\x05\x08\xa7\x90\xd8?\x80\x9d\x8e\x8d6#9\xec?~Ic\xc5\x96:\x88\xb4\xa3\x95R\xe1q\x19{\x83\x0b\xd5\xdc\x87i8?\xf9\xf3\xb1\x8d\xdf\x15\x99J\xa5+\x14",
            "type": "public-key",
        },
        2: b"5l\x9e\xd4\xa0\x93!\xb9i_\x1e\xaf\x91\x82\x03\xf1\xb5_h\x9d\xa6\x1f\xbc\x96\x18L\x15}\xdah\x0c\x81\x05\x00\x00\x05v",
        3: b"0E\x02 !+Q\x06\xf0\x93t\x11\xd5\xffpN\x92\xca\x98\x92\x1f\xd4\x87\x80l\xcf]\x96\x11\x8b\xf8\x8a7&e\x9a\x02!\x00\xd9O\x0f\x9b\xfb5%E>k\t\x9b\x192\x8c\xea\x8c\xb2\xdc\x96x\xfd\xbf\x97\xdc\xc4\xcc\x02}\xe6%\x98",
    }
    d2 = {
        4: {"id": b"damianomazzella", "name": ""},
        1: {
            "transports": ["usb", "nfc", "ble"],
            "id": b'c\xf2\xfa$%I\xff\xfb\x9c\x0b~!\xea\xc5\xcc\xb3\x1f\x88\xd8%N\xc8\x8dk\x89\xb1\x85\x05:A\xedP\xd7\xd9V!\xda\xaf\xde2\xbe0\x9cm\x8b\xc4\xfa\xb5\xb2\x85\xca\xd8\xf0\xdf\xe1Z\xd8\x18\xcc\xb6\xdcG\xe0<\xa8\xee\xa7\xb2\x8a\xb9\x1f\x02\xe7\xcb\xaa\n\xfe"\xbcE',
            "type": "public-key",
        },
        2: b"\x9e\xf5J\xb7\x02|\xb4\xc1C\xcd\xab3\xbf\x9a(\xd5Da\x1aG\xaa7\xffM\x0b\x84\x99\x9a\xfb\xe9\xfcJ\x05\x00\x00\x05\xa4",
        3: b"0D\x02 \x06\xee\xfc\xb5\xfb\xcd\x94v\xc3\xa5x\xe1\x03n\xdfW7w\xc2\xdd\xf9i\xa2\xc5qr\xa8\xe2`?\x90\x8d\x02 6\xa4)\x1a\x8dae\xd0\xe4\xc7\xef\x97\x1b\x81\\\xc8\xc7\xfeJ>\x81\x9f\xc1\x8a\x1cc\xbd\x16\xf1\t\xfd\xc1",
    }
    d3 = {
        4: {
            "id": b"\x81\xcc\x0f\x00\x00\x00\x00\x00\x00\x00",
            "name": "testalo",
            "displayName": "testalo",
        },
        1: {
            "transports": ["usb", "nfc", "ble"],
            "id": b"\xe7^87a\xe7_<\xb2P^\xbf\x8e\x01?*2\x9e\xe3\xd1\xbe\xd2H\xfcp\x18\x8d\xd6K\x98f\xde\x90s\x9f\x8c2la\xa6r\xba\xa4\xcd\x80\xef\xaf\xa2%\xaf\xb8\xed\x8e\xda\xb5\xfc\xba\x80\x8b\xb6\x9c}\xee\xbe\x0e\xfaH&\xcb\xe3C\xa3\xae\xd6R\xf0\x97\x01S\xed",
            "type": "public-key",
        },
        2: b"t\xa6\xea\x92\x13\xc9\x9c/t\xb2$\x92\xb3 \xcf@&*\x94\xc1\xa9P\xa09\x7f)%\x0b`\x84\x1e\xf0\x05\x00\x00\x05\xa5",
        3: b"0F\x02!\x00\x81\x05\xcc\"\x05\xe5L\xd9\x10l\xf5\x864r\xa5\x83\xc4e\xed$\xb9\xea'\xd7\xb2Ro\xea\xd0-u\xaa\x02!\x00\xc5\xd1\xfc\xa8\xb2\x84-\xb9\x04vZ9\x05s\xccU\xd6s\x80\xd5T\x00+\xdb\xb4\xa9\x90BN\xeb\xe6\x0e",
    }
    for d in (d1, d2, d3):
        assert cbor_encode(d) == ucbor_encode(d), d

    # bytearray
    for d in (bytearray(b'\x01'),):
        assert cbor_encode(d) == ucbor_encode(d), d

    # memoryview
    for d in (memoryview(b'\x01'),):
        assert cbor_encode(d) == ucbor_encode(d), d


def decode():
    print("decode")

    # bool
    for e in (b"\xf5", b"\xf4"):
        assert cbor_decode(e) == ucbor_decode(e), e

    # int
    for e in (
        b"\x00",
        b"\x17",
        b"\x18\x18",
        b"\x18\xff",
        b"\x19\x01\x00",
        b"\x19\xff\xff",
        b"\x1a\x00\x01\x00\x00",
        b"\x1a\xff\xff\xff\xff",
        b"\x1b\x00\x00\x00\x01\x00\x00\x00\x00",
        b" ",
        b"7",
        b"8\x18",
    ):
        try:
            assert cbor_decode(e) == ucbor_decode(e), e
        except OverflowError:
            pass

    # bytes
    for e in (
        b"X3OF:2\xa6~=[\x87\xf3N\xb6T\xd9\xd26\n=X\x94\x1e\xac2\xb2o\xf1\xcd{\xb1\x1e\xb4,\x0f\xf2\xfe\xba\x97\xecYS\x13\xd7\x00\xb4\\\xb5\x1aX\x9aF_",
        b"@",
    ):
        assert cbor_decode(e) == ucbor_decode(e), e

    # str
    for e in (
        b"x\x1bdma@lemmeen.onmicrosoft.com",
        b"pDamiano Mazzella",
        b"D\x01\x02\x03\x04",
        b"b\xc3\xbc",
        b"c\xe6\xb0\xb4",
        b"d\xf0\x90\x85\x91",
    ):
        assert cbor_decode(e) == ucbor_decode(e), e

    # list
    for e in (b"\x83cusbcnfccble",):
        assert cbor_decode(e) == ucbor_decode(e), e

    # dict
    for e in (
        b"\xa4\x01\xa3bidXP\x92\xb3\xf4\xa0\xfa\x8a\xe5\x92j\xe3\xc8\xf4\x8e\xddO\x8f\x1a\x91\r&\x8e|\x9fm\x84\xc5!\x9d\x97\xe3\x8f\x05\x08\xa7\x90\xd8?\x80\x9d\x8e\x8d6#9\xec?~Ic\xc5\x96:\x88\xb4\xa3\x95R\xe1q\x19{\x83\x0b\xd5\xdc\x87i8?\xf9\xf3\xb1\x8d\xdf\x15\x99J\xa5+\x14dtypejpublic-keyjtransports\x83cusbcnfccble\x02X%5l\x9e\xd4\xa0\x93!\xb9i_\x1e\xaf\x91\x82\x03\xf1\xb5_h\x9d\xa6\x1f\xbc\x96\x18L\x15}\xdah\x0c\x81\x05\x00\x00\x05v\x03XG0E\x02 !+Q\x06\xf0\x93t\x11\xd5\xffpN\x92\xca\x98\x92\x1f\xd4\x87\x80l\xcf]\x96\x11\x8b\xf8\x8a7&e\x9a\x02!\x00\xd9O\x0f\x9b\xfb5%E>k\t\x9b\x192\x8c\xea\x8c\xb2\xdc\x96x\xfd\xbf\x97\xdc\xc4\xcc\x02}\xe6%\x98\x04\xa3bidX3OF:2\xa6~=[\x87\xf3N\xb6T\xd9\xd26\n=X\x94\x1e\xac2\xb2o\xf1\xcd{\xb1\x1e\xb4,\x0f\xf2\xfe\xba\x97\xecYS\x13\xd7\x00\xb4\\\xb5\x1aX\x9aF_dnamex\x1bdma@lemmeen.onmicrosoft.comkdisplayNamepDamiano Mazzella",
    ):
        assert cbor_decode(e) == ucbor_decode(e), e


if __name__ == "__main__":
    encode()
    decode()
