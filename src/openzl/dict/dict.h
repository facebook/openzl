// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_DICT_H
#define OPENZL_DICT_DICT_H

struct ZL_Dict_s {
    ZL_DictID dictID;
    ZL_IDType materializingCodec;
    void* dictObj;
}

#endif // OPENZL_DICT_DICT_H
