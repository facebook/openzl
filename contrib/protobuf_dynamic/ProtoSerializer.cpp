// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "tools/protobuf/ProtoSerializer.h"
#include <google/protobuf/reflection.h>
#include "openzl/codecs/zl_clustering.h"
#include "openzl/common/stream.h"
#include "openzl/cpp/Input.hpp"
#include "tools/protobuf/StringWriter.h"
#include "tools/protobuf/Types.h"

namespace openzl {
namespace protobuf {
namespace {
class InputCopy : public Input {
   public:
    explicit InputCopy(ZL_Input* input)
            : Input(ZL_codemodMutDataAsInput(
                            STREAM_create(ZL_DATA_ID_INPUTSTREAM)),
                    ZL_TypedRef_free)
    {
        openzl::unwrap(
                STREAM_copy(
                        ZL_codemodMutInputAsData(get()),
                        ZL_codemodInputAsData(input)),
                "Failed to copy input data");
    }
};

/**
 * Create an Input from a given buffer, templated on the InputType.
 */
struct CreateInput {
    template <InputType Type>
    Input operator()(const void* buf, const size_t size)
    {
        auto zl_type = InputTraits<Type>::zl_type;
        auto width   = InputTraits<Type>::elm_width;
        if (zl_type == ZL_Type_serial) {
            return Input::refSerial(buf, size);
        }
        if (zl_type == ZL_Type_numeric) {
            return Input::refNumeric(buf, width, size / width);
        }
        throw std::runtime_error("Unsupported type");
    }
};

/**
 * Write a primitive value to the given writers based on its corresponding
 * InputType.
 */
template <InputType Type>
size_t write(
        const typename InputTraits<Type>::type& val,
        std::vector<std::unique_ptr<StringWriter>>& writers)
{
    using T = typename InputTraits<Type>::type;
    if constexpr (std::is_same_v<T, std::string>) {
        auto total = write<InputType::FIELD_LENGTH>(val.size(), writers);
        writers.at((int)Type)->write(val);
        return val.size() + total;
    } else {
        writers.at((int)Type)->writeLE(val);
        return sizeof(T);
    }
}

/**
 * Write a primitive field to the given writers based on its corresponding
 * InputType.
 */
struct WriteField {
    template <InputType Type>
    size_t operator()(
            const Reflection* ref,
            const Message& message,
            const FieldDescriptor* field,
            std::vector<std::unique_ptr<StringWriter>>& writers)
    {
        using Traits = InputTraits<Type>;
        using T      = typename InputTraits<Type>::type;
        size_t total = 0;
        if (!field->is_repeated()) {
            auto val = (ref->*Traits::Get)(message, field);
            total += write<Type>(val, writers);
        } else {
            auto repeated = ref->GetRepeatedFieldRef<T>(message, field);
            total += write<InputType::FIELD_LENGTH>(repeated.size(), writers);
            for (const auto& val : repeated) {
                total += write<Type>(val, writers);
            }
        }
        return total;
    }
};

/**
 * Write a message to the given writers and return the total number of bytes
 * written.
 */
size_t writeMessage(
        const Message& message,
        std::vector<std::unique_ptr<StringWriter>>& writers)
{
    // Get the field descriptors for the message
    const auto ref = message.GetReflection();
    std::vector<const FieldDescriptor*> fields;
    ref->ListFields(message, &fields);

    size_t total = 0;
    for (auto& field : fields) {
        total += write<InputType::FIELD_ID>(field->number(), writers);
        total += write<InputType::FIELD_TYPE>(field->cpp_type(), writers);

        // Handle primitive types
        if (field->cpp_type() != FieldDescriptor::CPPTYPE_MESSAGE) {
            total +=
                    call(CPPTypeToInputType[field->cpp_type()],
                         WriteField{},
                         ref,
                         message,
                         field,
                         writers);
            continue;
        }

        // Handle nested messages
        if (!field->is_repeated()) {
            total += writeMessage(ref->GetMessage(message, field), writers);
        } else {
            auto repeated = ref->GetRepeatedFieldRef<Message>(message, field);
            total += write<InputType::FIELD_LENGTH>(repeated.size(), writers);
            for (const auto& val : repeated) {
                total += writeMessage(val, writers);
            }
        }
    }
    total += write<InputType::FIELD_TYPE>(kStop, writers);
    return total;
}

using InputsAndBufs = std::
        pair<std::vector<Input>, std::vector<std::unique_ptr<std::string>>>;

InputsAndBufs getInputs(const Message& message)

{
    // Initialize the output queues and writers
    std::vector<std::unique_ptr<StringWriter>> writers(kNumInputs);
    std::vector<InputType> types(kNumInputs);
    for (size_t i = 0; i < kNumInputs; i++) {
        writers[i] = std::make_unique<StringWriter>();
        types[i]   = InputType(i);
    }

    auto size = writeMessage(message, writers);
    (void)size;

    // Coalesce the buffers and create the inputs
    InputsAndBufs result;
    for (size_t i = 0; i < kNumInputs; ++i) {
        auto buf = std::make_unique<std::string>(writers[i]->move());
        result.first.emplace_back(
                call(types[i], CreateInput{}, buf->data(), buf->size()));
        // TODO: T235159924 For the type split implementation we'll just tag
        // inputs with the input idx. Eventually we want to use the path here.
        result.first[i].setIntMetadata(ZL_CLUSTERING_TAG_METADATA_ID, i);
        result.second.emplace_back(std::move(buf));
    }

    return result;
};
} // namespace

std::vector<Input> ProtoSerializer::getTrainingInputs(const Message& message)
{
    auto [inputs, bufs] = getInputs(message);
    auto copied         = std::vector<Input>();
    for (size_t i = 0; i < inputs.size(); ++i) {
        copied.emplace_back(InputCopy(inputs[i].get()));
    }
    return copied;
};

std::string ProtoSerializer::serialize(const Message& message)
{
    auto [inputs, bufs] = getInputs(message);
    return cctx_.compress(inputs);
};
} // namespace protobuf
} // namespace openzl
