// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_GDPARAMS_H
#define ZSTRONG_GDPARAMS_H

#include "openzl/shared/portability.h"
#include "openzl/zl_common_types.h"

ZL_BEGIN_C_DECLS

/* Note : Global params are listed in ZL_DParam,
 *        defined within zstrong/zs2_decompress.h
 */

#include "openzl/zl_decompress.h" // ZL_DParam

typedef struct {
    int stickyParameters;
    ZL_TernaryParam checkCompressedChecksum;
    ZL_TernaryParam checkContentChecksum;
    ZL_TernaryParam enableCodecFusion;
} GDParams;

// All defaults for Global parameters
extern const GDParams GDParams_default;

ZL_Report
GDParams_setParameter(GDParams* gdparams, ZL_DParam gdparam, int value);

// Update @dst _only for_ values which are marked "default" (== 0) in @dst
// by using values from @defaults instead
void GDParams_applyDefaults(GDParams* dst, const GDParams* defaults);

/// Finalizes the parameters, and validates that they are correctly set.
/// Parameters that are incompatible are resolved where possible, and if
/// they cannot be resolved, an error is returned.
///
/// @returns An error if the parameters are invalid, otherwise success,
/// and the parameters are validated.
ZL_Report GDParams_finalize(GDParams* gdparams);

// Read values
int GDParams_getParameter(const GDParams* gdparams, ZL_DParam paramId);

// Copy GDParams from one instance to another.
void GDParams_copy(GDParams* dst, const GDParams* src);

ZL_END_C_DECLS

#endif // ZSTRONG_GDPARAMS_H
