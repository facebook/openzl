// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/datagen/structures/LocalParamsProducer.h"

#include <limits.h>
#include <algorithm>
#include <random>
#include <set>

#include "tests/utils.h"

namespace openzl {
namespace tests {
namespace datagen {

int LocalParamsProducer::makeRandomParamId()
{
    int id = rw_->range("param_id", 0, 15);
    switch (id) {
        case 10:
            return INT_MIN;
        case 11:
            return INT_MIN + 1;
        case 12:
            return INT_MAX - 1;
        case 13:
            return INT_MAX;
        default:
            return id;
    }
}

ZL_IntParam LocalParamsProducer::makeRandomIntParam()
{
    ZL_IntParam p;
    p.paramId    = makeRandomParamId();
    p.paramValue = rw_->range("int_param_value", 0, 127);
    return p;
}

ZL_CopyParam LocalParamsProducer::makeRandomCopyParam()
{
    ZL_CopyParam p;
    p.paramId = makeRandomParamId();
    EXPECT_GT(kLoremTestInput.size(), 256 + 2);
    p.paramPtr = kLoremTestInput.data()
            + rw_->range("copy_param_value_offset", 0, 127);
    p.paramSize = rw_->range("copy_param_size", 0, 127);
    return p;
}

ZL_RefParam LocalParamsProducer::makeRandomRefParam()
{
    ZL_RefParam p;
    p.paramId  = makeRandomParamId();
    p.paramRef = kLoremTestInput.data()
            + rw_->range("ref_param_value_offset", 0, 127);
    return p;
}

template <>
ZL_IntParam LocalParamsProducer::makeRandomParam<ZL_IntParam>()
{
    return makeRandomIntParam();
}

template <>
ZL_CopyParam LocalParamsProducer::makeRandomParam<ZL_CopyParam>()
{
    return makeRandomCopyParam();
}

template <>
ZL_RefParam LocalParamsProducer::makeRandomParam<ZL_RefParam>()
{
    return makeRandomRefParam();
}

LocalParams LocalParamsProducer::makeRandomLocalParams()
{
    LocalParams lp;
    const auto nbIntParams  = rw_->range("num_int_params", 0u, 15u);
    const auto nbCopyParams = rw_->range("num_copy_params", 0u, 15u);
    const auto nbRefParams  = rw_->range("num_ref_params", 0u, 15u);

    for (size_t i = 0; i < nbIntParams; i++) {
        const auto p = makeRandomIntParam();
        lp.push_back(p);
    }
    for (size_t i = 0; i < nbCopyParams; i++) {
        const auto p = makeRandomCopyParam();
        lp.push_back(p);
    }
    for (size_t i = 0; i < nbRefParams; i++) {
        const auto p = makeRandomRefParam();
        lp.push_back(p);
    }

    return lp;
}

template <typename P>
void LocalParamsProducer::mutateParamsPreservingEquality(
        LocalParams& out,
        const std::vector<P>& in)
{
    std::vector<P> firsts;
    std::vector<P> repeats;
    std::set<int> seen;
    for (const auto& p : in) {
        const auto ret = seen.insert(p.paramId);
        if (ret.second) {
            firsts.push_back(p);
        } else {
            repeats.push_back(p);
        }
    }

    std::mt19937 urbg(rw_->u32("param_shuffle_seed"));
    std::shuffle(firsts.begin(), firsts.end(), urbg);
    std::shuffle(repeats.begin(), repeats.end(), urbg);

    const auto repeats_to_add =
            rw_->range("param_repeats_to_add", (size_t)0, repeats.size());
    const auto repeats_to_keep =
            rw_->range("param_repeats_to_keep", (size_t)0, repeats.size());
    repeats.resize(repeats_to_keep);
    for (auto& p : repeats) {
        auto srcIdx = rw_->range(
                "param_repeat_keep_src_idx", (size_t)0, firsts.size() - 1);
        const auto& src = firsts[srcIdx];
        p.paramId       = src.paramId;
    }
    for (size_t i = 0; i < repeats_to_add; i++) {
        auto p      = makeRandomParam<P>();
        auto srcIdx = rw_->range(
                "param_repeat_add_src_idx", (size_t)0, firsts.size() - 1);
        const auto& src = firsts[srcIdx];
        p.paramId       = src.paramId;
        repeats.push_back(p);
    }

    for (const auto& p : firsts) {
        out.push_back(p);
    }
    for (const auto& p : repeats) {
        out.push_back(p);
    }
}

LocalParams LocalParamsProducer::mutateParamsPreservingEquality(
        const LocalParams& orig)
{
    LocalParams lp;

    mutateParamsPreservingEquality(lp, orig.intParams());
    mutateParamsPreservingEquality(lp, orig.copyParams());
    mutateParamsPreservingEquality(lp, orig.refParams());

    return lp;
}

void LocalParamsProducer::mutateParamValuePerturbingEquality(ZL_IntParam& p)
{
    p.paramValue++;
}

void LocalParamsProducer::mutateParamValuePerturbingEquality(ZL_CopyParam& p)
{
    // TODO: originally this did something fancier. But it didn't work because
    // copy params are... copied into the engine and no longer point to the
    // same buffer we sourced the param from and therefore can't be shifted
    // or grown in the same way.
    auto new_p    = makeRandomCopyParam();
    new_p.paramId = p.paramId;
    if (new_p.paramSize == p.paramSize
        && !memcmp(new_p.paramPtr, p.paramPtr, new_p.paramSize)) {
        if (new_p.paramSize) {
            new_p.paramSize -= 1;
        } else {
            new_p.paramPtr = ((const char*)new_p.paramPtr) + 1;
        }
    }
    p = new_p;
}

void LocalParamsProducer::mutateParamValuePerturbingEquality(ZL_RefParam& p)
{
    p.paramRef = ((const char*)p.paramRef) + 1;
}

template <typename P>
static int nextFreeParamID(const std::vector<P>& ps)
{
    int pid = 0;
    while (true) {
        bool used = false;
        for (const auto& p : ps) {
            if (p.paramId == pid) {
                used = true;
                break;
            }
        }
        if (used) {
            pid++;
            continue;
        }
        break;
    }
    return pid;
}

template <typename P>
void LocalParamsProducer::mutateParamsPerturbingEquality(
        LocalParams& out,
        const std::vector<P>& in)
{
    std::vector<P> ps = in;
    const auto idxToPerturb =
            rw_->range("param_perturb_idx", (size_t)0, ps.size());
    if (idxToPerturb == ps.size()) {
        auto new_p    = makeRandomParam<P>();
        new_p.paramId = nextFreeParamID(ps);
        ps.push_back(new_p);
    } else {
        auto* p = &ps[idxToPerturb];
        for (auto& o : ps) {
            if (o.paramId == p->paramId) {
                p = &o;
                break;
            }
        }
        if (rw_->boolean("param_perturb_should_change_id")) {
            p->paramId = nextFreeParamID(ps);
        } else {
            mutateParamValuePerturbingEquality(*p);
        }
    }

    for (const auto& p : ps) {
        out.push_back(p);
    }
}

LocalParams LocalParamsProducer::mutateParamsPerturbingEquality(
        const LocalParams& orig)
{
    LocalParams lp{ *orig };

    switch (rw_->range("which_kind_of_param_to_perturb", 0, 2)) {
        case 0:
            lp.clearIntParams();
            mutateParamsPerturbingEquality(lp, orig.intParams());
            break;
        case 1:
            lp.clearCopyParams();
            mutateParamsPerturbingEquality(lp, orig.copyParams());
            break;
        case 2:
            lp.clearRefParams();
            mutateParamsPerturbingEquality(lp, orig.refParams());
            break;
    }

    return lp;
}

} // namespace datagen
} // namespace tests
} // namespace openzl
