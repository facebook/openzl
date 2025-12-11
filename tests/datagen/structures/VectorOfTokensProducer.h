// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <assert.h>
#include <random>
#include <unordered_map>
#include <vector>

#include "tests/datagen/distributions/UniformDistribution.h"
#include "tests/datagen/structures/FixedWidthDataProducer.h"

namespace openzl::tests::datagen {

struct VectorOfTokensParameters {
    /// At every level of the tree, there will be branchingFactor children
    size_t branchingFactor = 2;
    /// The maximum depth of the tree
    size_t maxDepth = 12;
    /// The probability that a node in the tree has no children
    size_t endProb = 20;
    /// When generating vectors of tokens that follow a path in the tree,
    /// the probability that a node is skipped. The path is still followed, it
    /// just isn't appended to the list of tokens.
    size_t skipProb = 20;
    /// The number of tokens to generate
    size_t numTokens = 5000000;

    void print(std::ostream& os) const
    {
        os << "branchingFactor=" << branchingFactor << ",maxDepth=" << maxDepth
           << ",endProb=" << endProb << ",skipProb=" << skipProb
           << ",numTokens=" << numTokens;
    }
};

/**
 * Generates a vector of uint32_t tokens that are drawn from paths in a tree,
 * which is constructed according to the VectorOfTokensParameters.
 *
 * This is simulating tokenized stack traces, where each vector of tokens is
 * drawn from a path in a tree, and sometimes nodes in a path are skipped.
 */
class VectorOfTokensProducer : public FixedWidthDataProducer {
   public:
    explicit VectorOfTokensProducer(
            std::shared_ptr<RandWrapper> rw,
            const VectorOfTokensParameters& params = {})
            : FixedWidthDataProducer(std::move(rw), 4),
              params_(params),
              dist100_(rw_, 0, 99),
              child_(rw_, 0, params_.branchingFactor - 1)
    {
        assert(params_.branchingFactor > 0);
        fillPaths("VectorOfTokensProducer:fillPaths", 0, 0);
    }

    FixedWidthData operator()(RandWrapper::NameType name) override
    {
        std::vector<uint32_t> tokens;
        tokens.reserve(params_.numTokens);
        while (tokens.size() < params_.numTokens) {
            appendPath(name, tokens);
        }
        tokens.resize(params_.numTokens);
        return FixedWidthData(tokens);
    }

    void print(std::ostream& os) const override
    {
        os << "VectorOfTokensProducer(";
        params_.print(os);
        os << ")";
    }

   private:
    void appendPath(RandWrapper::NameType name, std::vector<uint32_t>& tokens)
    {
        auto shouldSkip = [&] { return dist100_(name) < params_.skipProb; };
        uint32_t token  = 0;
        for (;;) {
            if (!shouldSkip()) {
                tokens.push_back(token);
            }
            const auto it = paths_.find(token);
            if (it == paths_.end() || it->second.empty()) {
                break;
            }
            token = it->second[child_(name)];
        }
    }

    void fillPaths(RandWrapper::NameType name, uint32_t token, size_t depth)
    {
        auto shouldStop = [&] { return dist100_(name) < params_.endProb; };
        if (depth >= params_.maxDepth || shouldStop()) {
            return;
        }
        for (size_t i = 0; i < params_.branchingFactor; ++i) {
            const uint32_t child = nextToken_++;
            paths_[token].push_back(child);
            fillPaths(name, child, depth + 1);
        }
    }

    VectorOfTokensParameters params_;
    std::unordered_map<uint32_t, std::vector<uint32_t>> paths_;
    UniformDistribution<size_t> dist100_;
    UniformDistribution<size_t> child_;
    uint32_t nextToken_ = 0;
};

} // namespace openzl::tests::datagen
