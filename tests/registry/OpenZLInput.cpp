// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/registry/OpenZLInput.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"

namespace openzl::tests {
namespace {
std::string serializeInput(const Input& input)
{
    assert(ZL_isLittleEndian());
    auto type = input.type();
    std::string serialized;
    const uint64_t numElts     = input.numElts();
    const uint64_t eltWidth    = input.eltWidth();
    const uint64_t contentSize = input.contentSize();
    serialized.push_back(char(type));
    serialized.append((const char*)&numElts, sizeof(numElts));
    serialized.append((const char*)&eltWidth, sizeof(eltWidth));
    serialized.append((const char*)&contentSize, sizeof(contentSize));
    serialized.append((const char*)input.ptr(), contentSize);
    if (type == Type::String) {
        serialized.append(
                (const char*)input.stringLens(),
                numElts * sizeof(input.stringLens()[0]));
    }
    return serialized;
}

std::unique_ptr<OpenZLInput> deserializeInput(std::string_view& serialized)
{
    constexpr size_t kHeaderSize = sizeof(char) + 3 * sizeof(uint64_t);
    assert(ZL_isLittleEndian());
    if (serialized.size() < kHeaderSize) {
        throw std::runtime_error("Corrupted input: not enough data for header");
    }

    auto type = Type(serialized[0]);
    serialized.remove_prefix(1);

    uint64_t numElts;
    memcpy(&numElts, serialized.data(), sizeof(numElts));
    serialized.remove_prefix(sizeof(numElts));

    uint64_t eltWidth;
    memcpy(&eltWidth, serialized.data(), sizeof(eltWidth));
    serialized.remove_prefix(sizeof(eltWidth));

    uint64_t contentSize;
    memcpy(&contentSize, serialized.data(), sizeof(contentSize));
    serialized.remove_prefix(sizeof(contentSize));

    if (serialized.size() < contentSize) {
        throw std::runtime_error(
                "Corrupted input: not enough data for content");
    }

    std::string content(serialized.data(), contentSize);
    serialized.remove_prefix(contentSize);

    switch (type) {
        case Type::Serial:
            return SerialOpenZLInput::make(std::move(content));
        case Type::Numeric: {
            if (numElts * eltWidth != contentSize) {
                throw std::runtime_error(
                        "Corrupted input: invalid numeric dimensions");
            }
            return makeNumericInput(std::move(content), eltWidth);
        }
        case Type::Struct: {
            if (numElts * eltWidth != contentSize) {
                throw std::runtime_error(
                        "Corrupted input: invalid struct dimensions");
            }
            return StructOpenZLInput::make(std::move(content), eltWidth);
        }
        case Type::String: {
            size_t lensSize = numElts * sizeof(uint32_t);
            if (serialized.size() < lensSize) {
                throw std::runtime_error(
                        "Corrupted input: not enough data for string lengths");
            }
            std::vector<uint32_t> lens(numElts);
            if (numElts != 0) {
                memcpy(lens.data(), serialized.data(), lensSize);
            }
            serialized.remove_prefix(lensSize);
            return StringOpenZLInput::make(std::move(content), std::move(lens));
        }
        default:
            throw std::runtime_error("Corrupted input: unknown type");
    }
}
} // namespace

std::string OpenZLInput::serialize() const
{
    const auto ins = inputs();
    return serialize(ins);
}

/* static */ std::string OpenZLInput::serialize(poly::span<const Input> inputs)
{
    std::string serialized;
    for (auto& in : inputs) {
        serialized += serializeInput(in);
    }
    return serialized;
}

/* static */ std::unique_ptr<OpenZLInput> OpenZLInput::deserialize(
        std::string_view serialized)
{
    std::vector<std::unique_ptr<OpenZLInput>> inputs;
    while (!serialized.empty()) {
        auto input = deserializeInput(serialized);
        inputs.push_back(std::move(input));
    }
    if (inputs.size() == 1) {
        return std::move(inputs[0]);
    } else {
        return MultiOpenZLInput::make(std::move(inputs));
    }
}

} // namespace openzl::tests
