// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <optional>
#include <ostream>
#include <vector>

#include "openzl/zl_localParams.h"

namespace openzl {
namespace tests {

/**
 * Copied from zstrong_cpp.h, with modifications :/
 *
 * TODO(T223620666): unify with the version in D70812010 once that lands.
 */
class LocalParams {
   public:
    LocalParams() : params_() {}
    explicit LocalParams(ZL_LocalParams params)
            : params_(params),
              intStorage_(
                      params.intParams.intParams,
                      params.intParams.intParams
                              + params.intParams.nbIntParams),
              copyStorage_(
                      params.genericParams.copyParams,
                      params.genericParams.copyParams
                              + params.genericParams.nbCopyParams),
              refStorage_(
                      params.refParams.refParams,
                      params.refParams.refParams + params.refParams.nbRefParams)
    {
        params_.intParams.intParams     = intStorage_.data();
        params_.intParams.nbIntParams   = intStorage_.size();
        params_.copyParams.copyParams   = copyStorage_.data();
        params_.copyParams.nbCopyParams = copyStorage_.size();
        params_.refParams.refParams     = refStorage_.data();
        params_.refParams.nbRefParams   = refStorage_.size();
    }

    LocalParams(LocalParams const&) = delete;
    LocalParams(LocalParams&&)      = default;

    LocalParams& operator=(LocalParams const&) = delete;
    LocalParams& operator=(LocalParams&&)      = default;

    ZL_LocalParams const& operator*() const
    {
        return params_;
    }

    ZL_LocalParams const* operator->() const
    {
        return &params_;
    }

    LocalParams copy() const
    {
        return LocalParams{ **this };
    }

    const std::vector<ZL_RefParam>& refParams() const
    {
        return refStorage_;
    }

    const std::vector<ZL_CopyParam>& copyParams() const
    {
        return copyStorage_;
    }

    const std::vector<ZL_IntParam>& intParams() const
    {
        return intStorage_;
    }

    void push_back(ZL_IntParam param)
    {
        intStorage_.push_back(param);
        params_.intParams.intParams = intStorage_.data();
        ++params_.intParams.nbIntParams;
    }

    void push_back(int paramId, int paramValue)
    {
        const auto ip = (ZL_IntParam){
            .paramId    = paramId,
            .paramValue = paramValue,
        };
        push_back(ip);
    }

    void push_back(ZL_CopyParam param)
    {
        copyStorage_.push_back(param);
        params_.genericParams.copyParams = copyStorage_.data();
        ++params_.genericParams.nbCopyParams;
    }

    void push_back(ZL_RefParam param)
    {
        refStorage_.push_back(param);
        params_.refParams.refParams   = refStorage_.data();
        params_.refParams.nbRefParams = refStorage_.size();
    }

    void setIntParams(const std::vector<ZL_IntParam>& params)
    {
        clearIntParams();
        for (const auto& p : params) {
            push_back(p);
        }
    }

    void setCopyParams(const std::vector<ZL_CopyParam>& params)
    {
        clearCopyParams();
        for (const auto& p : params) {
            push_back(p);
        }
    }

    void setRefParams(const std::vector<ZL_RefParam>& params)
    {
        clearRefParams();
        for (const auto& p : params) {
            push_back(p);
        }
    }

    void clearIntParams()
    {
        intStorage_.clear();
        params_.intParams.intParams   = intStorage_.data();
        params_.intParams.nbIntParams = intStorage_.size();
    }

    void clearCopyParams()
    {
        copyStorage_.clear();
        params_.copyParams.copyParams   = copyStorage_.data();
        params_.copyParams.nbCopyParams = copyStorage_.size();
    }

    void clearRefParams()
    {
        refStorage_.clear();
        params_.refParams.refParams   = refStorage_.data();
        params_.refParams.nbRefParams = refStorage_.size();
    }

   private:
    ZL_LocalParams params_;
    std::vector<ZL_IntParam> intStorage_;
    std::vector<ZL_CopyParam> copyStorage_;
    std::vector<ZL_RefParam> refStorage_;
};

/**
 * Pretty-printer for local params.
 */
std::ostream& operator<<(std::ostream& os, const LocalParams& lp);

/**
 * Reference implementation of equality check for local params (against which
 * to validate C impl).
 */
bool LocalParams_match(const LocalParams& lhs, const LocalParams& rhs);

/**
 * Checks that two local params sets equality and hash matching match between
 * (1) the C impls, (2) the C++ impl, (3) `should_match`.
 */
void LocalParams_check_match_consistency(
        const LocalParams& lhs,
        const LocalParams& rhs,
        std::optional<bool> should_match = std::nullopt);

void LocalParams_check_eq(const LocalParams& lhs, const LocalParams& rhs);

void LocalParams_check_ne(const LocalParams& lhs, const LocalParams& rhs);

} // namespace tests
} // namespace openzl
