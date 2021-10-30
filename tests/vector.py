# -*- coding: utf-8 -*-
# pylint:disable=unresolved-import
import binascii
import cbor


def cbor2hex(data):
    ce = cbor.encode(data)
    print(ce, data)
    return binascii.hexlify(ce).decode()


"""
From https://github.com/cbor/test-vectors
Unsupported values are commented out.
"""


def test_vectors():
    _TEST_VECTORS = [
        ("00", 0),
        ("01", 1),
        ("0a", 10),
        ("17", 23),
        ("1818", 24),
        ("1819", 25),
        ("1864", 100),
        ("1903e8", 1000),
        ("1a000f4240", 1000000),
        # ("1b000000e8d4a51000", 1000000000000),
        # ("1bffffffffffffffff", 18446744073709551615),
        # ('c249010000000000000000', 18446744073709551616),
        # ("3bffffffffffffffff", -18446744073709551616),
        # ('c349010000000000000000', -18446744073709551617),
        ("20", -1),
        ("29", -10),
        ("3863", -100),
        ("3903e7", -1000),
        # ('f90000', 0.0),
        # ('f98000', -0.0),
        # ('f93c00', 1.0),
        # ('fb3ff199999999999a', 1.1),
        # ('f93e00', 1.5),
        # ('f97bff', 65504.0),
        # ('fa47c35000', 100000.0),
        # ('fa7f7fffff', 3.4028234663852886e+38),
        # ('fb7e37e43c8800759c', 1e+300),
        # ('f90001', 5.960464477539063e-08),
        # ('f90400', 6.103515625e-05),
        # ('f9c400', -4.0),
        # ('fbc010666666666666', -4.1),
        # ('f97c00', None),
        # ('f97e00', None),
        # ('f9fc00', None),
        # ('fa7f800000', None),
        # ('fa7fc00000', None),
        # ('faff800000', None),
        # ('fb7ff0000000000000', None),
        # ('fb7ff8000000000000', None),
        # ('fbfff0000000000000', None),
        ("f4", False),
        ("f5", True),
        # ('f6', None),
        # ('f7', None),
        # ('f0', None),
        # ('f818', None),
        # ('f8ff', None),
        # ('c074323031332d30332d32315432303a30343a30305a', None),
        # ('c11a514b67b0', None),
        # ('c1fb41d452d9ec200000', None),
        # ('d74401020304', None),
        # ('d818456449455446', None),
        # ('d82076687474703a2f2f7777772e6578616d706c652e636f6d', None),
        ("40", b""),
        ("4401020304", b"\1\2\3\4"),
        ("60", ""),
        ("6161", "a"),
        ("6449455446", "IETF"),
        ("62225c", '"\\'),
        ("62c3bc", "ü"),
        ("63e6b0b4", "水"),
        ("64f0908591", "𐅑"),
        ("80", []),
        ("83010203", [1, 2, 3]),
        ("8301820203820405", [1, [2, 3], [4, 5]]),
        (
            "98190102030405060708090a0b0c0d0e0f101112131415161718181819",
            [
                1,
                2,
                3,
                4,
                5,
                6,
                7,
                8,
                9,
                10,
                11,
                12,
                13,
                14,
                15,
                16,
                17,
                18,
                19,
                20,
                21,
                22,
                23,
                24,
                25,
            ],
        ),
        ("a0", {}),
        ("a201020304", {1: 2, 3: 4}),
        ("a26161016162820203", {"a": 1, "b": [2, 3]}),
        ("826161a161626163", ["a", {"b": "c"}]),
        (
            "a56161614161626142616361436164614461656145",
            {"c": "C", "d": "D", "a": "A", "b": "B", "e": "E"},
        ),
        # ('5f42010243030405ff', None),
        # ('7f657374726561646d696e67ff', 'streaming'),
        # ('9fff', []),
        # ('9f018202039f0405ffff', [1, [2, 3], [4, 5]]),
        # ('9f01820203820405ff', [1, [2, 3], [4, 5]]),
        # ('83018202039f0405ff', [1, [2, 3], [4, 5]]),
        # ('83019f0203ff820405', [1, [2, 3], [4, 5]]),
        # ('9f0102030405060708090a0b0c0d0e0f101112131415161718181819ff', [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25]),  # noqa E501
        # ('bf61610161629f0203ffff', {'a': 1, 'b': [2, 3]}),
        # ('826161bf61626163ff', ['a', {'b': 'c'}]),
        # ('bf6346756ef563416d7421ff', {'Amt': -2, 'Fun': True}),
    ]
    for (data, value) in _TEST_VECTORS:
        try:
            assert cbor2hex(value) == data
        except Exception:
            print("ERROR in test vector, %s" % data)
            raise


"""
As defined in section 6 of:
https://fidoalliance.org/specs/fido-v2.0-ps-20170927/fido-client-to-authenticator-protocol-v2.0-ps-20170927.html
"""


def test_integers():
    assert cbor2hex(0) == "00"
    assert cbor2hex(0) == "00"
    assert cbor2hex(23) == "17"
    assert cbor2hex(24) == "1818"
    assert cbor2hex(255) == "18ff"
    assert cbor2hex(256) == "190100"
    assert cbor2hex(65535) == "19ffff"
    assert cbor2hex(65536) == "1a00010000"
    # assert cbor2hex(4294967295) == "1affffffff"
    # assert cbor2hex(4294967296) == "1b0000000100000000"
    assert cbor2hex(-1) == "20"
    assert cbor2hex(-24) == "37"
    assert cbor2hex(-25) == "3818"


def test_key_order():
    _TEST_VECTORS = [
        ("a30100413200613300", {"3": 0, b"2": 0, 1: 0}),
        ("a3190100004000613300", {"3": 0, b"": 0, 256: 0}),
        ("a3413300423232004331313100", {b"22": 0, b"3": 0, b"111": 0}),
        # ("a4000018ff00190100001b000000010000000000", {4294967296: 0, 255: 0, 256: 0, 0: 0}),
        ("a3433030310043303032004330303300",
         {b"001": 0, b"003": 0, b"002": 0}),
        ("a2f400f500", {True: 0, False: 0}),
    ]
    for (data, value) in _TEST_VECTORS:
        try:
            assert cbor2hex(value) == data, data
        except Exception:
            print("ERROR in test_key_order, {:s}".format(data))
            raise


if __name__ == "__main__":
    test_vectors()
    test_integers()
    test_key_order()
