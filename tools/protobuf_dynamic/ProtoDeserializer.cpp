// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tools/protobuf/ProtoDeserializer.h"
#include <google/protobuf/message.h>
#include <sstream>
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/FrameInfo.hpp"
#include "openzl/cpp/Output.hpp"
#include "tools/protobuf/StringReader.h"

namespace openzl {
namespace protobuf {
namespace {

struct TypeWidth {
    template <InputType Type>
    size_t operator()()
    {
        using T = typename InputTraits<Type>::type;
        return sizeof(T);
    }
};

/**
 * Decompress a given string using the provided DCtx. Returns a vector of
 * IOBufs containing the decompressed data.
 */
std::vector<std::string> decompress(DCtx& dctx, const std::string& compressed)
{
    // Collect Frame info
    auto fi      = FrameInfo(compressed);
    auto numOuts = fi.numOutputs();

    // Allocate output buffers
    std::vector<std::string> bufs(numOuts);
    std::vector<Output> outs(numOuts);
    for (size_t n = 0; n < numOuts; n++) {
        auto const size  = fi.outputContentSize(n);
        auto const type  = fi.outputType(n);
        auto const width = call((InputType)n, TypeWidth{});
        bufs[n].resize(size);
        if (type == Type::Serial) {
            outs[n] = Output::wrapSerial(bufs[n].data(), size);
        } else if (type == Type::Numeric) {
            outs[n] = Output::wrapNumeric(bufs[n].data(), width, size / width);
        } else {
            throw std::runtime_error("Unexpected output type");
        }
    }

    // Decompress
    dctx.decompress(outs, compressed);
    return bufs;
};

/**
 * Read a primitive value from the given readers based on the input type.
 */
template <InputType Type>
size_t read(
        typename InputTraits<Type>::type& val,
        std::vector<std::unique_ptr<StringReader>>& readers)
{
    using Traits = InputTraits<Type>;
    using T      = typename Traits::type;
    if constexpr (std::is_same_v<T, std::string>) {
        uint32_t len = 0;
        auto total   = read<InputType::FIELD_LENGTH>(len, readers);
        readers.at((int)Type)->read(val, len);
        return total + len;
    } else {
        readers.at((int)Type)->readLE(val);
        return sizeof(T);
    }
}

/**
 * Read a primitive field from the given readers based on the input type.
 */
struct ReadField {
    template <InputType Type>
    size_t operator()(
            const Reflection* ref,
            Message& message,
            const FieldDescriptor* field,
            std::vector<std::unique_ptr<StringReader>>& readers)
    {
        using T      = typename InputTraits<Type>::type;
        size_t total = 0;
        if (!field->is_repeated()) {
            T val;
            total += read<Type>(val, readers);
            (ref->*InputTraits<Type>::Set)(&message, field, std::move(val));
        } else {
            uint32_t len;
            total += read<InputType::FIELD_LENGTH>(len, readers);
            auto repeated = ref->GetMutableRepeatedFieldRef<T>(&message, field);
            for (size_t i = 0; i < len; ++i) {
                T val;
                total += read<Type>(val, readers);
                repeated.Add(std::move(val));
            }
        }

        return total;
    }
};

/**
 * Populate a message from the given readers and return the total number of
 * bytes read.
 */
size_t readMessage(
        Message& message,
        std::vector<std::unique_ptr<StringReader>>& readers)
{
    // Get the field descriptors for the message
    const auto ref  = message.GetReflection();
    const auto desc = message.GetDescriptor();

    uint32_t total = 0;
    // Read the message fields
    while (!readers.at((int)InputType::FIELD_ID)->atEnd()) {
        uint32_t field_type;
        total += read<InputType::FIELD_TYPE>(field_type, readers);
        if (field_type == kStop) {
            return total;
        }

        uint32_t field_id;
        total += read<InputType::FIELD_ID>(field_id, readers);

        auto field = desc->FindFieldByNumber(field_id);
        if (field == nullptr) {
            std::ostringstream msg;
            msg << "Unknown field id " << field_id << " in " << desc->name();
            throw std::runtime_error(msg.str());
        }

        auto expected_type = field->cpp_type();
        if (field_type != expected_type) {
            std::ostringstream msg;
            msg << "Field type mismatch for field_id " << field_id << " name "
                << field->name() << " in " << desc->name() << ": expected "
                << static_cast<uint32_t>(expected_type) << "("
                << field->CppTypeName(static_cast<FieldDescriptor::CppType>(
                           expected_type));
            throw std::runtime_error(msg.str());
        }

        if (field_type != FieldDescriptor::CPPTYPE_MESSAGE) {
            total +=
                    call(CPPTypeToInputType[field_type],
                         ReadField{},
                         ref,
                         message,
                         field,
                         readers);
            continue;
        }

        // Handle nested messages
        if (!field->is_repeated()) {
            Message* nested = ref->MutableMessage(&message, field);
            total += readMessage(*nested, readers);
        } else {
            uint32_t len;
            total += read<InputType::FIELD_LENGTH>(len, readers);
            for (size_t i = 0; i < len; i++) {
                auto nested = ref->AddMessage(&message, field);
                total += readMessage(*nested, readers);
            }
        }
    }
    return total;
}
} // namespace

void ProtoDeserializer::deserialize(
        const std::string& serialized,
        Message& message)
{
    // Decompress the data
    auto buffers = decompress(dctx_, serialized);

    // Create readers
    std::vector<std::unique_ptr<StringReader>> readers(buffers.size());
    for (size_t i = 0; i < buffers.size(); i++) {
        readers.at(i) = std::make_unique<StringReader>(buffers[i]);
    };

    auto size = readMessage(message, readers);
    (void)size;
};

} // namespace protobuf
} // namespace openzl
