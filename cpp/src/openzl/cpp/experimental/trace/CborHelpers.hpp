// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include <string>
#include <vector>

#include "openzl/cpp/LocalParams.hpp"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_errors.h"

namespace openzl::visualizer {

ZL_Report addIntValue(
        A1C_MapBuilder& builder,
        const char* key,
        size_t val,
        ZL_OperationContext* opCtx);

ZL_Report addFloatValue(
        A1C_MapBuilder& builder,
        const char* key,
        double val,
        ZL_OperationContext* opCtx);

ZL_Report addStringValue(
        A1C_MapBuilder& builder,
        const char* key,
        const char* val,
        ZL_OperationContext* opCtx);

ZL_Report addBooleanValue(
        A1C_MapBuilder& builder,
        const char* key,
        bool val,
        ZL_OperationContext* opCtx);

ZL_Report addStrArray(
        A1C_Arena* a1c_arena,
        A1C_MapBuilder& builder,
        const char* key,
        const std::vector<std::string>& strs,
        ZL_OperationContext* opCtx);

ZL_Report addNumArray(
        A1C_Arena* a1c_arena,
        A1C_MapBuilder& builder,
        const char* key,
        const std::vector<int64_t>& numbers,
        ZL_OperationContext* opCtx);

ZL_Report addBytesArray(
        A1C_Arena* a1c_arena,
        A1C_MapBuilder& builder,
        const char* key,
        const std::vector<uint8_t>& bytes,
        ZL_OperationContext* opCtx);

ZL_Report serializeLocalParams(
        A1C_Arena* a1c_arena,
        A1C_Item* a1c_localParamsParent,
        const LocalParams& lpi,
        ZL_OperationContext* opCtx);
} // namespace openzl::visualizer
