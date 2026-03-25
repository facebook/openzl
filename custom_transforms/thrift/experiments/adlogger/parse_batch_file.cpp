// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "admarket/adlogger/parser/if/BatchAdsLogLineData.h"
#include "admarket/adlogger/parser/if/gen-cpp2/adlogger_parser_types.h"
#include "admarket/training_data/feature_store/common/FeatureStoreSerialization.h"
#include "folly/FileUtil.h"
#include "folly/Range.h"
#include "folly/io/IOBuf.h"

using facebook::adlogger::parser::AdsLogLineData;
using facebook::adlogger::parser::AdsLogLineDataBatch;
using facebook::adlogger::parser::BatchAdsLogLineData;
using facebook::adlogger::parser::makeBatchAdsLogLineData;
using facebook::training_data::feature_store::FeatureStoreSerialization;

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <batch_file>" << std::endl;
        return 1;
    }
    std::string buffer;
    const char* const filename = argv[1];
    folly::readFile(filename, buffer);

    std::vector<AdsLogLineData> adsLogLineDataVec =
            FeatureStoreSerialization::deserializeBatch(
                    folly::IOBuf::wrapBuffer(folly::ByteRange(buffer)));
    auto batchData = makeBatchAdsLogLineData(std::move(adsLogLineDataVec));

    folly::writeFile(
            (**batchData.compressedFeatureIds_ref()).moveToFbString(),
            (std::string(filename) + ".featureIds").c_str());

    folly::writeFile(
            (**batchData.compressedFloats_ref()).moveToFbString(),
            (std::string(filename) + ".floats").c_str());

    folly::writeFile(
            (**batchData.compressedSparseIds_ref()).moveToFbString(),
            (std::string(filename) + ".sparseIds").c_str());

    folly::writeFile(
            (**batchData.compressedLengths_ref()).moveToFbString(),
            (std::string(filename) + ".lengths").c_str());

    folly::writeFile(
            (**batchData.compressedMetadata_ref()).moveToFbString(),
            (std::string(filename) + ".metadata").c_str());
}
