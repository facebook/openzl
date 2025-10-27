// Copyright (c) Meta Platforms, Inc. and affiliates.

/* zs2_localParams.h
 * Design notes
 *
 * This API regroups definitions of Local Parameters,
 * which are present into various components of the library,
 * such as Transforms, Selectors, and Graphs.
 */

#ifndef ZSTRONG_ZS2_LOCALPARAMS_H
#define ZSTRONG_ZS2_LOCALPARAMS_H

#include <stddef.h> // size_t

#include "openzl/zl_opaque_types.h" // ZL_GENERIC_LIST
#include "openzl/zl_portability.h"  // ZL_NOEXCEPT_FUNC_PTR

#if defined(__cplusplus)
extern "C" {
#endif

/* -------------------------------------------------
 * Local Parameters
 * -------------------------------------------------
 * Local Parameters are local to the instance they are associated with.
 * If there are multiple instances of the same component,
 * each instance can have its own set of local parameters.
 *
 * Within each instance, local parameters are identified by a unique ID.
 * The ID only needs to be unique within the instance.
 * Different Transforms could use the same ID, and it would still work fine (no
 * risk of collision). However, it's good practice to favor global uniqueness
 * just to simplify debugging when reading trace logs.
 *
 * There are 2 separate ID planes per instance:
 * - one for integer parameters
 * - one for all other types of parameters
 * Each ID must be unique within its own plane.
 */

/* -------------------------------------------------
 * Integer Parameters
 * -------------------------------------------------*/

/* single local integer parameter.
 * The ID plane of integer parameters is separated from other types.
 * On the Transform's side, these parameters can be requested
 * using ZS2_*_getLocalIntParam() (see "zs2_ctransform.h") */
typedef struct {
    int paramId; /* prefer employing an enum for improved readability */
    int paramValue;
} ZL_IntParam;

/* structure representing a set of local integer parameters */
typedef struct {
    const ZL_IntParam* intParams;
    size_t nbIntParams;
} ZL_LocalIntParams;

/* -------------------------------------------------
 * Generic Parameters
 * -------------------------------------------------*/

/* Generic Parameters are whatever the Transform or Graph defines.
 * They can be arrays of any type, or fully opaque objects.
 * It's up to the Transform or Graph to define these parameters.
 * There are 3 ways to pass such parameters:
 * - by copy: notably suitable for small arrays, or POD types
 * - by reference: suitable for large arrays, or opaque objects
 * On the Transform's side, these parameters can be requested
 * using ZS2_*_getLocalParam(), which requires a parameter ID.
 * They are always received by read-only reference.
 */

/* Copy parameters (POD type)
 * The content of the provided buffer will be copied into @cgraph,
 * ensuring lifetime independence.
 * This makes it possible, for example, to pass parameters from the stack.
 * Note that should the flat buffer contain pointers,
 * only the pointer values are copied,
 * therefore content of these references must outlive @cgraph.
 * */
typedef struct {
    int paramId; /* prefer employing an enum for improved readability */
    const void* paramPtr;
    size_t paramSize;
} ZL_CopyParam;
#define ZL_CopyParam ZL_CopyParam // backward compatibility

/* structure representing a set of copy parameters */
typedef struct {
    const ZL_CopyParam* copyParams;
    size_t nbCopyParams;
} ZL_LocalCopyParams;
// backward compatibility
#define ZL_LocalCopyParams ZL_LocalCopyParams
#define genParams copyParams
#define nbGenParams nbCopyParams

/* Referenced parameter
 * The reference must remain valid throughout @cgraph's lifetime,
 * i.e. it must outlive @cgraph.
 * Note that all generic parameters share the same ID plane.
 */
typedef struct {
    int paramId; /* prefer employing an enum for improved readability */
    const void* paramRef;
    /// Optionally the size of the referenced object.
    /// OpenZL does not interpret this value. A common pattern is to use the
    /// value 0 to mean unknown size.
    size_t paramSize;
} ZL_RefParam;

/* structure representing a set of reference parameters */
typedef struct {
    const ZL_RefParam* refParams;
    size_t nbRefParams;
} ZL_LocalRefParams;

/* structure representing a complete set of local parameters */
typedef struct {
    ZL_LocalIntParams intParams;
    ZL_LocalCopyParams copyParams;
    ZL_LocalRefParams refParams;
} ZL_LocalParams;
#define genericParams copyParams // support older member name

/* -------------------------------------------------
 * Helper macros
 * -------------------------------------------------*/

/* Helpers macros, to define typed local params:
 * Note (@Cyan) : these macros only work in C11 mode, not in C++.
 * Example :
 * ZL_LocalIntParams lip =
 *     ZL_INTPARAMS( {id1, value1}, {id2, value2} );
 */
#define ZL_INTPARAMS(...) { ZL_GENERIC_LIST(ZL_IntParam, __VA_ARGS__) }
#define ZL_COPYPARAMS(...) { ZL_GENERIC_LIST(ZL_CopyParam, __VA_ARGS__) }
#define ZL_REFPARAMS(...) { ZL_GENERIC_LIST(ZL_RefParam, __VA_ARGS__) }

/* Note(@Cyan): below macros might be replaceable by inline functions */

/* Helper, to create LocalParams with just a single Integer Parameter
 **/
#define ZL_LP_1INTPARAM(_id, _val)                \
    (ZL_LocalParams)                              \
    {                                             \
        .intParams = ZL_INTPARAMS({ _id, _val }), \
    }

/* Helper, to create LocalParams with just a single Copied Parameter
 **/
#define ZL_LP_1COPYPARAM(_id, _ptr, _size)                 \
    (ZL_LocalParams)                                       \
    {                                                      \
        .copyParams = ZL_COPYPARAMS({ _id, _ptr, _size }), \
    }

/* Helper, to create LocalParams with just a single Referenced Parameter
 **/
#define ZL_LP_1REFPARAM(_id, _ptr)                \
    (ZL_LocalParams)                              \
    {                                             \
        .refParams = ZL_REFPARAMS({ _id, _ptr }), \
    }

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_LOCALPARAMS_H
