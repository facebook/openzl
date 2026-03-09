#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.


import base64
import sys

# pip install cbor2
import cbor2


# This script converts a python object representation to its cbor equivalent.

KEY = object()
VAL = object()


# converts the blob params from b64 str to bytes
def rewrite_func(v, path):
    if isinstance(v, int):
        pass
    elif isinstance(v, float):
        pass
    elif isinstance(v, str):
        if "blobs" in path and path[-1] != KEY:
            v = base64.b64decode(v)
    elif isinstance(v, bytes):
        pass
    else:
        raise TypeError("Unhandled type!")
    return v


simple_types = (int, float, str, bytes)


def rewrite(func, obj, path=None):
    if path is None:
        path = []
    if isinstance(obj, dict):
        res = {}
        for k, v in obj.items():
            path.append(k)
            path.append(KEY)
            k = rewrite(func, k, path)
            path.pop()
            path.pop()
            path.append(k)
            path.append(VAL)
            v = rewrite(func, v, path)
            path.pop()
            path.pop()
            res[k] = v
    elif isinstance(obj, list):
        res = []
        for i, v in enumerate(obj):
            path.append(i)
            v = rewrite(func, v)
            path.pop()
            res.append(v)
    elif any(isinstance(obj, t) for t in simple_types):
        res = func(obj, path)
    else:
        raise TypeError("Unhandled type!")
    return res


def main():
    pyobjstr = sys.stdin.read()
    obj = eval(pyobjstr)
    obj = rewrite(rewrite_func, obj)
    cbor = cbor2.dumps(obj)
    # print(obj)
    print("std::string_view{")
    w = 16
    for i in range(0, len(cbor), w):
        substr = cbor[i : i + w]
        term = "" if i + w <= len(cbor) else ","
        print('  "' + "".join("\\x%02x" % (c,) for c in substr) + '"' + term)
    print("  " + repr(len(cbor)) + "};")
    return 0


if __name__ == "__main__":
    main()
