// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <google/protobuf/message.h>
#include <google/protobuf/reflection.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/message_differencer.h>
#include <filesystem>
#include <iomanip>
#include "custom_parsers/dependency_registration.h"
#include "openzl/common/assertion.h"
#include "openzl/common/logging.h"
#include "tools/arg/arg_parser.h"
#include "tools/io/InputFile.h"
#include "tools/io/InputSet.h"
#include "tools/io/InputSetBuilder.h"
#include "tools/io/OutputFile.h"
#include "tools/protobuf/DescriptorLoader.h"
#include "tools/protobuf/DynamicMessageHelper.h"
#include "tools/protobuf/ProtoDeserializer.h"
#include "tools/protobuf/ProtoSerializer.h"
#include "tools/protobuf/serialization_utils.h"
#ifdef ZL_IS_FBCODE
#    if ZL_FBCODE_IS_RELEASE
#        include "openzl/prod/tools/protobuf/schema.pb.h"
#    else
#        include "openzl/dev/tools/protobuf/schema.pb.h"
#    endif
#else
#    include "tools/protobuf/schema.pb.h"
#endif
#include "tools/training/train.h"
#include "tools/training/train_params.h"

namespace openzl {
namespace protobuf {
namespace {
using MessageDifferencer = google::protobuf::util::MessageDifferencer;
using JsonPrintOptions   = google::protobuf::util::JsonPrintOptions;
using JsonParseOptions   = google::protobuf::util::JsonParseOptions;
using nano               = std::chrono::nanoseconds;

std::string kInput       = "input";
std::string kOutput      = "output";
std::string kInputType   = "input-protocol";
std::string kOutputType  = "output-protocol";
std::string kCheck       = "check";
std::string kNumIters    = "num-iters";
std::string kCompressor  = "compressor";
std::string kProto       = "proto";
std::string kDescriptor  = "descriptor";
std::string kProtoPath   = "proto-path";
std::string kMessageType = "message-type";

enum Cmd : int {
    UNSPECIFIED = 0,
    SERIALIZE   = 1,
    BENCHMARK   = 2,
    TRAIN       = 3,
};

Protocol parseProtocol(const std::string& protocol)
{
    if (protocol == "proto") {
        return Protocol::Proto;
    } else if (protocol == "zl") {
        return Protocol::ZL;
    } else if (protocol == "json") {
        return Protocol::JSON;
    } else {
        throw std::runtime_error("Unrecognized protocol: " + protocol);
    }
}

std::string toString(Protocol protocol)
{
    switch (protocol) {
        case Protocol::Proto:
            return "proto";
        case Protocol::ZL:
            return "zl";
        case Protocol::JSON:
            return "json";
    }
    throw std::runtime_error("Invalid protocol!");
}

std::string ext(Protocol protocol)
{
    switch (protocol) {
        case Protocol::Proto:
            return "pb";
        case Protocol::ZL:
            return "zl";
        case Protocol::JSON:
            return "json";
    }
    throw std::runtime_error("Invalid protocol!");
}

/**
 * @brief This class is used to store the global arguments passed to the CLI
 * tool.
 */
class Args {
   public:
    explicit Args(openzl::arg::ParsedArgs& args)
    {
        inputs = tools::io::InputSetBuilder(false)
                         .add_path(args.globalRequiredFlag(kInput))
                         .build();
        inputType =
                parseProtocol(args.globalFlag(kInputType).value_or("proto"));
        if (args.globalHasFlag(kCompressor)) {
            auto file = std::make_unique<tools::io::InputFile>(
                    args.globalRequiredFlag(kCompressor));
            Compressor compressor;
            compressor.deserialize(file->contents());
            serializer.setCompressor(std::move(compressor));
        }

        // See if user wants to use a dynamic schema
        bool has_proto      = args.globalHasFlag(kProto);
        bool has_descriptor = args.globalHasFlag(kDescriptor);

        if (has_proto || has_descriptor) {
            use_dynamic = true;

            if (has_proto && has_descriptor) {
                throw std::runtime_error(
                        "Cannot specify both --proto and --descriptor");
            }

            if (!args.globalHasFlag(kMessageType)) {
                throw std::runtime_error(
                        "--message-type is required when using --proto or --descriptor");
            }

            message_type = args.globalRequiredFlag(kMessageType);

            auto loader = std::make_unique<DescriptorLoader>();
            if (args.globalHasFlag(kProtoPath)) {
                loader->addProtoPath(args.globalRequiredFlag(kProtoPath));
            }

            if (has_proto) {
                descriptor_pool =
                        loader->loadProtoFile(args.globalRequiredFlag(kProto));
            } else {
                descriptor_pool = loader->loadDescriptorFile(
                        args.globalRequiredFlag(kDescriptor));
            }

            if (!descriptor_pool) {
                throw std::runtime_error("Failed to load protobuf schema");
            }

            message_helper = std::make_unique<DynamicMessageHelper>(
                    descriptor_pool.get());
        } else {
            // We will use the schema compiled with CLI source code
            use_dynamic = false;
        }
    };

    std::unique_ptr<google::protobuf::Message> newMessage() const
    {
        if (use_dynamic) {
            return message_helper->newMessage(message_type);
        } else {
            return std::make_unique<Schema>();
        }
    }

    std::unique_ptr<tools::io::InputSet> inputs;
    Protocol inputType;
    ProtoSerializer serializer;
    ProtoDeserializer deserializer;

    // Dynamic schema members
    bool use_dynamic = false;
    std::string message_type;
    std::shared_ptr<const google::protobuf::DescriptorPool> descriptor_pool;
    std::unique_ptr<DynamicMessageHelper> message_helper;
};

class BenchmarkArgs : public Args {
   public:
    explicit BenchmarkArgs(openzl::arg::ParsedArgs& args) : Args(args)
    {
        if (args.cmdHasFlag(Cmd::BENCHMARK, kNumIters)) {
            numIters =
                    std::stoi(args.cmdFlag(Cmd::BENCHMARK, kNumIters).value());
        }
    }

    size_t numIters = 10;
};

class SerializeArgs : public Args {
   public:
    explicit SerializeArgs(openzl::arg::ParsedArgs& args) : Args(args)
    {
        outputType = parseProtocol(
                args.cmdFlag(Cmd::SERIALIZE, kOutputType).value_or("zl"));

        check  = args.cmdHasFlag(Cmd::SERIALIZE, kCheck);
        output = args.cmdFlag(Cmd::SERIALIZE, kOutput);
    }

    Protocol outputType;
    bool check;
    std::optional<std::string> output;
};

class TrainArgs : public Args {
   public:
    explicit TrainArgs(openzl::arg::ParsedArgs& args) : Args(args)
    {
        output = std::make_unique<tools::io::OutputFile>(
                args.cmdRequiredFlag(Cmd::TRAIN, kOutput));
    }
    std::unique_ptr<tools::io::OutputFile> output;
};

void updateResults(
        size_t iter_count,
        const std::array<size_t, 2>& serialized_size,
        const std::array<nano, 2>& cdur,
        const std::array<nano, 2>& ddur)
{
    constexpr size_t BYTES_TO_MiB = 1024 * 1024;

    const auto uncompressed_size =
            serialized_size[static_cast<int>(Protocol::Proto)];

    for (auto protocol : { Protocol::Proto, Protocol::ZL }) {
        const auto ratio = static_cast<double>(uncompressed_size)
                / serialized_size[static_cast<int>(protocol)];

        const auto cmicros = std::chrono::duration<double, std::micro>(
                cdur[static_cast<int>(protocol)]);
        const auto dmicros = std::chrono::duration<double, std::micro>(
                ddur[static_cast<int>(protocol)]);

        const auto cmibps = (uncompressed_size * iter_count * 1000 * 1000.0)
                / (cmicros.count() * BYTES_TO_MiB);
        const auto dmibps = (uncompressed_size * iter_count * 1000 * 1000.0)
                / (dmicros.count() * BYTES_TO_MiB);

        std::cout << toString(protocol) << ": " << uncompressed_size << " -> "
                  << serialized_size[static_cast<int>(protocol)] << " ("
                  << std::setprecision(2) << std::fixed << ratio << "),  "
                  << cmibps << " MiB/s  " << dmibps << " MiB/s\n";
    }
    std::cout << std::flush;
}

int handleBenchmark(BenchmarkArgs args)
{
    std::array<size_t, 2> serialized_size = { 0, 0 };
    std::array<nano, 2> cdur              = { nano::zero(), nano::zero() };
    std::array<nano, 2> ddur              = { nano::zero(), nano::zero() };

    size_t total_inputs = 0;
    for (auto& input : *args.inputs) {
        total_inputs++;
        // Deserialize object with the input protocol
        const auto contents = std::string(input->contents());
        auto obj            = args.newMessage();
        deserialize(contents, args.inputType, args.deserializer, *obj);
        for (auto protocol : { Protocol::Proto, Protocol::ZL }) {
            // Get the serialized size of the object with the chosen
            // protocol
            auto serialized   = serialize(*obj, protocol, args.serializer);
            auto deserialized = args.newMessage();
            deserialize(serialized, protocol, args.deserializer, *deserialized);
            serialized_size[static_cast<int>(protocol)] += serialized.size();

            // Check if the round trip is correct
            ZL_REQUIRE(
                    MessageDifferencer::Equivalent(*obj, *deserialized),
                    "Round trip check failed!");

            // Benchmark serialization and deserialization speeds
            const auto serialization_start = std::chrono::steady_clock::now();
            for (size_t n = 0; n < args.numIters; ++n) {
                auto val = serialize(*deserialized, protocol, args.serializer);
            }
            const auto serialization_end     = std::chrono::steady_clock::now();
            const auto deserialization_start = std::chrono::steady_clock::now();
            for (size_t n = 0; n < args.numIters; ++n) {
                auto temp = args.newMessage();
                deserialize(serialized, protocol, args.deserializer, *temp);
            }
            const auto deserialization_end = std::chrono::steady_clock::now();
            cdur[static_cast<int>(protocol)] +=
                    serialization_end - serialization_start;
            ddur[static_cast<int>(protocol)] +=
                    deserialization_end - deserialization_start;
        }
        updateResults(args.numIters, serialized_size, cdur, ddur);
    }
    std::cout << std::endl;

    if (total_inputs == 0) {
        throw std::runtime_error("No samples found in inputs");
    }
    return 0;
}

int handleSerialize(SerializeArgs args)
{
    for (auto& input : *args.inputs) {
        const std::string contents = std::string(std::move(input->contents()));

        // Deserialize and serialize the protobuf object with the chosen
        // protocol
        auto obj = args.newMessage();
        deserialize(contents, args.inputType, args.deserializer, *obj);
        auto serialized = serialize(*obj, args.outputType, args.serializer);
        ZL_LOG(ALWAYS, "Serialized to %d bytes!", serialized.size());

        // Check if the round trip is correct
        if (args.check) {
            auto deserialized = args.newMessage();
            deserialize(
                    serialized,
                    args.outputType,
                    args.deserializer,
                    *deserialized);
            ZL_REQUIRE(
                    MessageDifferencer::Equivalent(*obj, *deserialized),
                    "Round trip check failed!");
            ZL_LOG(ALWAYS, "Round trip check passed!");
        };

        // Write the serialized object to a file
        std::filesystem::path path;
        if (!args.output) {
            path = std::filesystem::path(input->name());
            path.replace_extension(ext(args.outputType));
        } else {
            path = std::filesystem::path(args.output.value());
        }
        std::make_unique<tools::io::OutputFile>(path)->write(serialized);
    }
    return 0;
}

int handleTrain(TrainArgs args)
{
    std::vector<std::unique_ptr<google::protobuf::Message>> messages;
    for (auto& input : *args.inputs) {
        const auto contents = std::string(input->contents());
        auto message        = args.newMessage();
        deserialize(contents, args.inputType, args.deserializer, *message);
        messages.push_back(std::move(message));
    }

    std::vector<training::MultiInput> samples(messages.size());
    for (size_t i = 0; i < messages.size(); i++) {
        samples[i] = training::MultiInput(
                args.serializer.getTrainingInputs(*messages[i]));
    }

    auto compressor = args.serializer.getCompressor();

    auto params = training::TrainParams();
    params.compressorGenFunc =
            openzl::custom_parsers::createCompressorFromSerialized;

    auto serialized = training::train(samples, *compressor, params)[0];

    ZL_LOG(ALWAYS,
           "Writing trained compressor to %s",
           std::string(args.output->name()).c_str());
    args.output->write(*serialized);

    return 0;
}
} // namespace
} // namespace protobuf
} // namespace openzl

using namespace openzl::protobuf;
/**
 * @brief This CLI tool can be used to convert between protobuf default
 * serialization and OpenZL serialization for protobuf messages.
 */
int main(int argc, char** argv)
{
    openzl::arg::ArgParser parser;

    // global flags
    parser.addGlobalFlag(kInput, 'i', true, "The input protobuf file");
    parser.addGlobalFlag(
            kInputType,
            't',
            true,
            "The input protocol used. Must be one of: proto, zl");
    parser.addGlobalFlag(
            kCompressor,
            'c',
            true,
            "An optional compressor to use for the ZL protocol.");

    // Dynamic schema arguments: Use these flags to work with protobuf schemas
    // that are not compiled into the binary. You can either provide a .proto
    // source file (--proto) or a pre-compiled descriptor file (--descriptor).
    // When using dynamic schemas, you must also specify the message type name
    // with --message-type. Optionally, use --proto-path to add import search
    // directories for .proto files.
    parser.addGlobalFlag(
            kProto,
            0,
            true,
            "Load schema from .proto file (for dynamic schemas)");
    parser.addGlobalFlag(
            kDescriptor,
            0,
            true,
            "Load schema from .desc file (for dynamic schemas)");
    parser.addGlobalFlag(
            kProtoPath, 0, true, "Directory to search for .proto imports");
    parser.addGlobalFlag(
            kMessageType,
            0,
            true,
            "Fully qualified message type name (required with --proto or --descriptor)");

    // serialize
    parser.addCommand(Cmd::SERIALIZE, "serialize", 's');
    parser.addCommandFlag(
            Cmd::SERIALIZE, kOutput, 'o', true, "The output protobuf file");
    parser.addCommandFlag(
            Cmd::SERIALIZE,
            kOutputType,
            'u',
            true,
            "The output protocol used. Must be one of: proto, zl");
    parser.addCommandFlag(
            Cmd::SERIALIZE,
            kCheck,
            'c',
            false,
            "Check if serialization round trip is correct.");

    // benchmark
    parser.addCommand(Cmd::BENCHMARK, "benchmark", 'b');
    parser.addCommandFlag(
            Cmd::BENCHMARK,
            kNumIters,
            'n',
            true,
            "The number of iterations to run for each file.");

    // train
    parser.addCommand(Cmd::TRAIN, "train", 't');
    parser.addCommandFlag(
            Cmd::TRAIN,
            kOutput,
            'o',
            true,
            "The output trained compressor file");

    auto args = parser.parse(argc, argv);

    switch (args.chosenCmd()) {
        case Cmd::SERIALIZE: {
            return openzl::protobuf::handleSerialize(SerializeArgs(args));
        }
        case Cmd::BENCHMARK: {
            return openzl::protobuf::handleBenchmark(BenchmarkArgs(args));
        }
        case Cmd::TRAIN: {
            return openzl::protobuf::handleTrain(TrainArgs(args));
        }
        default: {
            ZL_LOG(ALWAYS, "No command specified!");
            return 1;
        }
    }

    return 1;
}
