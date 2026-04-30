// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/local_params_utils.h"

#include <set>

#include <gtest/gtest.h>

#include "openzl/compress/localparams.h"

namespace openzl {
namespace tests {

ZL_INLINE bool matches(const ZL_IntParam& lhs, const ZL_IntParam& rhs)
{
    return lhs.paramId == rhs.paramId && lhs.paramValue == rhs.paramValue;
}

ZL_INLINE bool matches(const ZL_CopyParam& lhs, const ZL_CopyParam& rhs)
{
    return lhs.paramId == rhs.paramId
            && std::string_view((const char*)lhs.paramPtr, lhs.paramSize)
            == std::string_view((const char*)rhs.paramPtr, rhs.paramSize);
}

ZL_INLINE bool matches(const ZL_RefParam& lhs, const ZL_RefParam& rhs)
{
    return lhs.paramId == rhs.paramId && lhs.paramRef == rhs.paramRef;
}

template <typename P>
ZL_INLINE bool matches(const std::vector<P>& l, const std::vector<P>& r)
{
    std::set<int> ids;
    for (const auto& p : l) {
        ids.insert(p.paramId);
    }
    for (const auto& p : r) {
        ids.insert(p.paramId);
    }
    for (const auto id : ids) {
        const P* lp = nullptr;
        const P* rp = nullptr;
        for (const auto& p : l) {
            if (p.paramId == id) {
                lp = &p;
                break;
            }
        }
        for (const auto& p : r) {
            if (p.paramId == id) {
                rp = &p;
                break;
            }
        }
        if (lp == nullptr || rp == nullptr) {
            if (lp != rp) {
                return false;
            }
        } else {
            if (!matches(*lp, *rp)) {
                return false;
            }
        }
    }
    return true;
}

/**
 * Reference implementation of equality check to validate C impl against.
 */
bool LocalParams_match(const LocalParams& lhs, const LocalParams& rhs)
{
    return matches(lhs.intParams(), rhs.intParams())
            && matches(lhs.copyParams(), rhs.copyParams())
            && matches(lhs.refParams(), rhs.refParams());
}

void LocalParams_check_match_consistency(
        const LocalParams& lhs,
        const LocalParams& rhs,
        std::optional<bool> should_match)
{
    const auto eq    = ZL_LocalParams_eq(&*lhs, &*rhs);
    const auto lh    = ZL_LocalParams_hash(&*lhs);
    const auto rh    = ZL_LocalParams_hash(&*rhs);
    const auto match = LocalParams_match(lhs, rhs);
    EXPECT_EQ(match, should_match.value_or(match)) << lhs << ",\n" << rhs;
    EXPECT_EQ(match, eq) << lhs << ",\n" << rhs;
    if (match) {
        EXPECT_EQ(lh, rh) << lhs << ",\n" << rhs;
    } else {
        EXPECT_NE(lh, rh) << lhs << ",\n" << rhs;
    }
}

void LocalParams_check_eq(const LocalParams& lhs, const LocalParams& rhs)
{
    LocalParams_check_match_consistency(lhs, rhs, true);
}

void LocalParams_check_ne(const LocalParams& lhs, const LocalParams& rhs)
{
    LocalParams_check_match_consistency(lhs, rhs, false);
}

std::ostream& operator<<(std::ostream& os, const LocalParams& lp)
{
    os << "(ZL_LocalParams){\n";
    if (!lp.intParams().empty()) {
        os << "  .intParams = {\n";
        for (const auto& ip : lp.intParams()) {
            os << "    " << ip.paramId << ": " << ip.paramValue << ",\n";
        }
        os << "  },\n";
    }
    if (!lp.copyParams().empty()) {
        os << "  .copyParams = {\n";
        for (const auto& cp : lp.copyParams()) {
            os << "    " << cp.paramId << ": (" << cp.paramPtr << ", "
               << cp.paramSize << "),\n";
        }
        os << "  },\n";
    }
    if (!lp.refParams().empty()) {
        os << "  .refParams = {\n";
        for (const auto& rp : lp.refParams()) {
            os << "    " << rp.paramId << ": " << rp.paramRef << ",\n";
        }
        os << "  },\n";
    }
    os << "}";
    return os;
}

} // namespace tests
} // namespace openzl
