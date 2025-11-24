// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include <map>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <folly/Overload.h>
#include <folly/io/IOBuf.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "openzl/zl_version.h"
#include "tests/datagen/distributions/VecLengthDistribution.h"
#include "tests/datagen/random_producer/RNGEngine.h"
#include "tests/datagen/structures/FixedWidthDataProducer.h"

// TODO: Not sure if there is a better way to do this
#if ZL_FBCODE_IS_RELEASE
#    include "openzl/prod/custom_transforms/thrift/kernels/tests/gen-cpp2/fuzz_data_fatal_types.h"
#    include "openzl/prod/custom_transforms/thrift/kernels/tests/gen-cpp2/fuzz_data_types.h"
#    include "openzl/prod/custom_transforms/thrift/kernels/tests/gen-cpp2/fuzz_data_visitation.h"
#else
#    include "openzl/dev/custom_transforms/thrift/kernels/tests/gen-cpp2/fuzz_data_fatal_types.h"
#    include "openzl/dev/custom_transforms/thrift/kernels/tests/gen-cpp2/fuzz_data_types.h"
#    include "openzl/dev/custom_transforms/thrift/kernels/tests/gen-cpp2/fuzz_data_visitation.h"
#endif

namespace zstrong::thrift::tests {
template <typename T>
std::string serialize(const T& value)
{
    std::string serialized;
    apache::thrift::CompactSerializer::serialize(value, &serialized);
    return serialized;
}

namespace detail {
template <typename T, typename RNG>
void fill(T& value, RNG&& gen);

template <typename Map, typename RNG>
void fillMap(Map& map, RNG&& gen)
{
    using Key   = typename Map::key_type;
    using Value = typename Map::mapped_type;

    std::uniform_int_distribution<size_t> sizeDist(0, 10);
    size_t const size = sizeDist(gen);
    for (size_t i = 0; i < size; ++i) {
        Key key;
        fill(key, gen);
        Value value;
        fill(value, gen);
        map.try_emplace(std::move(key), std::move(value));
    }
}

template <typename List, typename RNG>
void fillSet(List& set, RNG&& gen)
{
    using Value               = typename List::value_type;
    size_t const useSmallSize = std::bernoulli_distribution(0.5)(gen);
    size_t const minSize      = useSmallSize ? 0 : 15;
    size_t const maxSize      = useSmallSize ? 10 : 20;
    std::uniform_int_distribution<size_t> sizeDist(minSize, maxSize);
    size_t const size = sizeDist(gen);
    for (size_t i = 0; i < size; ++i) {
        Value value;
        fill(value, gen);
        set.insert(std::move(value));
    }
}

template <typename List, typename RNG>
void fillList(List& list, RNG&& gen)
{
    using Value               = typename List::value_type;
    size_t const useSmallSize = std::bernoulli_distribution(0.5)(gen);
    size_t const minSize      = useSmallSize ? 0 : 15;
    size_t const maxSize      = useSmallSize ? 10 : 20;
    std::uniform_int_distribution<size_t> sizeDist(minSize, maxSize);
    size_t const size = sizeDist(gen);
    list.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        Value value;
        fill(value, gen);
        list.push_back(std::move(value));
    }
}

template <typename RNG>
class FillContainer {
   public:
    FillContainer(RNG& gen) : gen_(&gen) {}

    void operator()(std::string& str)
    {
        return fillList(str, *gen_);
    }

    template <typename Key, typename Value>
    void operator()(std::map<Key, Value>& map)
    {
        return fillMap(map, *gen_);
    }

    template <typename Key, typename Value>
    void operator()(std::unordered_map<Key, Value>& map)
    {
        return fillMap(map, *gen_);
    }

    template <typename Value>
    void operator()(std::vector<Value>& list)
    {
        return fillList(list, *gen_);
    }

    template <typename Value>
    void operator()(std::set<Value>& list)
    {
        return fillSet(list, *gen_);
    }

    template <typename Value>
    void operator()(std::unordered_set<Value>& list)
    {
        return fillSet(list, *gen_);
    }

   private:
    RNG* gen_;
};

template <typename Int, typename RNG>
void fillInt(Int& v, RNG&& gen)
{
    std::uniform_int_distribution<Int> dist;
    v = dist(gen);
    // Uniformly select the # of bits
    std::uniform_int_distribution<size_t> shiftDist(0, sizeof(Int) * 8);
    auto const shift = shiftDist(gen);
    if (shift < sizeof(Int) * 8) {
        v >>= shift;
    } else {
        v = 0;
    }
}

template <typename RNG>
void fillBool(bool& v, RNG&& gen)
{
    std::bernoulli_distribution dist(0.5);
    v = dist(gen);
}

template <typename Float, typename RNG>
void fillFloat(Float& v, RNG&& gen)
{
    std::normal_distribution<Float> dist;
    v = dist(gen);
}

template <typename T, typename RNG>
void fillThrift(T& thrift, RNG&& gen)
{
    std::bernoulli_distribution shouldFill(0.5);
    apache::thrift::for_each_field(thrift, [&](const auto&, auto&& field_ref) {
        if (!shouldFill(gen)) {
            return;
        }
        field_ref.emplace();
        fill(*field_ref, gen);
    });
}

template <typename T, typename RNG>
void fill(T& value, RNG&& gen)
{
    auto fillFn = folly::overload(
            FillContainer(gen),
            [&gen](char& v) { fillInt(v, gen); },
            [&gen](int8_t& v) { fillInt(v, gen); },
            [&gen](int16_t& v) { fillInt(v, gen); },
            [&gen](int64_t& v) { fillInt(v, gen); },
            [&gen](int32_t& v) { fillInt(v, gen); },
            [&gen](bool& v) { fillBool(v, gen); },
            [&gen](float& v) { fillFloat(v, gen); },
            [&gen](double& v) { fillFloat(v, gen); },
            [&gen](auto& v) { fillThrift(v, gen); });
    fillFn(value);
}
} // namespace detail

template <typename T, typename RNG>
T generate(RNG&& gen)
{
    T data;
    detail::fill(data, gen);
    return data;
}

template <typename T>
class ThriftProducer : public zstrong::tests::datagen::FixedWidthDataProducer {
   public:
    explicit ThriftProducer(
            std::shared_ptr<zstrong::tests::datagen::RandWrapper> rw,
            size_t maxSamples = 10)
            : FixedWidthDataProducer(rw, 1), dist_(std::move(rw), 5, maxSamples)
    {
    }

    ::zstrong::tests::datagen::FixedWidthData operator()(
            zstrong::tests::datagen::RandWrapper::NameType name) override
    {
        std::string datum;
        const size_t n = dist_(name);
        for (size_t i = 0; i < n; ++i) {
            // todo: Thrift is important enough we should migrate generate<>()
            // to get structured randomness from randwrapper instead of just
            // using it as an rng. For e.g. fuzzing
            T value = zstrong::thrift::tests::generate<T>(
                    zstrong::tests::datagen::RNGEngine<uint64_t>(
                            rw_.get(),
                            "ThriftProducer::RNGEngine::operator()"));
            apache::thrift::CompactSerializer::serialize(value, &datum);
        }
        return { std::move(datum), 1 };
    }

    void print(std::ostream& os) const override
    {
        os << "ThriftProducer(std::string, 1)";
    }

   private:
    zstrong::tests::datagen::VecLengthDistribution dist_;
};

} // namespace zstrong::thrift::tests
