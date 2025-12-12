// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <array>

#include "tests/datagen/registry_records/RegistryRecord.h"

namespace openzl::tests::datagen {

class ZigzagRegistryRecord : public RegistryRecord {
   public:
    explicit ZigzagRegistryRecord() : RegistryRecord() {}

    std::string operator()(RandWrapper::NameType) override
    {
        return std::string(SAMPLES[idx++ % SAMPLES.size()]);
    }

    size_t size() const override
    {
        return SAMPLES.size();
    }

    void print(std::ostream& os) const override
    {
        os << "RegistryRecord(ZL_StandardNodeID_zigzag)";
    }

   private:
    size_t idx = 0;

    static constexpr std::array<std::string_view, 2ul> SAMPLES = {
        "\000\001\002\003\004\005\006\007\008\009\010\011\012\013",
        "\255\254\253\252\251\250\249\248\247\246\245\244\243\242",
    };
};

} // namespace openzl::tests::datagen
