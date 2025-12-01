// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_selector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The int metadata id / key that should be set with the index of the successor
 * to use.
 */
static const int kDirectedSelectorMetadataID = 0;

/**
 * A selector that does what it's told. The selector expects to receive
 * direction as to which successor to select in the form of an integer metadata
 * on the input stream (at kDirectedSelectorMetadataID).
 */
ZL_SelectorDesc buildDirectedSelectorDesc(
        ZL_Type type,
        const ZL_GraphID* successors,
        size_t nbSuccessors);

#ifdef __cplusplus
}
#endif
