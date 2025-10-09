// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/common/scope_context.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

ZL_Report ZL_test_errors_binary_arg_types_deduced_in_c_inner(
        ZL_ScopeContext scopeCtx);

ZL_END_C_DECLS
