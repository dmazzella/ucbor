# -*- coding: utf-8 -*-
# pylint: disable=import-error
import _cbor

def encode(data):
    r = _cbor.dumps(data)
    return r

def decode(data):
    r = _cbor.loads(data)
    return r

