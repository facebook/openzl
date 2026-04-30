// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "./a1cbor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "./utils.h"

////////////////////////////////////////
// Constants
////////////////////////////////////////

static char* A1C_gEmptyString = "";

////////////////////////////////////////
// Portability
////////////////////////////////////////

#if defined(__has_builtin) && !defined(A1C_TEST_FALLBACK)
#    define A1C_HAS_BUILTIN(x) __has_builtin(x)
#else
#    define A1C_HAS_BUILTIN(x) 0
#endif

/// @returns true on overflow.
static bool A1C_NODISCARD A1C_overflowAdd(size_t x, size_t y, size_t* result)
{
#if A1C_HAS_BUILTIN(__builtin_add_overflow)
    return __builtin_add_overflow(x, y, result);
#else
    *result = x + y;
    if (x > SIZE_MAX - y) {
        return true;
    } else {
        return false;
    }
#endif
}

static bool A1C_NODISCARD A1C_overflowMul(size_t x, size_t y, size_t* result)
{
#if A1C_HAS_BUILTIN(__builtin_mul_overflow)
    return __builtin_mul_overflow(x, y, result);
#else
    *result = x * y;
    if (y > 0 && x > SIZE_MAX / y) {
        return true;
    } else {
        return false;
    }
#endif
}

#if !A1C_HAS_BUILTIN(__builtin_bswap16) || !A1C_HAS_BUILTIN(__builtin_bswap32) \
        || !A1C_HAS_BUILTIN(__builtin_bswap64)
static void A1C_byteswap_fallback(void* value, size_t size)
{
    assert(size == 2 || size == 4 || size == 8);
    uint8_t* first = (uint8_t*)value;
    uint8_t* last  = first + size - 1;
    while (first < last) {
        uint8_t tmp = *first;
        *first      = *last;
        *last       = tmp;
        first++;
        last--;
    }
}
#endif

static uint16_t A1C_byteswap16(uint16_t value)
{
#if A1C_HAS_BUILTIN(__builtin_bswap16)
    return __builtin_bswap16(value);
#else
    A1C_byteswap_fallback(&value, sizeof(value));
    return value;
#endif
}

static uint32_t A1C_byteswap32(uint32_t value)
{
#if A1C_HAS_BUILTIN(__builtin_bswap32)
    return __builtin_bswap32(value);
#else
    A1C_byteswap_fallback(&value, sizeof(value));
    return value;
#endif
}

static uint64_t A1C_byteswap64(uint64_t value)
{
#if A1C_HAS_BUILTIN(__builtin_bswap64)
    return __builtin_bswap64(value);
#else
    A1C_byteswap_fallback(&value, sizeof(value));
    return value;
#endif
}

static bool A1C_isLittleEndian(void)
{
    const union {
        uint32_t u;
        uint8_t c[4];
    } one = { 1 };
    return one.c[0];
}

static uint16_t A1C_bigEndian16(uint16_t value)
{
    if (A1C_isLittleEndian()) {
        return A1C_byteswap16(value);
    } else {
        return value;
    }
}

static uint32_t A1C_bigEndian32(uint32_t value)
{
    if (A1C_isLittleEndian()) {
        return A1C_byteswap32(value);
    } else {
        return value;
    }
}

static uint64_t A1C_bigEndian64(uint64_t value)
{
    if (A1C_isLittleEndian()) {
        return A1C_byteswap64(value);
    } else {
        return value;
    }
}

////////////////////////////////////////
// Utilities
////////////////////////////////////////

#define A1C_RET_IF_ERR(ret) \
    do {                    \
        if (!(ret)) {       \
            return 0;       \
        }                   \
    } while (0)

const char A1C_kBase64Map[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

static ZL_MAYBE_UNUSED_FUNCTION size_t A1C_base64EncodedSize(size_t srcSize)
{
    size_t encodedSize = (srcSize / 3) * 4;
    if (srcSize % 3 != 0) {
        encodedSize += 4;
    }
    return encodedSize;
}

static size_t A1C_base64Encode(char* dst, const uint8_t* src, size_t srcSize)
{
    const char* const dstBegin  = dst;
    const uint8_t* const srcEnd = src + srcSize;
    for (; (srcEnd - src) >= 3; src += 3, dst += 4) {
        dst[0] = A1C_kBase64Map[src[0] >> 2];
        dst[1] = A1C_kBase64Map[((src[0] & 0x03) << 4) + ((src[1] >> 4))];
        dst[2] = A1C_kBase64Map[((src[1] & 0x0f) << 2) + ((src[2] >> 6))];
        dst[3] = A1C_kBase64Map[src[2] & 0x3f];
    }

    if (src < srcEnd) {
        assert(src + 1 == srcEnd || src + 2 == srcEnd);
        dst[0] = A1C_kBase64Map[src[0] >> 2];
        if (src + 1 == srcEnd) {
            dst[1] = A1C_kBase64Map[(src[0] & 0x03) << 4];
            dst[2] = '=';
        } else {
            dst[1] = A1C_kBase64Map[((src[0] & 0x03) << 4) + (src[1] >> 4)];
            dst[2] = A1C_kBase64Map[(src[1] & 0x0f) << 2];
        }
        dst[3] = '=';
        dst += 4;
    }
    const size_t dstSize = (size_t)(dst - dstBegin);
    assert(dstSize == A1C_base64EncodedSize(srcSize));
    return dstSize;
}

////////////////////////////////////////
// Errors
////////////////////////////////////////

const char* A1C_ErrorType_getString(A1C_ErrorType type)
{
    switch (type) {
        case A1C_ErrorType_ok:
            return "ok";
        case A1C_ErrorType_badAlloc:
            return "badAlloc";
        case A1C_ErrorType_truncated:
            return "truncated";
        case A1C_ErrorType_invalidItemHeader:
            return "invalidItemHeader";
        case A1C_ErrorType_largeIntegersUnsupported:
            return "largeIntegersUnsupported";
        case A1C_ErrorType_integerOverflow:
            return "integerOverflow";
        case A1C_ErrorType_invalidChunkedString:
            return "invalidChunkedString";
        case A1C_ErrorType_maxDepthExceeded:
            return "maxDepthExceeded";
        case A1C_ErrorType_invalidSimpleEncoding:
            return "invalidSimpleEncoding";
        case A1C_ErrorType_breakNotAllowed:
            return "breakNotAllowed";
        case A1C_ErrorType_writeFailed:
            return "writeFailed";
        case A1C_ErrorType_invalidSimpleValue:
            return "invalidSimpleValue";
        case A1C_ErrorType_formatError:
            return "formatError";
        case A1C_ErrorType_trailingData:
            return "trailingData";
        case A1C_ErrorType_jsonUTF8Unsupported:
            return "jsonUTF8Unsupported";
        default:
            return "unknown";
    }
}

////////////////////////////////////////
// Arena
////////////////////////////////////////

static void* A1C_Arena_calloc(A1C_Arena* arena, size_t count, size_t size)
{
    size_t bytes;
    if (A1C_overflowMul(count, size, &bytes)) {
        return NULL;
    }
    if (bytes == 0) {
        return (void*)A1C_gEmptyString;
    }
    return arena->calloc(arena->opaque, bytes);
}

static void* A1C_LimitedArena_calloc(void* opaque, size_t bytes)
{
    A1C_LimitedArena* arena = (A1C_LimitedArena*)opaque;
    if (arena == NULL) {
        return NULL;
    }
    assert(arena->limitBytes == 0
           || arena->allocatedBytes <= arena->limitBytes);

    size_t newBytes;
    if (A1C_overflowAdd(arena->allocatedBytes, bytes, &newBytes)) {
        return NULL;
    }
    if (arena->limitBytes > 0 && newBytes > arena->limitBytes) {
        return NULL;
    }
    void* result =
            arena->backingArena.calloc(arena->backingArena.opaque, bytes);
    if (result != NULL) {
        arena->allocatedBytes = newBytes;
    }
    return result;
}

A1C_LimitedArena A1C_LimitedArena_init(A1C_Arena arena, size_t limitBytes)
{
    A1C_LimitedArena limitedArena = {
        .backingArena   = arena,
        .allocatedBytes = 0,
        .limitBytes     = limitBytes,
    };
    return limitedArena;
}

A1C_Arena A1C_LimitedArena_arena(A1C_LimitedArena* limitedArena)
{
    A1C_Arena arena = {
        .calloc = A1C_LimitedArena_calloc,
        .opaque = limitedArena,
    };

    return arena;
}

void A1C_LimitedArena_reset(A1C_LimitedArena* limitedArena)
{
    assert(limitedArena->limitBytes == 0
           || limitedArena->allocatedBytes <= limitedArena->limitBytes);
    limitedArena->allocatedBytes = 0;
}

////////////////////////////////////////
// Item Helpers
////////////////////////////////////////

bool A1C_Item_eq(const A1C_Item* a, const A1C_Item* b)
{
    if (a->type != b->type) {
        return false;
    }

    switch (a->type) {
        case A1C_ItemType_int64:
            return a->int64 == b->int64;
        case A1C_ItemType_float16:
            return a->float16 == b->float16;
        case A1C_ItemType_float32: {
            uint32_t aBits, bBits;
            memcpy(&aBits, &a->float64, sizeof(aBits));
            memcpy(&bBits, &b->float64, sizeof(bBits));
            return aBits == bBits;
        }
        case A1C_ItemType_float64: {
            uint64_t aBits, bBits;
            memcpy(&aBits, &a->float64, sizeof(aBits));
            memcpy(&bBits, &b->float64, sizeof(bBits));
            return aBits == bBits;
        }
        case A1C_ItemType_boolean:
        case A1C_ItemType_null:
        case A1C_ItemType_undefined:
        case A1C_ItemType_simple:
            return a->simple == b->simple;
        case A1C_ItemType_bytes:
            if (a->bytes.size != b->bytes.size) {
                return false;
            }
            if (a->bytes.size == 0) {
                return true;
            }
            return memcmp(a->bytes.data, b->bytes.data, a->bytes.size) == 0;
        case A1C_ItemType_string:
            if (a->string.size != b->string.size) {
                return false;
            }
            if (a->string.size == 0) {
                return true;
            }
            return memcmp(a->string.data, b->string.data, a->string.size) == 0;
        case A1C_ItemType_array:
            if (a->array.size != b->array.size) {
                return false;
            }
            for (size_t i = 0; i < a->array.size; i++) {
                if (!A1C_Item_eq(&a->array.items[i], &b->array.items[i])) {
                    return false;
                }
            }
            return true;
        case A1C_ItemType_map:
            if (a->map.size != b->map.size) {
                return false;
            }
            for (size_t i = 0; i < a->map.size; i++) {
                if (!A1C_Item_eq(&a->map.items[i].key, &b->map.items[i].key)) {
                    return false;
                }
                if (!A1C_Item_eq(&a->map.items[i].val, &b->map.items[i].val)) {
                    return false;
                }
            }
            return true;
        case A1C_ItemType_tag:
            return a->tag.tag == b->tag.tag
                    && A1C_Item_eq(a->tag.item, b->tag.item);
        default:
            assert(false);
            return false;
    }
}

A1C_Item* A1C_Map_get(const A1C_Map* map, const A1C_Item* key)
{
    for (size_t i = 0; i < map->size; i++) {
        if (A1C_Item_eq(&map->items[i].key, key)) {
            return &map->items[i].val;
        }
    }
    return NULL;
}

A1C_Item* A1C_Map_get_cstr(const A1C_Map* map, const char* key)
{
    A1C_Item keyItem;
    A1C_Item_string_ref(&keyItem, key, strlen(key));
    return A1C_Map_get(map, &keyItem);
}

A1C_Item* A1C_Map_get_int(const A1C_Map* map, A1C_Int64 key)
{
    A1C_Item keyItem;
    A1C_Item_int64(&keyItem, key);
    return A1C_Map_get(map, &keyItem);
}

A1C_Item* A1C_Array_get(const A1C_Array* array, size_t index)
{
    if (index >= array->size) {
        return NULL;
    }
    return &array->items[index];
}

////////////////////////////////////////
// Creation
////////////////////////////////////////

A1C_Item* A1C_Item_root(A1C_Arena* arena)
{
    A1C_Item* item = A1C_Arena_calloc(arena, 1, sizeof(A1C_Item));
    return item;
}

void A1C_Item_int64(A1C_Item* item, A1C_Int64 val)
{
    item->type  = A1C_ItemType_int64;
    item->int64 = val;
}

void A1C_Item_float16(A1C_Item* item, A1C_Float16 val)
{
    item->type    = A1C_ItemType_float16;
    item->float16 = val;
}

void A1C_Item_float32(A1C_Item* item, A1C_Float32 val)
{
    item->type    = A1C_ItemType_float32;
    item->float32 = val;
}

void A1C_Item_float64(A1C_Item* item, A1C_Float64 val)
{
    item->type    = A1C_ItemType_float64;
    item->float64 = val;
}

void A1C_Item_boolean(A1C_Item* item, bool val)
{
    item->type    = A1C_ItemType_boolean;
    item->boolean = val;
}

void A1C_Item_null(A1C_Item* item)
{
    item->type = A1C_ItemType_null;
}

void A1C_Item_undefined(A1C_Item* item)
{
    item->type = A1C_ItemType_undefined;
}

static void A1C_Item_simple(A1C_Item* item, A1C_Simple val)
{
    item->type   = A1C_ItemType_simple;
    item->simple = val;
}

A1C_Item* A1C_Item_tag(A1C_Item* item, uint64_t tag, A1C_Arena* arena)
{
    A1C_Item* child = A1C_Arena_calloc(arena, 1, sizeof(A1C_Item));
    if (child == NULL) {
        return NULL;
    }
    child->parent = item;

    item->type     = A1C_ItemType_tag;
    item->tag.tag  = tag;
    item->tag.item = child;

    return child;
}

uint8_t* A1C_Item_bytes(A1C_Item* item, size_t size, A1C_Arena* arena)
{
    uint8_t* data = A1C_Arena_calloc(arena, size, 1);
    if (data == NULL) {
        return NULL;
    }

    A1C_Item_bytes_ref(item, data, size);

    return data;
}

bool A1C_NODISCARD A1C_Item_bytes_copy(
        A1C_Item* item,
        const uint8_t* data,
        size_t size,
        A1C_Arena* arena)
{
    uint8_t* dst = A1C_Item_bytes(item, size, arena);
    if (dst == NULL) {
        return false;
    }
    if (size > 0) {
        memcpy(dst, data, size);
    }
    return true;
}

void A1C_Item_bytes_ref(A1C_Item* item, const uint8_t* data, size_t size)
{
    item->type       = A1C_ItemType_bytes;
    item->bytes.data = data;
    item->bytes.size = size;
}

char* A1C_Item_string(A1C_Item* item, size_t size, A1C_Arena* arena)
{
    char* data = A1C_Arena_calloc(arena, size, 1);
    if (data == NULL) {
        return NULL;
    }

    A1C_Item_string_ref(item, data, size);

    return data;
}

bool A1C_NODISCARD A1C_Item_string_copy(
        A1C_Item* item,
        const char* data,
        size_t size,
        A1C_Arena* arena)
{
    char* dst = A1C_Item_string(item, size, arena);
    if (dst == NULL) {
        return false;
    }
    if (size > 0) {
        memcpy(dst, data, size);
    }
    return true;
}
bool A1C_NODISCARD
A1C_Item_string_cstr(A1C_Item* item, const char* data, A1C_Arena* arena)
{
    return A1C_Item_string_copy(item, data, strlen(data), arena);
}

void A1C_Item_string_ref(A1C_Item* item, const char* data, size_t size)
{
    item->type        = A1C_ItemType_string;
    item->string.data = data;
    item->string.size = size;
}

void A1C_Item_string_refCStr(A1C_Item* item, const char* data)
{
    A1C_Item_string_ref(item, data, strlen(data));
}

A1C_Pair* A1C_Item_map(A1C_Item* item, size_t size, A1C_Arena* arena)
{
    A1C_Pair* items = A1C_Arena_calloc(arena, size, sizeof(A1C_Pair));
    if (items == NULL) {
        return NULL;
    }

    item->type      = A1C_ItemType_map;
    item->map.items = items;
    item->map.size  = size;

    for (size_t i = 0; i < size; ++i) {
        items[i].key.parent = item;
        items[i].val.parent = item;
    }

    return items;
}

A1C_MapBuilder A1C_Item_map_builder(
        A1C_Item* const item,
        size_t maxSize,
        A1C_Arena* const arena)
{
    A1C_Pair* const pairs = A1C_Item_map(item, maxSize, arena);
    if (pairs == NULL) {
        return (A1C_MapBuilder){
            .map     = NULL,
            .pairs   = NULL,
            .maxSize = 0,
        };
    }
    item->map.size = 0;
    return (A1C_MapBuilder){
        .map     = &item->map,
        .pairs   = pairs,
        .maxSize = maxSize,
    };
}

A1C_Pair* A1C_MapBuilder_add(const A1C_MapBuilder builder)
{
    A1C_Map* const map = builder.map;
    if (map == NULL) {
        return NULL;
    }
    if (map->size >= builder.maxSize) {
        return NULL;
    }
    A1C_Pair* const pair = &builder.pairs[map->size];
    map->size++;
    return pair;
}

A1C_Item* A1C_Item_array(A1C_Item* item, size_t size, A1C_Arena* arena)
{
    A1C_Item* items = A1C_Arena_calloc(arena, size, sizeof(A1C_Item));
    if (items == NULL) {
        return NULL;
    }

    item->type        = A1C_ItemType_array;
    item->array.items = items;
    item->array.size  = size;

    for (size_t i = 0; i < size; ++i) {
        items[i].parent = item;
    }

    return items;
}

A1C_ArrayBuilder A1C_Item_array_builder(
        A1C_Item* const item,
        size_t maxSize,
        A1C_Arena* const arena)
{
    A1C_Item* const items = A1C_Item_array(item, maxSize, arena);
    if (items == NULL) {
        return (A1C_ArrayBuilder){
            .array   = NULL,
            .items   = NULL,
            .maxSize = 0,
        };
    }
    item->array.size = 0;
    return (A1C_ArrayBuilder){
        .array   = &item->array,
        .items   = items,
        .maxSize = maxSize,
    };
}

A1C_Item* A1C_ArrayBuilder_add(const A1C_ArrayBuilder builder)
{
    A1C_Array* const array = builder.array;
    if (array == NULL) {
        return NULL;
    }
    if (array->size >= builder.maxSize) {
        return NULL;
    }
    A1C_Item* const item = &builder.items[array->size];
    array->size++;
    return item;
}

A1C_Item* A1C_Item_deepcopy(const A1C_Item* src, A1C_Arena* arena)
{
    if (src == NULL) {
        return NULL;
    }
    A1C_Item* dst = A1C_Item_root(arena);
    if (dst == NULL) {
        return NULL;
    }

    switch (src->type) {
        case A1C_ItemType_int64:
            A1C_Item_int64(dst, src->int64);
            break;
        case A1C_ItemType_float16:
            A1C_Item_float16(dst, src->float16);
            break;
        case A1C_ItemType_float32:
            A1C_Item_float32(dst, src->float32);
            break;
        case A1C_ItemType_float64:
            A1C_Item_float64(dst, src->float64);
            break;
        case A1C_ItemType_boolean:
            A1C_Item_boolean(dst, src->boolean);
            break;
        case A1C_ItemType_null:
            A1C_Item_null(dst);
            break;
        case A1C_ItemType_undefined:
            A1C_Item_undefined(dst);
            break;
        case A1C_ItemType_simple:
            A1C_Item_simple(dst, src->simple);
            break;
        case A1C_ItemType_bytes:
            if (!A1C_Item_bytes_copy(
                        dst, src->bytes.data, src->bytes.size, arena)) {
                return NULL;
            }
            break;
        case A1C_ItemType_string:
            if (!A1C_Item_string_copy(
                        dst, src->string.data, src->string.size, arena)) {
                return NULL;
            }
            break;
        case A1C_ItemType_array: {
            A1C_Item* const items = A1C_Item_array(dst, src->array.size, arena);
            if (items == NULL) {
                return NULL;
            }
            for (size_t i = 0; i < src->array.size; i++) {
                items[i] = *A1C_Item_deepcopy(&src->array.items[i], arena);
            }
            break;
        }
        case A1C_ItemType_map: {
            A1C_Pair* const pairs = A1C_Item_map(dst, src->map.size, arena);
            if (pairs == NULL) {
                return NULL;
            }
            for (size_t i = 0; i < src->map.size; i++) {
                pairs[i].key =
                        *A1C_Item_deepcopy(&src->map.items[i].key, arena);
                pairs[i].val =
                        *A1C_Item_deepcopy(&src->map.items[i].val, arena);
            }
            break;
        }
        case A1C_ItemType_tag: {
            A1C_Item* item = A1C_Item_tag(dst, src->tag.tag, arena);
            if (item == NULL) {
                return NULL;
            }
            *item = *A1C_Item_deepcopy(src->tag.item, arena);
            break;
        }
        default:
            assert(false);
            return NULL;
    }

    return dst;
}

////////////////////////////////////////
// Shared Coder Helpers
////////////////////////////////////////

typedef enum {
    A1C_MajorType_uint    = 0,
    A1C_MajorType_int     = 1,
    A1C_MajorType_bytes   = 2,
    A1C_MajorType_string  = 3,
    A1C_MajorType_array   = 4,
    A1C_MajorType_map     = 5,
    A1C_MajorType_tag     = 6,
    A1C_MajorType_special = 7,
} A1C_MajorType;

typedef struct {
    uint8_t header;
} A1C_ItemHeader;

static A1C_ItemHeader A1C_ItemHeader_make(
        A1C_MajorType type,
        uint8_t shortCount)
{
    A1C_ItemHeader header;
    header.header = (uint8_t)(type << 5) | shortCount;
    return header;
}

static A1C_MajorType A1C_ItemHeader_majorType(A1C_ItemHeader header)
{
    return (A1C_MajorType)(header.header >> 5);
}

static uint8_t A1C_ItemHeader_shortCount(A1C_ItemHeader header)
{
    return header.header & 0x1F;
}

static bool A1C_ItemHeader_isBreak(A1C_ItemHeader header)
{
    return header.header == 0xFF;
}

static bool A1C_ItemHeader_isIndefinite(A1C_ItemHeader header)
{
    return A1C_ItemHeader_shortCount(header) == 31;
}

static bool A1C_ItemHeader_isLegal(A1C_ItemHeader header)
{
    const A1C_MajorType majorType = A1C_ItemHeader_majorType(header);
    const uint8_t shortCount      = A1C_ItemHeader_shortCount(header);
    if (shortCount >= 28) {
        if (shortCount < 31) {
            return false;
        }
        assert(shortCount == 31);
        return !(
                majorType == A1C_MajorType_uint
                || majorType == A1C_MajorType_int
                || majorType == A1C_MajorType_tag);
    }
    return true;
}

////////////////////////////////////////
// Decoder
////////////////////////////////////////

void A1C_Decoder_init(
        A1C_Decoder* decoder,
        A1C_Arena arena,
        A1C_DecoderConfig config)
{
    memset(decoder, 0, sizeof(A1C_Decoder));
    assert(arena.calloc != NULL);
    decoder->limitedArena = A1C_LimitedArena_init(arena, config.limitBytes);
    decoder->arena        = A1C_LimitedArena_arena(&decoder->limitedArena);
    if (config.maxDepth == 0) {
        decoder->maxDepth = A1C_MAX_DEPTH_DEFAULT;
    } else {
        decoder->maxDepth = config.maxDepth;
    }
    decoder->referenceSource     = config.referenceSource;
    decoder->rejectUnknownSimple = config.rejectUnknownSimple;
}

A1C_Error A1C_Decoder_getError(const A1C_Decoder* decoder)
{
    return decoder->error;
}

static void
A1C_Decoder_reset(A1C_Decoder* decoder, const uint8_t* start, size_t size)
{
    memset(&decoder->error, 0, sizeof(A1C_Error));
    decoder->start  = start;
    decoder->ptr    = start;
    decoder->end    = start + size;
    decoder->parent = NULL;
    decoder->depth  = 0;
    A1C_LimitedArena_reset(&decoder->limitedArena);
}

static A1C_Item* A1C_NODISCARD A1C_Decoder_decodeOne(A1C_Decoder* decoder);
static bool A1C_NODISCARD
A1C_Decoder_decodeOneInto(A1C_Decoder* decoder, A1C_Item* item);

static bool A1C_NODISCARD A1C_Decoder_errorImpl(
        A1C_Decoder* decoder,
        A1C_ErrorType errorType,
        const char* file,
        int line)
{
    assert(decoder->ptr >= decoder->start);
    assert(decoder->ptr <= decoder->end);
    decoder->error.type   = errorType;
    decoder->error.srcPos = (size_t)(decoder->ptr - decoder->start);
    decoder->error.depth  = decoder->depth;
    decoder->error.item   = decoder->parent;
    decoder->error.file   = file;
    decoder->error.line   = line;
    return false;
}

#define A1C_Decoder_error(decoder, errorType) \
    A1C_Decoder_errorImpl((decoder), (errorType), __FILE__, __LINE__)

static size_t A1C_Decoder_remaining(const A1C_Decoder* decoder)
{
    assert(decoder->ptr <= decoder->end);
    return (size_t)(decoder->end - decoder->ptr);
}

static bool A1C_NODISCARD
A1C_Decoder_peek(A1C_Decoder* decoder, void* out, size_t size)
{
    assert(out != NULL);
    assert(decoder->ptr <= decoder->end);
    if ((size_t)(decoder->end - decoder->ptr) < size) {
        return A1C_Decoder_error(decoder, A1C_ErrorType_truncated);
    }
    if (size > 0) {
        memcpy(out, decoder->ptr, size);
    }
    return true;
}

static bool A1C_NODISCARD A1C_Decoder_skip(A1C_Decoder* decoder, size_t size)
{
    assert(decoder->ptr <= decoder->end);
    if ((size_t)(decoder->end - decoder->ptr) < size) {
        return A1C_Decoder_error(decoder, A1C_ErrorType_truncated);
    }
    if (size > 0) {
        decoder->ptr += size;
    }
    return true;
}

static bool A1C_NODISCARD
A1C_Decoder_read(A1C_Decoder* decoder, void* out, size_t size)
{
    A1C_RET_IF_ERR(A1C_Decoder_peek(decoder, out, size));
    return A1C_Decoder_skip(decoder, size);
}

static bool A1C_NODISCARD A1C_Decoder_readCount(
        A1C_Decoder* decoder,
        A1C_ItemHeader header,
        uint64_t* out)
{
    assert(A1C_ItemHeader_isLegal(header));
    const uint8_t shortCount = A1C_ItemHeader_shortCount(header);
    if (shortCount < 24 || shortCount == 31) {
        *out = shortCount;
        return true;
    } else if (shortCount == 24) {
        uint8_t value;
        A1C_RET_IF_ERR(A1C_Decoder_read(decoder, &value, sizeof(value)));
        *out = value;
    } else if (shortCount == 25) {
        uint16_t value;
        A1C_RET_IF_ERR(A1C_Decoder_read(decoder, &value, sizeof(value)));
        *out = A1C_bigEndian16(value);
    } else if (shortCount == 26) {
        uint32_t value;
        A1C_RET_IF_ERR(A1C_Decoder_read(decoder, &value, sizeof(value)));
        *out = A1C_bigEndian32(value);
    } else if (shortCount == 27) {
        uint64_t value;
        A1C_RET_IF_ERR(A1C_Decoder_read(decoder, &value, sizeof(value)));
        *out = A1C_bigEndian64(value);
    } else {
        // Impossible, we've already validated the header
        assert(false);
        *out = 0;
    }
    return true;
}

static bool A1C_NODISCARD
A1C_Decoder_readSize(A1C_Decoder* decoder, A1C_ItemHeader header, size_t* out)
{
    uint64_t tmp;
    A1C_RET_IF_ERR(A1C_Decoder_readCount(decoder, header, &tmp));
    if (tmp > SIZE_MAX) {
        return A1C_Decoder_error(decoder, A1C_ErrorType_integerOverflow);
    }
    *(out) = (size_t)tmp;
    return true;
}

static bool A1C_NODISCARD A1C_Decoder_decodeUInt(
        A1C_Decoder* decoder,
        A1C_ItemHeader header,
        A1C_Item* item)
{
    uint64_t pos;
    A1C_RET_IF_ERR(A1C_Decoder_readCount(decoder, header, &pos));
    if (pos > (uint64_t)INT64_MAX) {
        return A1C_Decoder_error(
                decoder, A1C_ErrorType_largeIntegersUnsupported);
    }
    A1C_Item_int64(item, (int64_t)pos);
    return true;
}

static bool A1C_NODISCARD A1C_Decoder_decodeInt(
        A1C_Decoder* decoder,
        A1C_ItemHeader header,
        A1C_Item* item)
{
    uint64_t neg;
    A1C_RET_IF_ERR(A1C_Decoder_readCount(decoder, header, &neg));
    if (neg >= ((uint64_t)1 << 63)) {
        return A1C_Decoder_error(
                decoder, A1C_ErrorType_largeIntegersUnsupported);
    }
    A1C_Item_int64(item, (int64_t)~neg);
    return true;
}

static bool A1C_NODISCARD A1C_Decoder_decodeDataDefinite(
        A1C_Decoder* decoder,
        A1C_ItemHeader header,
        A1C_Item* item,
        bool referenceSource)
{
    size_t size;
    A1C_RET_IF_ERR(A1C_Decoder_readSize(decoder, header, &size));
    if (A1C_Decoder_remaining(decoder) < size) {
        // Check before allocating to avoid allocating huge amounts of memory
        return A1C_Decoder_error(decoder, A1C_ErrorType_truncated);
    }
    const uint8_t* data;
    if (referenceSource) {
        data = decoder->ptr;
        A1C_RET_IF_ERR(A1C_Decoder_skip(decoder, size));
    } else {
        uint8_t* buf = A1C_Arena_calloc(&decoder->arena, size, 1);
        if (buf == NULL) {
            return A1C_Decoder_error(decoder, A1C_ErrorType_badAlloc);
        }
        A1C_RET_IF_ERR(A1C_Decoder_read(decoder, buf, size));
        data = buf;
    }
    if (A1C_ItemHeader_majorType(header) == A1C_MajorType_bytes) {
        A1C_Item_bytes_ref(item, data, size);
    } else {
        A1C_Item_string_ref(item, (const char*)data, size);
    }
    return true;
}

static bool A1C_NODISCARD A1C_Decoder_decodeData(
        A1C_Decoder* decoder,
        A1C_ItemHeader header,
        A1C_Item* item)
{
    if (!A1C_ItemHeader_isIndefinite(header)) {
        return A1C_Decoder_decodeDataDefinite(
                decoder, header, item, decoder->referenceSource);
    }

    const A1C_MajorType majorType = A1C_ItemHeader_majorType(header);
    size_t totalSize              = 0;
    A1C_Item* previous            = NULL;
    for (;;) {
        A1C_ItemHeader childHeader;
        A1C_RET_IF_ERR(
                A1C_Decoder_read(decoder, &childHeader, sizeof(childHeader)));
        if (!A1C_ItemHeader_isLegal(childHeader)) {
            return A1C_Decoder_error(decoder, A1C_ErrorType_invalidItemHeader);
        }
        if (A1C_ItemHeader_isBreak(childHeader)) {
            break;
        }

        if (A1C_ItemHeader_majorType(childHeader) != majorType
            || A1C_ItemHeader_isIndefinite(childHeader)) {
            return A1C_Decoder_error(
                    decoder, A1C_ErrorType_invalidChunkedString);
        }
        A1C_Item* child =
                A1C_Arena_calloc(&decoder->arena, 1, sizeof(A1C_Item));
        if (child == NULL) {
            return A1C_Decoder_error(decoder, A1C_ErrorType_badAlloc);
        }
        A1C_RET_IF_ERR(A1C_Decoder_decodeDataDefinite(
                decoder, childHeader, child, true));
        const size_t size = majorType == A1C_MajorType_bytes
                ? child->bytes.size
                : child->string.size;
        if (A1C_overflowAdd(totalSize, size, &totalSize)) {
            return A1C_Decoder_error(decoder, A1C_ErrorType_integerOverflow);
        }
        child->parent = previous;
        previous      = child;
    }
    uint8_t* data = A1C_Arena_calloc(&decoder->arena, totalSize, 1);
    if (data == NULL) {
        return A1C_Decoder_error(decoder, A1C_ErrorType_badAlloc);
    }
    uint8_t* dataEnd = data + totalSize;
    while (previous != NULL) {
        const uint8_t* chunkPtr = majorType == A1C_MajorType_bytes
                ? previous->bytes.data
                : (const uint8_t*)previous->string.data;
        const size_t chunkSize  = majorType == A1C_MajorType_bytes
                 ? previous->bytes.size
                 : previous->string.size;
        if (chunkSize > 0) {
            dataEnd -= chunkSize;
            assert(dataEnd >= data);
            memcpy(dataEnd, chunkPtr, chunkSize);
        }
        previous = previous->parent;
    }
    assert(dataEnd == data);
    if (majorType == A1C_MajorType_bytes) {
        A1C_Item_bytes_ref(item, data, totalSize);
    } else {
        A1C_Item_string_ref(item, (const char*)data, totalSize);
    }
    return true;
}

static bool A1C_NODISCARD A1C_Decoder_decodeArray(
        A1C_Decoder* decoder,
        A1C_ItemHeader header,
        A1C_Item* item)
{
    size_t size;
    A1C_RET_IF_ERR(A1C_Decoder_readSize(decoder, header, &size));
    if (A1C_ItemHeader_isIndefinite(header)) {
        size               = 0;
        A1C_Item* previous = NULL;
        for (;;) {
            A1C_ItemHeader childHeader;
            A1C_RET_IF_ERR(A1C_Decoder_peek(
                    decoder, &childHeader, sizeof(childHeader)));
            if (A1C_ItemHeader_isBreak(childHeader)) {
                A1C_RET_IF_ERR(A1C_Decoder_skip(decoder, sizeof(childHeader)));
                break;
            }
            A1C_Item* child = A1C_Decoder_decodeOne(decoder);
            if (child == NULL) {
                return false;
            }
            child->parent = previous;
            previous      = child;
            ++size;
        }
        A1C_Item* array = A1C_Item_array(item, size, &decoder->arena);
        if (array == NULL) {
            return A1C_Decoder_error(decoder, A1C_ErrorType_badAlloc);
        }
        while (previous != NULL) {
            const A1C_Item* child = previous;
            previous              = previous->parent;

            --size;
            array[size]        = *child;
            array[size].parent = item;
        }
        assert(size == 0);
    } else {
        if (A1C_Decoder_remaining(decoder) < size) {
            // Each item must be at least one byte
            // Check remaining before allocation to avoid huge allocations.
            return A1C_Decoder_error(decoder, A1C_ErrorType_truncated);
        }
        A1C_Item* array = A1C_Item_array(item, size, &decoder->arena);
        if (array == NULL) {
            return A1C_Decoder_error(decoder, A1C_ErrorType_badAlloc);
        }
        for (size_t i = 0; i < size; i++) {
            A1C_RET_IF_ERR(A1C_Decoder_decodeOneInto(decoder, array + i));
            array[i].parent = item;
        }
    }
    return true;
}

static bool A1C_NODISCARD A1C_Decoder_decodeMap(
        A1C_Decoder* decoder,
        A1C_ItemHeader header,
        A1C_Item* item)
{
    size_t size;
    A1C_RET_IF_ERR(A1C_Decoder_readSize(decoder, header, &size));
    if (A1C_ItemHeader_isIndefinite(header)) {
        size              = 0;
        A1C_Item* prevKey = NULL;
        A1C_Item* prevVal = NULL;
        for (;;) {
            A1C_ItemHeader keyHeader;
            A1C_RET_IF_ERR(
                    A1C_Decoder_peek(decoder, &keyHeader, sizeof(keyHeader)));
            if (A1C_ItemHeader_isBreak(keyHeader)) {
                A1C_RET_IF_ERR(A1C_Decoder_skip(decoder, sizeof(keyHeader)));
                break;
            }
            A1C_Item* key = A1C_Decoder_decodeOne(decoder);
            if (key == NULL) {
                return false;
            }
            A1C_Item* val = A1C_Decoder_decodeOne(decoder);
            if (val == NULL) {
                return false;
            }
            key->parent = prevKey;
            prevKey     = key;

            val->parent = prevVal;
            prevVal     = val;

            ++size;
        }
        A1C_Pair* map = A1C_Item_map(item, size, &decoder->arena);
        if (map == NULL) {
            return A1C_Decoder_error(decoder, A1C_ErrorType_badAlloc);
        }
        while (prevKey != NULL) {
            const A1C_Item* key = prevKey;
            prevKey             = prevKey->parent;

            assert(prevVal != NULL);
            const A1C_Item* val = prevVal;
            prevVal             = prevVal->parent;

            --size;
            map[size].key        = *key;
            map[size].key.parent = item;
            map[size].val        = *val;
            map[size].val.parent = item;
        }
        assert(size == 0);
    } else {
        if (A1C_Decoder_remaining(decoder) < size) {
            // Each item must be at least one byte
            // Check remaining before allocation to avoid huge allocations.
            return A1C_Decoder_error(decoder, A1C_ErrorType_truncated);
        }
        A1C_Pair* map = A1C_Item_map(item, size, &decoder->arena);
        if (map == NULL) {
            return A1C_Decoder_error(decoder, A1C_ErrorType_badAlloc);
        }
        for (size_t i = 0; i < size; i++) {
            A1C_RET_IF_ERR(A1C_Decoder_decodeOneInto(decoder, &map[i].key));
            map[i].key.parent = item;

            A1C_RET_IF_ERR(A1C_Decoder_decodeOneInto(decoder, &map[i].val));
            map[i].val.parent = item;
        }
    }
    return true;
}

static bool A1C_NODISCARD A1C_Decoder_decodeTag(
        A1C_Decoder* decoder,
        A1C_ItemHeader header,
        A1C_Item* item)
{
    uint64_t value;
    A1C_RET_IF_ERR(A1C_Decoder_readCount(decoder, header, &value));
    A1C_Item* child = A1C_Item_tag(item, value, &decoder->arena);
    if (child == NULL) {
        return A1C_Decoder_error(decoder, A1C_ErrorType_badAlloc);
    }
    A1C_RET_IF_ERR(A1C_Decoder_decodeOneInto(decoder, child));
    assert(child->parent == item);

    return true;
}

static bool A1C_NODISCARD A1C_Decoder_decodeSpecial(
        A1C_Decoder* decoder,
        A1C_ItemHeader header,
        A1C_Item* item)
{
    const size_t shortCount = A1C_ItemHeader_shortCount(header);
    if (shortCount == 20 || shortCount == 21) {
        A1C_Item_boolean(item, shortCount == 21);
    } else if (shortCount == 22) {
        A1C_Item_null(item);
    } else if (shortCount == 23) {
        A1C_Item_undefined(item);
    } else if (shortCount == 24) {
        if (decoder->rejectUnknownSimple) {
            return A1C_Decoder_error(
                    decoder, A1C_ErrorType_invalidSimpleEncoding);
        }
        uint8_t value;
        A1C_RET_IF_ERR(A1C_Decoder_read(decoder, &value, sizeof(value)));
        if (value < 32) {
            return A1C_Decoder_error(
                    decoder, A1C_ErrorType_invalidSimpleEncoding);
        }
        A1C_Item_simple(item, value);
    } else if (shortCount == 25) {
        uint16_t value;
        A1C_RET_IF_ERR(A1C_Decoder_read(decoder, &value, sizeof(value)));
        value = A1C_bigEndian16(value);
        A1C_Item_float16(item, value);
    } else if (shortCount == 26) {
        uint32_t value;
        A1C_RET_IF_ERR(A1C_Decoder_read(decoder, &value, sizeof(value)));
        value = A1C_bigEndian32(value);
        float float32;
        memcpy(&float32, &value, sizeof(float32));
        A1C_Item_float32(item, float32);
    } else if (shortCount == 27) {
        uint64_t value;
        A1C_RET_IF_ERR(A1C_Decoder_read(decoder, &value, sizeof(value)));
        value = A1C_bigEndian64(value);
        double float64;
        memcpy(&float64, &value, sizeof(float64));
        A1C_Item_float64(item, float64);
    } else if (shortCount < 20) {
        if (decoder->rejectUnknownSimple) {
            return A1C_Decoder_error(
                    decoder, A1C_ErrorType_invalidSimpleEncoding);
        }
        A1C_Item_simple(item, (uint8_t)shortCount);
    } else if (shortCount == 31) {
        return A1C_Decoder_error(decoder, A1C_ErrorType_breakNotAllowed);
    } else {
        assert(shortCount >= 28 && shortCount <= 30);
        assert(false);
    }
    return true;
}

static A1C_Item* A1C_NODISCARD A1C_Decoder_decodeOne(A1C_Decoder* decoder)
{
    A1C_Item* item = A1C_Arena_calloc(&decoder->arena, 1, sizeof(A1C_Item));
    if (item == NULL) {
        const bool result = A1C_Decoder_error(decoder, A1C_ErrorType_badAlloc);
        (void)result;
        return NULL;
    }
    item->parent = decoder->parent;
    if (!A1C_Decoder_decodeOneInto(decoder, item)) {
        return NULL;
    }
    return item;
}

static bool A1C_NODISCARD
A1C_Decoder_decodeOneInto(A1C_Decoder* decoder, A1C_Item* item)
{
    if (++decoder->depth > decoder->maxDepth) {
        return A1C_Decoder_error(decoder, A1C_ErrorType_maxDepthExceeded);
    }

    A1C_ItemHeader header;
    A1C_RET_IF_ERR(A1C_Decoder_read(decoder, &header, sizeof(header)));

    if (!A1C_ItemHeader_isLegal(header)) {
        return A1C_Decoder_error(decoder, A1C_ErrorType_invalidItemHeader);
    }

    switch (A1C_ItemHeader_majorType(header)) {
        case A1C_MajorType_uint:
            A1C_RET_IF_ERR(A1C_Decoder_decodeUInt(decoder, header, item));
            break;
        case A1C_MajorType_int:
            A1C_RET_IF_ERR(A1C_Decoder_decodeInt(decoder, header, item));
            break;
        case A1C_MajorType_bytes:
            A1C_RET_IF_ERR(A1C_Decoder_decodeData(decoder, header, item));
            break;
        case A1C_MajorType_string:
            A1C_RET_IF_ERR(A1C_Decoder_decodeData(decoder, header, item));
            break;
        case A1C_MajorType_array:
            A1C_RET_IF_ERR(A1C_Decoder_decodeArray(decoder, header, item));
            break;
        case A1C_MajorType_map:
            A1C_RET_IF_ERR(A1C_Decoder_decodeMap(decoder, header, item));
            break;
        case A1C_MajorType_tag:
            A1C_RET_IF_ERR(A1C_Decoder_decodeTag(decoder, header, item));
            break;
        case A1C_MajorType_special:
            A1C_RET_IF_ERR(A1C_Decoder_decodeSpecial(decoder, header, item));
            break;
    }
    --decoder->depth;
    return true;
}

A1C_Item*
A1C_Decoder_decode(A1C_Decoder* decoder, const uint8_t* data, size_t size)
{
    A1C_Decoder_reset(decoder, data, size);
    if (data == NULL) {
        decoder->error.type   = A1C_ErrorType_truncated;
        decoder->error.srcPos = 0;
        return NULL;
    }
    A1C_Item* item = A1C_Decoder_decodeOne(decoder);
    if (item != NULL && decoder->ptr < decoder->end) {
        const bool result =
                A1C_Decoder_error(decoder, A1C_ErrorType_trailingData);
        (void)result;
        return NULL;
    }
    return item;
}

////////////////////////////////////////
// Encoder
////////////////////////////////////////

void A1C_Encoder_init(
        A1C_Encoder* encoder,
        A1C_Encoder_WriteCallback write,
        void* opaque)
{
    memset(encoder, 0, sizeof(*encoder));
    encoder->write  = write;
    encoder->opaque = opaque;
}

A1C_Error A1C_Encoder_getError(const A1C_Encoder* encoder)
{
    return encoder->error;
}

static void A1C_Encoder_reset(A1C_Encoder* encoder)
{
    memset(&encoder->error, 0, sizeof(A1C_Error));
    encoder->bytesWritten = 0;
    encoder->depth        = 0;
}

bool A1C_Encoder_encodeOne(A1C_Encoder* encoder, const A1C_Item* item);

static bool A1C_NODISCARD A1C_Encoder_errorImpl(
        A1C_Encoder* encoder,
        A1C_ErrorType errorType,
        const char* file,
        int line)
{
    encoder->error.type   = errorType;
    encoder->error.srcPos = encoder->bytesWritten;
    encoder->error.depth  = encoder->depth;
    encoder->error.item   = encoder->currentItem;
    encoder->error.file   = file;
    encoder->error.line   = line;
    return false;
}

#define A1C_Encoder_error(encoder, errorType) \
    A1C_Encoder_errorImpl((encoder), (errorType), __FILE__, __LINE__)

static bool A1C_NODISCARD
A1C_Encoder_write(A1C_Encoder* encoder, const void* data, size_t size)
{
    if (size == 0) {
        return true;
    }
    const size_t written = encoder->write(encoder->opaque, data, size);
    encoder->bytesWritten += written;

    if (written < size) {
        return A1C_Encoder_error(encoder, A1C_ErrorType_writeFailed);
    }
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_writeCStr(A1C_Encoder* encoder, const char* cstr)
{
    return A1C_Encoder_write(encoder, cstr, strlen(cstr));
}

static bool A1C_NODISCARD A1C_Encoder_putc(A1C_Encoder* encoder, char c)
{
    return A1C_Encoder_write(encoder, &c, 1);
}

static bool A1C_NODISCARD A1C_Encoder_encodeHeaderAndCount(
        A1C_Encoder* encoder,
        A1C_MajorType majorType,
        uint64_t count)
{
    uint8_t shortCount;
    if (count < 24) {
        shortCount = (uint8_t)count;
    } else if (count <= UINT8_MAX) {
        shortCount = 24;
    } else if (count <= UINT16_MAX) {
        shortCount = 25;
    } else if (count <= UINT32_MAX) {
        shortCount = 26;
    } else {
        shortCount = 27;
    }
    A1C_ItemHeader header = A1C_ItemHeader_make(majorType, shortCount);
    A1C_RET_IF_ERR(A1C_Encoder_write(encoder, &header, sizeof(header)));
    if (shortCount == 24) {
        uint8_t count8 = (uint8_t)count;
        return A1C_Encoder_write(encoder, &count8, sizeof(count8));
    } else if (shortCount == 25) {
        uint16_t count16 = A1C_bigEndian16((uint16_t)count);
        return A1C_Encoder_write(encoder, &count16, sizeof(count16));
    } else if (shortCount == 26) {
        uint32_t count32 = A1C_bigEndian32((uint32_t)count);
        return A1C_Encoder_write(encoder, &count32, sizeof(count32));
    } else if (shortCount == 27) {
        uint64_t count64 = A1C_bigEndian64((uint64_t)count);
        return A1C_Encoder_write(encoder, &count64, sizeof(count64));
    }
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_encodeInt(A1C_Encoder* encoder, const A1C_Item* item)
{
    assert(item->type == A1C_ItemType_int64);
    A1C_MajorType majorType;
    uint64_t value;
    if (item->int64 >= 0) {
        majorType = A1C_MajorType_uint;
        value     = (uint64_t)item->int64;
    } else {
        majorType = A1C_MajorType_int;
        value     = (uint64_t)~item->int64;
    }
    return A1C_Encoder_encodeHeaderAndCount(encoder, majorType, value);
}

static bool A1C_NODISCARD
A1C_Encoder_encodeData(A1C_Encoder* encoder, const A1C_Item* item)
{
    assert(item->type == A1C_ItemType_bytes
           || item->type == A1C_ItemType_string);
    const A1C_MajorType majorType = item->type == A1C_ItemType_bytes
            ? A1C_MajorType_bytes
            : A1C_MajorType_string;
    const size_t count = item->type == A1C_ItemType_bytes ? item->bytes.size
                                                          : item->string.size;
    if (!A1C_Encoder_encodeHeaderAndCount(encoder, majorType, count)) {
        return false;
    }
    const void* data = item->type == A1C_ItemType_bytes
            ? (const void*)item->bytes.data
            : (const void*)item->string.data;
    return A1C_Encoder_write(encoder, data, count);
}

static bool A1C_NODISCARD
A1C_Encoder_encodeArray(A1C_Encoder* encoder, const A1C_Item* item)
{
    assert(item->type == A1C_ItemType_array);
    const size_t count = item->array.size;
    if (!A1C_Encoder_encodeHeaderAndCount(
                encoder, A1C_MajorType_array, count)) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        const A1C_Item* child = &item->array.items[i];
        if (!A1C_Encoder_encodeOne(encoder, child)) {
            return false;
        }
    }
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_encodeMap(A1C_Encoder* encoder, const A1C_Item* item)
{
    assert(item->type == A1C_ItemType_map);
    const size_t count = item->map.size;
    if (!A1C_Encoder_encodeHeaderAndCount(encoder, A1C_MajorType_map, count)) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        const A1C_Pair pair = item->map.items[i];
        if (!A1C_Encoder_encodeOne(encoder, &pair.key)) {
            return false;
        }
        if (!A1C_Encoder_encodeOne(encoder, &pair.val)) {
            return false;
        }
    }
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_encodeTag(A1C_Encoder* encoder, const A1C_Item* item)
{
    assert(item->type == A1C_ItemType_tag);
    const A1C_Tag* tag = &item->tag;
    if (!A1C_Encoder_encodeHeaderAndCount(
                encoder, A1C_MajorType_tag, tag->tag)) {
        return false;
    }
    if (!A1C_Encoder_encodeOne(encoder, tag->item)) {
        return false;
    }
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_encodeSpecial(A1C_Encoder* encoder, const A1C_Item* item)
{
    if (item->type == A1C_ItemType_boolean) {
        const uint64_t count = item->boolean ? 21 : 20;
        return A1C_Encoder_encodeHeaderAndCount(
                encoder, A1C_MajorType_special, count);
    } else if (item->type == A1C_ItemType_null) {
        return A1C_Encoder_encodeHeaderAndCount(
                encoder, A1C_MajorType_special, 22);
    } else if (item->type == A1C_ItemType_undefined) {
        return A1C_Encoder_encodeHeaderAndCount(
                encoder, A1C_MajorType_special, 23);
    } else if (item->type == A1C_ItemType_simple) {
        if (item->simple >= 20 && item->simple < 32) {
            return A1C_Encoder_error(encoder, A1C_ErrorType_invalidSimpleValue);
        }
        return A1C_Encoder_encodeHeaderAndCount(
                encoder, A1C_MajorType_special, item->simple);
    } else if (item->type == A1C_ItemType_float16) {
        A1C_ItemHeader header = A1C_ItemHeader_make(A1C_MajorType_special, 25);
        A1C_RET_IF_ERR(A1C_Encoder_write(encoder, &header, sizeof(header)));
        const uint16_t value = A1C_bigEndian16(item->float16);
        return A1C_Encoder_write(encoder, &value, sizeof(value));
    } else if (item->type == A1C_ItemType_float32) {
        A1C_ItemHeader header = A1C_ItemHeader_make(A1C_MajorType_special, 26);
        A1C_RET_IF_ERR(A1C_Encoder_write(encoder, &header, sizeof(header)));
        uint32_t value;
        memcpy(&value, &item->float32, sizeof(item->float32));
        value = A1C_bigEndian32(value);
        return A1C_Encoder_write(encoder, &value, sizeof(value));
    } else if (item->type == A1C_ItemType_float64) {
        A1C_ItemHeader header = A1C_ItemHeader_make(A1C_MajorType_special, 27);
        A1C_RET_IF_ERR(A1C_Encoder_write(encoder, &header, sizeof(header)));
        uint64_t value;
        memcpy(&value, &item->float64, sizeof(item->float64));
        value = A1C_bigEndian64(value);
        return A1C_Encoder_write(encoder, &value, sizeof(value));
    } else {
        assert(false);
        return false;
    }
}

bool A1C_Encoder_encodeOne(A1C_Encoder* encoder, const A1C_Item* item)
{
    ++encoder->depth;
    encoder->currentItem = item;
    switch (item->type) {
        case A1C_ItemType_int64:
            A1C_RET_IF_ERR(A1C_Encoder_encodeInt(encoder, item));
            break;
        case A1C_ItemType_bytes:
        case A1C_ItemType_string:
            A1C_RET_IF_ERR(A1C_Encoder_encodeData(encoder, item));
            break;
        case A1C_ItemType_array:
            A1C_RET_IF_ERR(A1C_Encoder_encodeArray(encoder, item));
            break;
        case A1C_ItemType_map:
            A1C_RET_IF_ERR(A1C_Encoder_encodeMap(encoder, item));
            break;
        case A1C_ItemType_tag:
            A1C_RET_IF_ERR(A1C_Encoder_encodeTag(encoder, item));
            break;
        case A1C_ItemType_boolean:
        case A1C_ItemType_null:
        case A1C_ItemType_undefined:
        case A1C_ItemType_float16:
        case A1C_ItemType_float32:
        case A1C_ItemType_float64:
        case A1C_ItemType_simple:
            A1C_RET_IF_ERR(A1C_Encoder_encodeSpecial(encoder, item));
            break;
    }

    --encoder->depth;
    return true;
}

bool A1C_Encoder_encode(A1C_Encoder* encoder, const A1C_Item* item)
{
    A1C_Encoder_reset(encoder);
    return A1C_Encoder_encodeOne(encoder, item);
}

////////////////////////////////////////
// Encoder JSON
////////////////////////////////////////

static bool A1C_Encoder_jsonOne(A1C_Encoder* encoder, const A1C_Item* item);

static bool A1C_Encoder_jsonIndent(A1C_Encoder* encoder)
{
    for (size_t i = 0; i < encoder->depth; ++i) {
        A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "  "));
    }
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_jsonNumeric(A1C_Encoder* encoder, const A1C_Item* item)
{
    char buffer[32];
    int len;
    if (item->type == A1C_ItemType_int64) {
        len = snprintf(buffer, sizeof(buffer), "%lld", (long long)item->int64);
    } else if (item->type == A1C_ItemType_float32) {
        len = snprintf(buffer, sizeof(buffer), "%g", item->float32);
    } else {
        assert(item->type == A1C_ItemType_float64);
        len = snprintf(buffer, sizeof(buffer), "%g", item->float64);
    }
    if (len < 0 || (size_t)len >= sizeof(buffer)) {
        return A1C_Encoder_error(encoder, A1C_ErrorType_formatError);
    }
    return A1C_Encoder_write(encoder, buffer, (size_t)len);
}

static bool A1C_NODISCARD
A1C_Encoder_jsonBytes(A1C_Encoder* encoder, const A1C_Item* item)
{
    A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, '"'));
    const size_t size   = item->bytes.size;
    const uint8_t* data = item->bytes.data;
    if (size > 0) {
        const uint8_t* end = data + size;
        while (data < end) {
            char buffer[256];
            size_t toEncode = (size_t)(end - data);
            if (toEncode > 192) {
                toEncode = 192;
            }
            assert(A1C_base64EncodedSize(toEncode) <= sizeof(buffer));
            const size_t base64Size = A1C_base64Encode(buffer, data, toEncode);
            A1C_RET_IF_ERR(A1C_Encoder_write(encoder, buffer, base64Size));
            data += toEncode;
        }
        assert(data == end);
    }
    A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, '"'));
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_jsonString(A1C_Encoder* encoder, const A1C_Item* item)
{
    A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, '"'));
    for (size_t i = 0; i < item->string.size; ++i) {
        const char c = item->string.data[i];
        if ((uint8_t)c >= 0x80) {
            return A1C_Encoder_error(
                    encoder, A1C_ErrorType_jsonUTF8Unsupported);
        }
        if (c == '"') {
            A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "\\\""));
        } else if (c == '\\') {
            A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "\\\\"));
        } else if (c == '\r') {
            A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "\\r"));
        } else if (c == '\n') {
            A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "\\n"));
        } else if (c == '\t') {
            A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "\\t"));
        } else if (c < 0x20 || c > 0x7E) {
            char buffer[7];
            int len = snprintf(
                    buffer,
                    sizeof(buffer),
                    "\\u%04x",
                    (unsigned int)(uint8_t)c);
            if (len < 0 || (size_t)len >= sizeof(buffer)) {
                return A1C_Encoder_error(encoder, A1C_ErrorType_formatError);
            }
            A1C_RET_IF_ERR(A1C_Encoder_write(encoder, buffer, (size_t)len));
        } else {
            A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, c));
        }
    }
    A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, '"'));
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_jsonArray(A1C_Encoder* encoder, const A1C_Item* item)
{
    A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "["));

    ++encoder->depth;
    for (size_t i = 0; i < item->array.size; ++i) {
        if (i != 0) {
            A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, ','));
        }
        A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, '\n'));
        A1C_RET_IF_ERR(A1C_Encoder_jsonIndent(encoder));
        A1C_RET_IF_ERR(A1C_Encoder_jsonOne(encoder, &item->array.items[i]));
    }
    --encoder->depth;

    A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, '\n'));
    A1C_RET_IF_ERR(A1C_Encoder_jsonIndent(encoder));
    A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "]"));
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_jsonMap(A1C_Encoder* encoder, const A1C_Item* item)
{
    A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "{"));

    ++encoder->depth;
    for (size_t i = 0; i < item->map.size; ++i) {
        if (i != 0) {
            A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, ','));
        }
        A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, '\n'));
        A1C_RET_IF_ERR(A1C_Encoder_jsonIndent(encoder));
        A1C_RET_IF_ERR(A1C_Encoder_jsonOne(encoder, &item->map.items[i].key));
        A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, ": "));
        A1C_RET_IF_ERR(A1C_Encoder_jsonOne(encoder, &item->map.items[i].val));
    }
    --encoder->depth;

    A1C_RET_IF_ERR(A1C_Encoder_putc(encoder, '\n'));
    A1C_RET_IF_ERR(A1C_Encoder_jsonIndent(encoder));
    A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "}"));
    return true;
}

static bool A1C_NODISCARD
A1C_Encoder_jsonTag(A1C_Encoder* encoder, const A1C_Item* item)
{
    A1C_Pair items[3];
    A1C_Item map;
    map.type      = A1C_ItemType_map;
    map.map.items = items;
    map.map.size  = 3;

    A1C_Item_string_refCStr(&items[0].key, "type");
    A1C_Item_string_refCStr(&items[0].val, "tag");
    A1C_Item_string_refCStr(&items[1].key, "tag");
    A1C_Item_int64(&items[1].val, (int64_t)item->tag.tag);
    A1C_Item_string_refCStr(&items[2].key, "value");
    items[2].val = *item->tag.item;

    return A1C_Encoder_jsonMap(encoder, &map);
}

static bool A1C_NODISCARD
A1C_Encoder_jsonSimple(A1C_Encoder* encoder, const A1C_Item* item)
{
    A1C_Pair items[2];
    A1C_Item map;
    map.type      = A1C_ItemType_map;
    map.map.items = items;
    map.map.size  = 2;

    A1C_Item_string_refCStr(&items[0].key, "type");
    A1C_Item_string_refCStr(&items[0].val, "simple");
    A1C_Item_string_refCStr(&items[1].key, "value");
    A1C_Item_int64(&items[1].val, item->simple);

    return A1C_Encoder_jsonMap(encoder, &map);
}

static bool A1C_NODISCARD
A1C_Encoder_jsonHalf(A1C_Encoder* encoder, const A1C_Item* item)
{
    A1C_Pair items[2];
    A1C_Item map;
    map.type      = A1C_ItemType_map;
    map.map.items = items;
    map.map.size  = 2;

    A1C_Item_string_refCStr(&items[0].key, "type");
    A1C_Item_string_refCStr(&items[0].val, "half");
    A1C_Item_string_refCStr(&items[1].key, "uint16");
    A1C_Item_int64(&items[1].val, item->float16);

    return A1C_Encoder_jsonMap(encoder, &map);
}

bool A1C_Encoder_jsonOne(A1C_Encoder* encoder, const A1C_Item* item)
{
    encoder->currentItem = item;
    switch (item->type) {
        case A1C_ItemType_int64:
        case A1C_ItemType_float32:
        case A1C_ItemType_float64:
            A1C_RET_IF_ERR(A1C_Encoder_jsonNumeric(encoder, item));
            break;
        case A1C_ItemType_float16:
            A1C_RET_IF_ERR(A1C_Encoder_jsonHalf(encoder, item));
            break;
        case A1C_ItemType_bytes:
            A1C_RET_IF_ERR(A1C_Encoder_jsonBytes(encoder, item));
            break;
        case A1C_ItemType_string:
            A1C_RET_IF_ERR(A1C_Encoder_jsonString(encoder, item));
            break;
        case A1C_ItemType_array:
            A1C_RET_IF_ERR(A1C_Encoder_jsonArray(encoder, item));
            break;
        case A1C_ItemType_map:
            A1C_RET_IF_ERR(A1C_Encoder_jsonMap(encoder, item));
            break;
        case A1C_ItemType_tag:
            A1C_RET_IF_ERR(A1C_Encoder_jsonTag(encoder, item));
            break;
        case A1C_ItemType_boolean:
            if (item->boolean) {
                A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "true"));
            } else {
                A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "false"));
            }
            break;
        case A1C_ItemType_null:
            A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "null"));
            break;
        case A1C_ItemType_undefined:
            A1C_RET_IF_ERR(A1C_Encoder_writeCStr(encoder, "undefined"));
            break;
        case A1C_ItemType_simple:
            A1C_RET_IF_ERR(A1C_Encoder_jsonSimple(encoder, item));
            break;
    }

    return true;
}

bool A1C_Encoder_json(A1C_Encoder* encoder, const A1C_Item* item)
{
    A1C_Encoder_reset(encoder);
    return A1C_Encoder_jsonOne(encoder, item);
}

////////////////////////////////////////
// Simple Encoder
////////////////////////////////////////

static size_t A1C_noopWrite(void* opaque, const uint8_t* data, size_t size)
{
    (void)opaque;
    (void)data;
    return size;
}

size_t A1C_Item_encodedSize(const A1C_Item* item)
{
    A1C_Encoder encoder;
    A1C_Encoder_init(&encoder, A1C_noopWrite, NULL);
    if (!A1C_Encoder_encode(&encoder, item)) {
        return 0;
    }
    return encoder.bytesWritten;
}

typedef struct {
    uint8_t* ptr;
    uint8_t* end;
} A1C_Buffer;

static size_t A1C_bufferWrite(void* opaque, const uint8_t* data, size_t size)
{
    A1C_Buffer* buffer = (A1C_Buffer*)opaque;
    assert(buffer->ptr <= buffer->end);
    const size_t capacity = (size_t)(buffer->end - buffer->ptr);
    if (size > capacity) {
        size = capacity;
    }
    if (size > 0) {
        memcpy(buffer->ptr, data, size);
        buffer->ptr += size;
    }
    return size;
}

size_t A1C_Item_encode(
        const A1C_Item* item,
        uint8_t* dst,
        size_t dstCapacity,
        A1C_Error* error)
{
    A1C_Buffer buf = {
        .ptr = dst,
        .end = dst + dstCapacity,
    };
    A1C_Encoder encoder;
    A1C_Encoder_init(&encoder, A1C_bufferWrite, &buf);
    bool success = A1C_Encoder_encode(&encoder, item);
    assert(encoder.bytesWritten == (size_t)(buf.ptr - dst));
    if (success) {
        return encoder.bytesWritten;
    }
    if (error != NULL) {
        *error = encoder.error;
    }
    return 0;
}

size_t A1C_Item_jsonSize(const A1C_Item* item)
{
    A1C_Encoder encoder;
    A1C_Encoder_init(&encoder, A1C_noopWrite, NULL);
    if (!A1C_Encoder_json(&encoder, item)) {
        return 0;
    }
    return encoder.bytesWritten;
}

size_t A1C_Item_json(
        const A1C_Item* item,
        uint8_t* dst,
        size_t dstCapacity,
        A1C_Error* error)
{
    A1C_Buffer buf = {
        .ptr = dst,
        .end = dst + dstCapacity,
    };
    A1C_Encoder encoder;
    A1C_Encoder_init(&encoder, A1C_bufferWrite, &buf);
    bool success = A1C_Encoder_json(&encoder, item);
    assert(encoder.bytesWritten == (size_t)(buf.ptr - dst));
    if (success) {
        return encoder.bytesWritten;
    }
    if (error != NULL) {
        *error = encoder.error;
    }
    return 0;
}
