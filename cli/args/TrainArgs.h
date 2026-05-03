// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <sstream>

#include "custom_parsers/dependency_registration.h"
#include "openzl/cpp/Compressor.hpp"

#include "tools/io/InputSetBuilder.h"
#include "tools/io/OutputFile.h"
#include "tools/training/train_params.h"

#include "cli/args/ArgsUtils.h"
#include "cli/args/GlobalArgs.h"
#include "cli/utils/util.h"

namespace openzl::cli {

class TrainArgs : public GlobalArgs, public ProfileArgs {
   public:
    static void addArgs(arg::ArgParser& parser)
    {
        // Add the command
        parser.addCommand(cmd(), "train", 't');

        // Add the top-level flags
        parser.addCommandPositional(
                cmd(), kSampleDir, "Directory containing samples to train on.");
        parser.addCommandFlag(
                cmd(),
                kCompressor,
                'c',
                true,
                "Train with the given serialized compressor file.");
        parser.addCommandFlag(
                cmd(),
                kOutput,
                'o',
                true,
                "Output file path for the trained compressor.");
        parser.addCommandFlag(
                cmd(), kForce, 'f', false, "Overwrite output file.");

        // Train Params
        parser.addCommandFlag(
                cmd(),
                kTrainer,
                't',
                true,
                "The trainer picked to use for training(full-split|greedy|bottom-up).\n"
                "By default uses greedy as the trainer. See {algo_name}_trainer.h for\n"
                "information on when to use each trainer.");
        parser.addCommandFlag(
                cmd(),
                kThreads,
                0,
                true,
                "Number of threads to allocate to the thread pool. If blank, defaults to hardware concurrency limit.");
        parser.addCommandFlag(
                cmd(),
                kNumSamples,
                0,
                true,
                "Chooses N samples from the directory provided where each file is smaller than the default file size limit (500Mb).");
        parser.addCommandFlag(
                cmd(),
                kUseAllSamples,
                0,
                false,
                "Use all samples in the directory provided for training, ignoring any size limits.");
        parser.addCommandFlag(
                cmd(),
                kNoAceSuccessors,
                0,
                false,
                "Disable ACE successors during training.");
        parser.addCommandFlag(
                cmd(),
                kNoClustering,
                0,
                false,
                "Skip clustering during training.");
        parser.addCommandFlag(
                cmd(),
                kMaxTimeSecs,
                0,
                true,
                "Adds a duration limit to how long training will run for. Training "
                "will stop early and return the current best result if the duration "
                "is exceeded.");
        parser.addCommandFlag(
                cmd(),
                kMaxFileSizeMb,
                0,
                true,
                "Specifies the maximum file size in megabytes to use for training. If flag is not passed in, defaults to 150MiB.");
        parser.addCommandFlag(
                cmd(),
                kMaxTotalSizeMb,
                0,
                true,
                "Specifies the maximum size of all samples in megabytes to use for training. If flag is not passed in, defaults to 300MiB.");
        parser.addCommandFlag(
                cmd(),
                kParetoFrontier,
                0,
                false,
                "Enables pareto frontier training. This will output a directory containing all compressors in the pareto frontier.");
    }

    explicit TrainArgs(const arg::ParsedArgs& parsed)
            : GlobalArgs(parsed), ProfileArgs(parsed)
    {
        // Create the compressor
        setCompressor(createCompressorFromArgs(
                *this, parsed.cmdFlag(cmd(), kCompressor)));
        auto outputPath = parsed.cmdFlag(cmd(), kOutput);
        if (outputPath) {
            checkOutput(outputPath.value(), parsed.cmdHasFlag(cmd(), kForce));
            output = std::make_unique<tools::io::OutputFile>(
                    std::move(outputPath).value());
        }
        auto sampleDir = parsed.cmdFlag(cmd(), kSampleDir);
        inputs         = tools::io::InputSetBuilder(recursive)
                         .add_path(std::move(sampleDir))
                         .build();

        // Train Params
        auto trainer = parsed.cmdFlag(cmd(), kTrainer);
        const std::string kFullSplitTrainerName = "full-split";
        const std::string kGreedyTrainerName    = "greedy";
        const std::string kBottomUpTrainerName  = "bottom-up";
        if (trainer) {
            if (trainer == kFullSplitTrainerName) {
                trainParams.clusteringTrainer =
                        openzl::training::ClusteringTrainer::FullSplit;
            } else if (trainer == kGreedyTrainerName) {
                trainParams.clusteringTrainer =
                        openzl::training::ClusteringTrainer::Greedy;
            } else if (trainer == kBottomUpTrainerName) {
                trainParams.clusteringTrainer =
                        openzl::training::ClusteringTrainer::BottomUp;
            } else {
                std::stringstream msg;
                msg << "Invalid training algorithm '" << *trainer
                    << "'. Valid options are: '" << kFullSplitTrainerName
                    << "', '" << kGreedyTrainerName << "', or '"
                    << kBottomUpTrainerName << "'";
                throw InvalidArgsException(msg.str());
            }
        }

        auto threads = parsed.cmdFlag(cmd(), kThreads);
        if (threads) {
            trainParams.threads = util::checkedstoul(threads.value());
        }
        auto numSamples = parsed.cmdFlag(cmd(), kNumSamples);
        if (numSamples) {
            trainParams.numSamples = util::checkedstoul(numSamples.value());
        }
        auto maxTimeSecs = parsed.cmdFlag(cmd(), kMaxTimeSecs);
        if (maxTimeSecs) {
            trainParams.maxTimeSecs = util::checkedstoul(maxTimeSecs.value());
        }
        useAllSamples      = parsed.cmdHasFlag(cmd(), kUseAllSamples);
        auto maxFileSizeMb = parsed.cmdFlag(cmd(), kMaxFileSizeMb);
        if (maxFileSizeMb) {
            trainParams.maxFileSizeMb =
                    util::checkedstoul(maxFileSizeMb.value());
        }
        auto maxTotalSizeMb = parsed.cmdFlag(cmd(), kMaxTotalSizeMb);
        if (maxTotalSizeMb) {
            trainParams.maxTotalSizeMb =
                    util::checkedstoul(maxTotalSizeMb.value());
        }

        if (parsed.cmdHasFlag(cmd(), kParetoFrontier)) {
            trainParams.paretoFrontier = true;
        }

        trainParams.noAceSuccessors =
                parsed.cmdHasFlag(cmd(), kNoAceSuccessors);

        trainParams.noClustering = parsed.cmdHasFlag(cmd(), kNoClustering);
        trainParams.compressorGenFunc =
                custom_parsers::createCompressorFromSerialized;
    }

    explicit TrainArgs(
            const GlobalArgs& globalArgs,
            const std::shared_ptr<Compressor>& compressor)
            : GlobalArgs(globalArgs), ProfileArgs(compressor)
    {
        trainParams.compressorGenFunc =
                custom_parsers::createCompressorFromSerialized;
    }

    static Cmd cmd()
    {
        return Cmd::TRAIN;
    }

    std::shared_ptr<tools::io::InputSet> inputs;
    std::shared_ptr<tools::io::Output> output;

    bool useAllSamples{};
    training::TrainParams trainParams;

   private:
    inline static const std::string kSampleDir  = "sample-dir";
    inline static const std::string kCompressor = "compressor";

    inline static const std::string kOutput = "output";
    inline static const std::string kForce  = "force";

    // Train Params
    inline static const std::string kTrainer         = "trainer";
    inline static const std::string kThreads         = "threads";
    inline static const std::string kNumSamples      = "num-samples";
    inline static const std::string kUseAllSamples   = "use-all-samples";
    inline static const std::string kNoAceSuccessors = "no-ace-successors";
    inline static const std::string kNoClustering    = "no-clustering";
    inline static const std::string kMaxTimeSecs     = "max-time-secs";
    inline static const std::string kMaxFileSizeMb   = "max-file-size-mb";
    inline static const std::string kMaxTotalSizeMb  = "max-total-size-mb";
    inline static const std::string kParetoFrontier  = "pareto-frontier";
};

} // namespace openzl::cli
