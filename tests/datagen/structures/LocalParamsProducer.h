// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <optional>

#include "tests/datagen/DataProducer.h"
#include "tests/datagen/random_producer/RandWrapper.h"
#include "tests/local_params_utils.h"

namespace openzl::tests::datagen {

class LocalParamsProducer : public DataProducer<LocalParams> {
   public:
    explicit LocalParamsProducer(std::shared_ptr<RandWrapper> generator)
            : DataProducer<LocalParams>(), rw_(std::move(generator))
    {
    }

    LocalParams operator()(RandWrapper::NameType) override
    {
        return makeRandomLocalParams();
    }

    void print(std::ostream& os) const override
    {
        os << "LocalParamsProducer()";
    }

    int makeRandomParamId();

    ZL_IntParam makeRandomIntParam();

    ZL_CopyParam makeRandomCopyParam();

    ZL_RefParam makeRandomRefParam();

    LocalParams makeRandomLocalParams();

    LocalParams mutateParamsPreservingEquality(const LocalParams& orig);

    void mutateParamValuePerturbingEquality(ZL_IntParam& p);

    void mutateParamValuePerturbingEquality(ZL_CopyParam& p);

    void mutateParamValuePerturbingEquality(ZL_RefParam& p);

    LocalParams mutateParamsPerturbingEquality(const LocalParams& orig);

   private:
    template <typename P>
    P makeRandomParam();

    template <typename P>
    void mutateParamsPreservingEquality(
            LocalParams& out,
            const std::vector<P>& in);

    template <typename P>
    void mutateParamsPerturbingEquality(
            LocalParams& out,
            const std::vector<P>& in);

   private:
    std::shared_ptr<RandWrapper> rw_;
};

} // namespace openzl::tests::datagen
