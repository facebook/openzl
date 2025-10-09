// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_COMPRESS_SELECTORS_ML_FEATURES_H
#define ZSTRONG_COMPRESS_SELECTORS_ML_FEATURES_H

#include "openzl/common/vector.h" //For VECTOR type
#include "openzl/shared/portability.h"
#include "openzl/zl_data.h" //For feature generator
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

/**
 * Defines type labeled features which is returned by the feature generator.
 */
typedef struct {
    const char* label;
    float value;
} LabeledFeature;
DECLARE_VECTOR_TYPE(LabeledFeature)

/**
 * Calculates the basic features for numeric data, assuming that the data is
 * unsigned integers.
 * Note: Calculates sample variance, sample skewness and sample kurtosis (not
 * population!).
 * @returns ZL_REPORT specifying success or failure of generating feature
 * operation.
 */
ZL_Report FeatureGen_integer(
        const ZL_Input* inputStream,
        VECTOR(LabeledFeature) * features,
        const void* featureContext);

/**
 * Defines type Feature Generator, which takes a stream and generates various
 * features from it. These features are then pushed into the
 * features vector parameter.
 * @returns ZL_REPORT specifying success or failure of generating feature
 */
typedef ZL_Report (*FeatureGenerator)(
        const ZL_Input* inputStream,
        VECTOR(LabeledFeature) * features,
        const void* featureContext);

ZL_END_C_DECLS

#endif
