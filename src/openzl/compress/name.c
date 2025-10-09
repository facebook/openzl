// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/name.h"
#include "openzl/shared/string_view.h"

#define ZL_NAME_STORAGE_SIZE (ZL_NAME_MAX_LEN + 1)

static ZL_Report ZL_validatePrefix(const char* prefix, bool isStandard)
{
    const bool hasStandardPrefix = (strncmp(prefix, "!zl.", 4) == 0);
    if (isStandard) {
        ZL_RET_R_IF_NOT(
                invalidName,
                hasStandardPrefix,
                "Standard name \"%s\" doesn't start with \"!zl.\"",
                prefix);
    } else {
        ZL_RET_R_IF(
                invalidName,
                hasStandardPrefix,
                "User defined anchor name \"%s\" cannot start with the "
                "standard prefix \"!zl.\"",
                prefix);
    }

    size_t idx = 0;
    // Anchors start with !
    if (prefix[idx] == '!') {
        ++idx;
    }
    for (; prefix[idx] != '\0'; ++idx) {
        ZL_RET_R_IF_EQ(
                invalidName,
                prefix[idx],
                '!',
                "Name \"%s\" contains '!', which denotes that a name is an "
                "anchor, and is only allowed in the first byte of the name",
                prefix);
        ZL_RET_R_IF_EQ(
                invalidName,
                prefix[idx],
                '#',
                "Name \"%s\" contains '#', which is not allowed in names",
                prefix);
    }
    ZL_RET_R_IF_GT(
            invalidName,
            idx,
            ZL_PREFIX_MAX_LEN,
            "Name \"%s\" is too long. Names must be no more than %d characters",
            prefix,
            ZL_PREFIX_MAX_LEN);
    return ZL_returnSuccess();
}

ZL_Report ZL_Name_init(
        ZL_Name* name,
        Arena* arena,
        const char* prefix,
        ZL_IDType uniqueID)
{
    if (prefix == NULL) {
        prefix = "";
    }

    ZL_RET_R_IF_ERR(ZL_validatePrefix(prefix, false));
    size_t prefixLen = strlen(prefix);
    ZL_ASSERT_LE(prefixLen, ZL_NAME_MAX_LEN);

    {
        char* prefixStorage = ALLOC_Arena_calloc(arena, ZL_NAME_STORAGE_SIZE);
        ZL_RET_R_IF_NULL(allocation, prefixStorage);
        strcpy(prefixStorage, prefix);
        prefix = prefixStorage;
    }

    name->isAnchor = (prefix[0] == '!');

    if (prefix[0] == '!') {
        name->unique   = StringView_init(prefix + 1, prefixLen - 1);
        name->prefix   = name->unique;
        name->isAnchor = true;
        return ZL_returnSuccess();
    }

    char* unique = ALLOC_Arena_calloc(arena, ZL_NAME_STORAGE_SIZE);
    ZL_RET_R_IF_NULL(allocation, unique);

    const int nameLen =
            snprintf(unique, ZL_NAME_STORAGE_SIZE, "%s#%u", prefix, uniqueID);
    ZL_ASSERT_LT(nameLen, ZL_NAME_STORAGE_SIZE);
    ZL_ASSERT_EQ(nameLen, (int)strlen(unique));
    ZL_RET_R_IF_LT(
            invalidName,
            nameLen,
            0,
            "Name formatting for \"%s\" failed",
            prefix);

    name->unique   = StringView_init(unique, (size_t)nameLen);
    name->prefix   = StringView_init(prefix, prefixLen);
    name->isAnchor = false;

    return ZL_returnSuccess();
}

ZL_Name ZL_Name_wrapStandard(const char* cstr)
{
    ZL_ASSERT_NN(cstr);
    ZL_ASSERT_SUCCESS(
            ZL_validatePrefix(cstr, true),
            "Standard name \"%s\" is invalid",
            cstr);
    ZL_ASSERT_EQ(cstr[0], '!');
    ++cstr;
    const size_t len = strlen(cstr);
    ZL_Name name     = {
            .unique   = StringView_init(cstr, len),
            .prefix   = StringView_init(cstr, len),
            .isAnchor = true,
    };
    return name;
}
