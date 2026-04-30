// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/experimental/trace/CborHelpers.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_errors.h"

namespace openzl::visualizer {

ZL_Report addIntValue(
        A1C_MapBuilder& builder,
        const char* key,
        size_t val,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    A1C_MAP_TRY_ADD(pair, builder);
    A1C_Item_string_refCStr(&pair->key, key);
    A1C_Item_int64(&pair->val, val);
    return ZL_returnSuccess();
};

ZL_Report addFloatValue(
        A1C_MapBuilder& builder,
        const char* key,
        double val,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    A1C_MAP_TRY_ADD(pair, builder);
    A1C_Item_string_refCStr(&pair->key, key);
    A1C_Item_float64(&pair->val, val);
    return ZL_returnSuccess();
}

ZL_Report addStringValue(
        A1C_MapBuilder& builder,
        const char* key,
        const char* val,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    A1C_MAP_TRY_ADD(pair, builder);
    A1C_Item_string_refCStr(&pair->key, key);
    A1C_Item_string_refCStr(&pair->val, val);
    return ZL_returnSuccess();
}

ZL_Report addBooleanValue(
        A1C_MapBuilder& builder,
        const char* key,
        bool val,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    A1C_MAP_TRY_ADD(pair, builder);
    A1C_Item_string_refCStr(&pair->key, key);
    A1C_Item_boolean(&pair->val, val);
    return ZL_returnSuccess();
}

ZL_Report addBytesArray(
        A1C_Arena* a1c_arena,
        A1C_MapBuilder& builder,
        const char* key,
        const std::vector<uint8_t>& bytes,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    A1C_MAP_TRY_ADD(pair, builder);
    A1C_Item_string_refCStr(&pair->key, key);

    if (!bytes.empty()) {
        bool success = A1C_Item_bytes_copy(
                &pair->val, bytes.data(), bytes.size(), a1c_arena);
        ZL_ERR_IF(!success, allocation, "Failed to copy bytes data.");
    } else {
        A1C_Item_bytes_ref(&pair->val, nullptr, 0);
    }

    return ZL_returnSuccess();
}

ZL_Report addNumArray(
        A1C_Arena* a1c_arena,
        A1C_MapBuilder& builder,
        const char* key,
        const std::vector<int64_t>& numbers,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    A1C_MAP_TRY_ADD(pair, builder);
    A1C_Item_string_refCStr(&pair->key, key);

    A1C_ArrayBuilder arrayBuilder =
            A1C_Item_array_builder(&pair->val, numbers.size(), a1c_arena);
    ZL_ERR_IF_NULL(arrayBuilder.array, allocation);

    for (const int64_t val : numbers) {
        A1C_ARRAY_TRY_ADD(item, arrayBuilder);
        A1C_Item_int64(item, val);
    }
    return ZL_returnSuccess();
}

ZL_Report addStrArray(
        A1C_Arena* a1c_arena,
        A1C_MapBuilder& builder,
        const char* key,
        const std::vector<std::string>& strs,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    A1C_MAP_TRY_ADD(pair, builder);
    A1C_Item_string_refCStr(&pair->key, key);

    A1C_ArrayBuilder arrayBuilder =
            A1C_Item_array_builder(&pair->val, strs.size(), a1c_arena);
    ZL_ERR_IF_NULL(arrayBuilder.array, allocation);

    for (const std::string& str : strs) {
        A1C_ARRAY_TRY_ADD(item, arrayBuilder);
        A1C_Item_string_refCStr(item, str.c_str());
    }
    return ZL_returnSuccess();
}

ZL_Report serializeLocalParams(
        A1C_Arena* a1c_arena,
        A1C_Item* a1c_localParamsParent,
        const LocalParams& lpi,
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);
    A1C_MapBuilder localParamsBuilder =
            A1C_Item_map_builder(a1c_localParamsParent, 3, a1c_arena);
    ZL_ERR_IF_NULL(localParamsBuilder.map, allocation);

    // local params: IntParams
    A1C_MAP_TRY_ADD(a1c_intParams, localParamsBuilder);
    A1C_Item_string_refCStr(&a1c_intParams->key, "intParams");
    A1C_ArrayBuilder intParamsBuilder = A1C_Item_array_builder(
            &a1c_intParams->val, lpi.getIntParams().size(), a1c_arena);
    ZL_ERR_IF_NULL(intParamsBuilder.array, allocation);
    for (const auto& intParam : lpi.getIntParams()) {
        A1C_ARRAY_TRY_ADD(a1c_intParam, intParamsBuilder);
        A1C_MapBuilder currIntParamBuilder =
                A1C_Item_map_builder(a1c_intParam, 2, a1c_arena);
        ZL_ERR_IF_NULL(currIntParamBuilder.map, allocation);
        ZL_ERR_IF_ERR(addIntValue(
                currIntParamBuilder, "paramId", intParam.paramId, opCtx));
        ZL_ERR_IF_ERR(addIntValue(
                currIntParamBuilder, "paramValue", intParam.paramValue, opCtx));
    }

    // local params: CopyParams
    A1C_MAP_TRY_ADD(a1c_copyParams, localParamsBuilder);
    A1C_Item_string_refCStr(&a1c_copyParams->key, "copyParams");
    A1C_ArrayBuilder copyParamsBuilder = A1C_Item_array_builder(
            &a1c_copyParams->val, lpi.getCopyParams().size(), a1c_arena);
    ZL_ERR_IF_NULL(copyParamsBuilder.array, allocation);
    for (const auto& copyParam : lpi.getCopyParams()) {
        A1C_ARRAY_TRY_ADD(a1c_copyParam, copyParamsBuilder);
        A1C_MapBuilder currCopyParamBuilder =
                A1C_Item_map_builder(a1c_copyParam, 3, a1c_arena);
        ZL_ERR_IF_NULL(currCopyParamBuilder.map, allocation);
        ZL_ERR_IF_ERR(addIntValue(
                currCopyParamBuilder, "paramId", copyParam.paramId, opCtx));
        ZL_ERR_IF_ERR(addIntValue(
                currCopyParamBuilder, "paramSize", copyParam.paramSize, opCtx));
        A1C_MAP_TRY_ADD(a1c_copyParam_paramData, currCopyParamBuilder);
        A1C_Item_string_refCStr(&a1c_copyParam_paramData->key, "paramData");
        // copy if the paramPtr is not null, and its size is greater than 0
        if (copyParam.paramPtr != nullptr && copyParam.paramSize > 0) {
            bool success = A1C_Item_bytes_copy(
                    &a1c_copyParam_paramData->val,
                    static_cast<const uint8_t*>(copyParam.paramPtr),
                    copyParam.paramSize,
                    a1c_arena);
            ZL_ERR_IF(
                    !success,
                    allocation,
                    "Failed to copy CopyParam data from pointer.");
        } else {
            A1C_Item_bytes_ref(&a1c_copyParam_paramData->val, nullptr, 0);
        }
    }

    // local params: refParams
    A1C_MAP_TRY_ADD(a1c_refParams, localParamsBuilder);
    A1C_Item_string_refCStr(&a1c_refParams->key, "refParams");
    A1C_ArrayBuilder refParamsBuilder = A1C_Item_array_builder(
            &a1c_refParams->val, lpi.getRefParams().size(), a1c_arena);
    ZL_ERR_IF_NULL(refParamsBuilder.array, allocation);
    for (const auto& refParam : lpi.getRefParams()) {
        A1C_ARRAY_TRY_ADD(a1c_refParam, refParamsBuilder);
        A1C_MapBuilder currRefParamBuilder =
                A1C_Item_map_builder(a1c_refParam, 1, a1c_arena);
        ZL_ERR_IF_NULL(currRefParamBuilder.map, allocation);
        ZL_ERR_IF_ERR(addIntValue(
                currRefParamBuilder, "paramId", refParam.paramId, opCtx));
    }

    return ZL_returnSuccess();
}

} // namespace openzl::visualizer
