// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef A1CBOR_H
#define A1CBOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

////////////////////////////////////////
// Portability
////////////////////////////////////////

#ifdef __has_attribute
#    define A1C_HAS_ATTRIBUTE(x) __has_attribute(x)
#else
#    define A1C_HAS_ATTRIBUTE(x) 0
#endif

#if A1C_HAS_ATTRIBUTE(warn_unused_result)
#    define A1C_NODISCARD __attribute__((warn_unused_result))
#else
#    define A1C_NODISCARD
#endif

/**
 * Functions passed into A1CBOR from C++ should be `noexcept`. The C++
 * standard, by omitting to describe a behavior, makes it by default undefined
 * behavior to throw an exception from C++ code into C code, even if that C
 * code was itself called by C++ code which has an appropriate handler for the
 * thrown exception.
 *
 * Even in the case where the undefined behavior monster doesn't come to eat
 * your sanity, and the compiler generates code such that the intermediate C
 * stack frames are successfully unwound between the thrown exception and an
 * enclosing handler, it is still unsafe to throw exceptions through A1CBOR:
 * it will likely result in memory leaks and objects being left in inconsistent
 * states which make them unsafe to further operate on, including freeing them.
 *
 * Starting in C++17, `noexcept` is part of the type specification, and can
 * be used in typedefs as part of describing the type of a function pointer.
 */
#if defined(__cplusplus) && __cplusplus >= 201703L
#    define A1C_NOEXCEPT_FUNC_PTR noexcept
#else
#    define A1C_NOEXCEPT_FUNC_PTR
#endif

////////////////////////////////////////
// Item
////////////////////////////////////////

/**
 * The possible types of an A1C_Item.
 * If there is data associated with the type, it will be in the union with the
 * same name as the type.
 */
typedef enum {
    A1C_ItemType_undefined = 0,
    A1C_ItemType_int64,
    A1C_ItemType_bytes,
    A1C_ItemType_string,
    A1C_ItemType_array,
    A1C_ItemType_map,
    A1C_ItemType_boolean,
    A1C_ItemType_null,
    A1C_ItemType_float16,
    A1C_ItemType_float32,
    A1C_ItemType_float64,
    A1C_ItemType_simple,
    A1C_ItemType_tag,
} A1C_ItemType;

typedef int64_t A1C_Int64;
typedef bool A1C_Bool;
/// @note Float16 is only supported by returning a 16-bit container.
typedef uint16_t A1C_Float16;
typedef double A1C_Float64;
typedef float A1C_Float32;

typedef struct {
    const uint8_t* data;
    size_t size;
} A1C_Bytes;

typedef struct {
    const char* data;
    size_t size;
} A1C_String;

typedef struct {
    struct A1C_Pair* items;
    size_t size;
} A1C_Map;

typedef struct {
    struct A1C_Item* items;
    size_t size;
} A1C_Array;

typedef struct {
    uint64_t tag;
    struct A1C_Item* item;
} A1C_Tag;

typedef uint8_t A1C_Simple;

/**
 * A1C_Item is the main structure used to represent a single CBOR item.
 *
 * The type of the item is indicated by `type`. The actual data is stored in
 * a union of the same name as the type. For example, if `type` is
 * `A1C_ItemType_int64`, then the actual integer value can be found in `int64`.
 *
 * The `parent` field is used to track the parent of the item in the tree, and
 * is NULL for the root item.
 */
typedef struct A1C_Item {
    A1C_ItemType type;
    union {
        A1C_Bool boolean;
        A1C_Int64 int64;
        A1C_Float16 float16;
        A1C_Float32 float32;
        A1C_Float64 float64;
        A1C_Bytes bytes;
        A1C_String string;
        A1C_Map map;
        A1C_Array array;
        A1C_Simple simple;
        A1C_Tag tag;
    };
    struct A1C_Item* parent;
} A1C_Item;

typedef struct A1C_Pair {
    A1C_Item key;
    A1C_Item val;
} A1C_Pair;

////////////////////////////////////////
// Errors
////////////////////////////////////////

typedef enum {
    A1C_ErrorType_ok = 0,
    A1C_ErrorType_badAlloc,
    A1C_ErrorType_truncated,
    A1C_ErrorType_invalidItemHeader,
    A1C_ErrorType_largeIntegersUnsupported,
    A1C_ErrorType_integerOverflow,
    A1C_ErrorType_invalidChunkedString,
    A1C_ErrorType_maxDepthExceeded,
    A1C_ErrorType_invalidSimpleEncoding,
    A1C_ErrorType_breakNotAllowed,
    A1C_ErrorType_writeFailed,
    A1C_ErrorType_invalidSimpleValue,
    A1C_ErrorType_formatError,
    A1C_ErrorType_trailingData,
    A1C_ErrorType_jsonUTF8Unsupported,
} A1C_ErrorType;

typedef struct {
    /// Error type.
    A1C_ErrorType type;
    /// The position within the encoded data where the error occurred.
    size_t srcPos;
    /// The depth at the time the error occurred.
    size_t depth;
    /// Decoding: The parent item of the item being decoded when the error
    /// occurred.
    /// Encoding: The item being encoded when the error occurred.
    const A1C_Item* item;
    /// The file where the error was reported.
    const char* file;
    /// The line where the error was reported.
    int line;
} A1C_Error;

/// @returns A string representation of the error type.
const char* A1C_ErrorType_getString(A1C_ErrorType type);

////////////////////////////////////////
// Arena
////////////////////////////////////////

typedef struct {
    /// Allocates and zeros memory of the given size. The memory must outlive
    /// any objects created by the library using this arena.
    /// @returns NULL on failure.
    void* (*calloc)(void* opaque, size_t bytes)A1C_NOEXCEPT_FUNC_PTR;
    /// Opaque pointer passed to alloc and calloc.
    void* opaque;
} A1C_Arena;

/// Arena wrapper that limits the number of bytes allocated.
typedef struct {
    A1C_Arena backingArena;
    size_t allocatedBytes;
    size_t limitBytes;
} A1C_LimitedArena;

/**
 * Creates a limited arena that won't allocate more than @p limitBytes.
 */
A1C_LimitedArena A1C_LimitedArena_init(A1C_Arena arena, size_t limitBytes);

/// Get an arena interface for the @p limitedArena.
A1C_Arena A1C_LimitedArena_arena(A1C_LimitedArena* limitedArena);

/// Reset the number of allocated bytes by the @p limitedArena.
/// @warning This does not free any memory.
void A1C_LimitedArena_reset(A1C_LimitedArena* limitedArena);

////////////////////////////////////////
// Decoder
////////////////////////////////////////

#define A1C_MAX_DEPTH_DEFAULT 128

typedef struct {
    /**
     * Maximum recursion depth allowed.
     *
     * Default (0) means use `A1C_MAX_DEPTH_DEFAULT`.
     */
    size_t maxDepth;
    /**
     * Limit the maximum number of bytes allocated by the decoder in a single
     * call to A1C_Decode_decode().
     *
     * Default (0) means unlimited.
     *
     * Unless the limit is lower, the maximum memory usage when decoding a
     * source of N bytes is sizeof(A1C_Item) * N. The decoder will NEVER
     * allocate more memory than this.
     */
    size_t limitBytes;
    /**
     * If true, the decoder will reference the original source for bytes and
     * strings, rather than copy the buffer into the arena.
     */
    bool referenceSource;
    /**
     * If true, the decoder will allow simple values with unknown types.
     * Otherwise A1C_ItemType_simple will not be used.
     */
    bool rejectUnknownSimple;
} A1C_DecoderConfig;

typedef struct {
    A1C_LimitedArena limitedArena;
    A1C_Arena arena;

    A1C_Error error;
    const uint8_t* start;
    const uint8_t* ptr;
    const uint8_t* end;
    A1C_Item* parent;
    size_t depth;
    size_t maxDepth;
    bool referenceSource;
    bool rejectUnknownSimple;
} A1C_Decoder;

/**
 * Initializes a decoder that allocates its A1C_Item objects in @p arena.
 * @note It is safe to free all memory allocated by the decoder once the
 * A1C_Item* allocated in the arena are no longer needed. The Decoder can
 * continue to be used.
 */
void A1C_Decoder_init(
        A1C_Decoder* decoder,
        A1C_Arena arena,
        A1C_DecoderConfig config);

/**
 * Decodes the CBOR encoded value in [data, data + size) into an A1C_Item.
 * Trailing bytes are not allowed. This function supports the full CBOR spec,
 * except that integers which do not fit in an int64_t are not supported.
 *
 * @note The memory usage of the decoder is at most sizeof(A1C_Item) * size,
 * unless the `limitBytes` configuration is lower.
 *
 * @returns The decoded item on success, or NULL on failure. Upon failure,
 * A1C_Decoder_getError() can be used to retrieve the error information.
 */
A1C_Item* A1C_NODISCARD
A1C_Decoder_decode(A1C_Decoder* decoder, const uint8_t* data, size_t size);

/**
 * @returns The error information from the last decode operation.
 */
A1C_Error A1C_Decoder_getError(const A1C_Decoder* decoder);

////////////////////////////////////////
// Item Helpers
////////////////////////////////////////

/**
 * @returns The value in the map with the key @p key or NULL if the key is not
 * found.
 */
A1C_Item* A1C_Map_get(const A1C_Map* map, const A1C_Item* key);
/// @returns The value in the map with the key @p key or NULL if the key is not
/// found.
A1C_Item* A1C_Map_get_cstr(const A1C_Map* map, const char* key);
/// @returns The value in the map with the key @p key or NULL if the key is not
/// found.
A1C_Item* A1C_Map_get_int(const A1C_Map* map, A1C_Int64 key);

/// @returns The item at index @p i in the array @p array, or NULL if @p i is
/// out of bounds.
A1C_Item* A1C_Array_get(const A1C_Array* array, size_t index);

/// @returns true if @p a and @p b are equal
bool A1C_Item_eq(const A1C_Item* a, const A1C_Item* b);

////////////////////////////////////////
// Creation
////////////////////////////////////////

/// @returns The root A1C_Item allocated in the given arena or NULL if the
/// allocation failed. The item defaults to `A1C_ItemType_undefined`.
A1C_Item* A1C_NODISCARD A1C_Item_root(A1C_Arena* arena);

/// Fills @p item with @p value and sets the type
void A1C_Item_int64(A1C_Item* item, A1C_Int64 val);

/// Fills @p item with @p value and sets the type
void A1C_Item_float16(A1C_Item* item, A1C_Float16 val);

/// Fills @p item with @p value and sets the type
void A1C_Item_float32(A1C_Item* item, A1C_Float32 val);

/// Fills @p item with @p value and sets the type
void A1C_Item_float64(A1C_Item* item, A1C_Float64 val);

/// Fills @p item with @p value and sets the type
void A1C_Item_boolean(A1C_Item* item, bool val);

/// Sets the type of @p item to null
void A1C_Item_null(A1C_Item* item);

/// Sets the type of @p item to undefined
void A1C_Item_undefined(A1C_Item* item);

/**
 * Sets @p item to the tag type with the tag @p tag and allocates
 * a child item in the arena.
 *
 * @returns The child item or NULL on allocation failure.
 */
A1C_Item* A1C_NODISCARD
A1C_Item_tag(A1C_Item* item, uint64_t tag, A1C_Arena* arena);

/**
 * Sets @p item to the bytes type with the size @p size and allocates
 * a buffer in the arena.
 *
 * @returns The allocated buffer on success or NULL on allocation failure.
 */
uint8_t* A1C_NODISCARD
A1C_Item_bytes(A1C_Item* item, size_t size, A1C_Arena* arena);

/**
 * Sets @p item to the bytes type with the given data and size, allocating
 * a buffer in the arena.
 *
 * @returns True on success, false on allocation failure.
 */
bool A1C_NODISCARD A1C_Item_bytes_copy(
        A1C_Item* item,
        const uint8_t* data,
        size_t size,
        A1C_Arena* arena);

/**
 * Sets @p item to the bytes type with the given data and size, referencing
 * the original source.
 */
void A1C_Item_bytes_ref(A1C_Item* item, const uint8_t* data, size_t size);

/**
 * Sets @p item to the string type with the size @p size and allocates
 * a buffer in the arena.
 *
 * @returns The allocated string on success or NULL on allocation failure.
 */
char* A1C_NODISCARD
A1C_Item_string(A1C_Item* item, size_t size, A1C_Arena* arena);

/**
 * Sets @p item to the string type with the given data and size, allocating
 * a buffer in the arena.
 *
 * @returns True on success, false on allocation failure.
 */
bool A1C_NODISCARD A1C_Item_string_copy(
        A1C_Item* item,
        const char* data,
        size_t size,
        A1C_Arena* arena);

/**
 * Equivalent to `A1C_Item_string_copy(item, data, strlen(data), arena)`.
 */
bool A1C_NODISCARD
A1C_Item_string_cstr(A1C_Item* item, const char* data, A1C_Arena* arena);

/**
 * Sets @p item to the string type with the given data and size, referencing
 * the original source.
 */
void A1C_Item_string_ref(A1C_Item* item, const char* data, size_t size);

/**
 * Equivalent to `A1C_Item_string_ref(item, data, strlen(data))`.
 */
void A1C_Item_string_refCStr(A1C_Item* item, const char* data);

/**
 * Creates a map in the given @p item of size @p size, allocating the keys and
 * values in the provided @p arena.
 *
 * @returns The map items, or NULL on allocation failure.
 */
A1C_Pair* A1C_NODISCARD
A1C_Item_map(A1C_Item* item, size_t size, A1C_Arena* arena);

typedef struct {
    A1C_Map* const map;
    A1C_Pair* const pairs;
    const size_t maxSize;
} A1C_MapBuilder;

/**
 * Creates a map in the given @p item of maximum size @p maxSize, allocating
 * space for the keys and values in the provided @p arena. Initializes the map
 * to have a size of 0. Use the returned `A1C_MapBuilder` to add items into
 * the map.
 *
 * @note This allocates space for @p maxSize elements.
 *
 * @returns the builder for the map, unconditionally.
 */
A1C_MapBuilder A1C_NODISCARD
A1C_Item_map_builder(A1C_Item* item, size_t maxSize, A1C_Arena* arena);

/**
 * Adds another element to the map.
 *
 * @note This is unsafe to use if you later reinitialize the underlying map.
 *       So don't do that!
 *
 * @returns if successful, a pointer to the added `A1C_Pair`, otherwise `NULL`.
 *          Causes for failure are either the initial allocation of the map
 *          in @ref A1C_Item_map_builder() failed or you've already added more
 *          than the `maxSize` elements you specified in that call.
 */
A1C_Pair* A1C_MapBuilder_add(A1C_MapBuilder builder);

/**
 * Creates an array in the given @p item of size @p size, allocating the items
 * in the provided @p arena.
 *
 * @returns The array items, or NULL on allocation failure.
 */
A1C_Item* A1C_NODISCARD
A1C_Item_array(A1C_Item* item, size_t size, A1C_Arena* arena);

typedef struct {
    A1C_Array* const array;
    A1C_Item* const items;
    const size_t maxSize;
} A1C_ArrayBuilder;

/**
 * Creates an array in the given @p item of maximum size @p maxSize, allocating
 * space for the values in the provided @p arena. Initializes the array to have
 * a size of 0. Use the returned `A1C_ArrayBuilder` to add items into the array.
 *
 * @note This allocates space for @p maxSize elements.
 *
 * @returns the builder for the array, unconditionally.
 */
A1C_ArrayBuilder A1C_NODISCARD
A1C_Item_array_builder(A1C_Item* item, size_t maxSize, A1C_Arena* arena);

/**
 * Adds another item to the array.
 *
 * @note This is unsafe to use if you later reinitialize the underlying array.
 *       So don't do that!
 *
 * @returns if successful, a pointer to the added `A1C_Item`, otherwise `NULL`.
 *          Causes for failure are either the initial allocation of the array
 *          in @ref A1C_Item_array_builder() failed or you've already added
 *          more than the `maxSize` elements you specified in that call.
 */
A1C_Item* A1C_ArrayBuilder_add(A1C_ArrayBuilder builder);

/**
 * Copies the contents of @p src into @p dst, allocating the items in the
 * provided @p arena.
 *
 * @returns The copied item or NULL on allocation failure.
 */
A1C_Item* A1C_Item_deepcopy(const A1C_Item* item, A1C_Arena* arena);

////////////////////////////////////////
// Encoder
////////////////////////////////////////

typedef size_t (*A1C_Encoder_WriteCallback)(
        void* opaque,
        const uint8_t* data,
        size_t size) A1C_NOEXCEPT_FUNC_PTR;

typedef struct {
    A1C_Error error;
    uint64_t bytesWritten;
    const A1C_Item* currentItem;
    A1C_Encoder_WriteCallback write;
    void* opaque;
    size_t depth;
} A1C_Encoder;

/**
 * Initializes an encoder for encoding CBOR.
 *
 * @param encoder The encoder to initialize.
 * @param write The callback to use for writing encoded bytes. It must try to
 *              write [data, size) and return the number of bytes written.
 *              Errors are reported by returning less than size.
 * @param opaque Opaque pointer passed to the write callback.
 */
void A1C_Encoder_init(
        A1C_Encoder* encoder,
        A1C_Encoder_WriteCallback write,
        void* opaque);

/**
 * Encodes a single A1C_Item into CBOR.
 *
 * @returns True on success and false on error. If the encoding fails, the error
 * information can be retrieved from A1C_Encoder_getError().
 */
bool A1C_NODISCARD
A1C_Encoder_encode(A1C_Encoder* encoder, const A1C_Item* item);

/**
 * Encodes a single A1C_Item into JSON format. If the CBOR is using non-JSON
 * features, like numeric keys, then this will return invalid JSON. If the CBOR
 * uses only JSON compatible features, it will return valid JSON.
 *
 * Limitations:
 * - Fails on non-ascii strings (UTF-8)
 * - Bytes are base64 encoded
 * - Floating point values are not losslessly encoded in all cases
 * - Tags, float16, and simple types are encoded as objects with the "type" key
 *   denoting the type
 *
 * @returns True on success and false on error. If the encoding fails, the error
 * information can be retrieved from A1C_Encoder_getError().
 */
bool A1C_NODISCARD A1C_Encoder_json(A1C_Encoder* encoder, const A1C_Item* item);

/**
 * @returns The error information from the last encoding operation.
 */
A1C_Error A1C_Encoder_getError(const A1C_Encoder* encoder);

////////////////////////////////////////
// Simple Encoder
////////////////////////////////////////

/// @returns The exact encoded size of @p item
/// @note This requires a full pass over @p item
size_t A1C_NODISCARD A1C_Item_encodedSize(const A1C_Item* item);

/**
 * Encodes @p item into [dst, dst + dstCapacity) and returns the number of bytes
 * written or 0 on error.
 *
 * @param[out] error If an error occurs, this will be filled in with the error
 * information. If you do not care about the error info, pass NULL.
 *
 * @returns The number of bytes written to @p dst on success, or 0 on failure.
 */
size_t A1C_NODISCARD A1C_Item_encode(
        const A1C_Item* item,
        uint8_t* dst,
        size_t dstCapacity,
        A1C_Error* error);

/// @returns The exact JSON encoded size of @p item
/// @note This requires a full pass over @p item
size_t A1C_NODISCARD A1C_Item_jsonSize(const A1C_Item* item);

/**
 * Encodes @p item as JSON into [dst, dst + dstCapacity) and returns the number
 * of bytes written or 0 on error.
 *
 * @param[out] error If an error occurs, this will be filled in with the error
 * information. If you do not care about the error info, pass NULL.
 *
 * @returns The number of bytes written to @p dst on success, or 0 on failure.
 */
size_t A1C_NODISCARD A1C_Item_json(
        const A1C_Item* item,
        uint8_t* dst,
        size_t dstCapacity,
        A1C_Error* error);

#ifdef __cplusplus
}
#endif

#endif
