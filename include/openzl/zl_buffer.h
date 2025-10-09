// Copyright (c) Meta Platforms, Inc. and affiliates.

// Public API
// Provides definition of xBuffer and xCursor
// which can be useful when employing the Custom Transform API.

#ifndef ZSTRONG_ZL_BUFFER_H
#define ZSTRONG_ZL_BUFFER_H

#include <stddef.h> // size_t

#ifdef __cplusplus
extern "C" {
#endif

/* Notes on design:
 *
 * There are a few separate levels of responsibility:
 * - Owning the buffer, i.e. being in charge of freeing it
 * - The size of the writable (or readable) area
 * - How much is already written (read)
 *
 * Owning is a dedicated topic.
 * A non-const starting pointer is enough to able to free() it.
 *
 * xBuffer belongs to category 2.
 * It doesn't imply ownership, it's essentially just a reference.
 *
 * xCursor belongs to category 3.
 * It essentially adds field @pos, to track writing (reading).
 * Writing (reading) is always considered starting from position 0.
 *
 * All sizes are provided in bytes.
 */

/* ZL_WBuffer :
 * Public definition to receive and use a write-enabled buffer.
 * A buffer may, in some cases, be empty or even non existent.
 * In which case, @capacity == 0, and @start == NULL.
 * This must be checked before writing into the buffer.
 */
typedef struct {
    void* start;
    size_t capacity;
} ZL_WBuffer;

/* ZL_WCursor :
 * Track write operations into a WBuffer.
 * Writing is presumed to start from position [0].
 * @pos represents the nb of bytes written from position [0].
 * @pos is always <= @wb.capacity.
 * A buffer may, in some cases, be empty or even non existent.
 * In which case, @wb.capacity == 0, and @wb.start == NULL.
 * This must be checked before writing into the buffer.
 *
 * Design note :
 * An appealing idea would be to have wb as a read-only member.
 * This way, usage of WCursor cannot impact the underlying wb WBuffer.
 * However, it's a problem when WCursor is stored into arrays,
 * as all array cells would be unable to change value after initialization.
 * This seems suitable only for temporary variables.
 * However, since arrays are required, it would necessitate 2 different types.
 * It's not clear that the benefit is worth such complexity.
 */
typedef struct {
    ZL_WBuffer wb;
    size_t pos;
} ZL_WCursor;

/* ZL_RBuffer :
 * Public definition to receive and use a read-only buffer.
 * A buffer may, in some cases, be empty or even non existent.
 * In which case, @size == 0.
 * This must be checked before attempting to read into the buffer.
 */
typedef struct {
    const void* start;
    size_t size;
} ZL_RBuffer;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZSTRONG_ZL_BUFFER_H
