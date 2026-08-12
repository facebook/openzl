// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/ace/automated_compressor_explorer.h"

#include <chrono>
#include <vector>

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/allocation.h"
#include "openzl/shared/a1cbor.h"

namespace openzl {
namespace training {

std::vector<ACECompressor> AutomatedCompressorExplorer::initialPopulation()
{
    // Use all prebuilt compressors
    auto prebuilt = getPrebuiltCompressors(inputType());
    std::vector<ACECompressor> population;
    population.insert(population.end(), prebuilt.begin(), prebuilt.end());

    // Use populationSize() random compressors
    for (size_t i = 0; i < populationSize(); ++i) {
        population.push_back(
                buildRandomCompressor(rng(), inputType(), formatVersion_));
    }
    return population;
}

namespace {
/// Put small evolutionary pressure on the results to be simpler.
/// Adjusts cSize on the scale of 0.1%, and times on the scale of 1%, given a
/// typical number of components of ~10.
void adjustResults(const ACECompressor& gene, std::vector<float>& results)
{
    std::array<float, 3> scale = { 0.0001, 0.001, 0.001 };
    const size_t numComponents = gene.numComponents();
    for (size_t i = 0; i < results.size(); ++i) {
        const auto delta = results[i] * scale[i] * numComponents;
        results[i] += std::max<float>(numComponents, delta);
    }
}
} // namespace

/* static */ std::vector<float> AutomatedCompressorExplorer::computeFitness(
        const ACECompressor& gene,
        poly::span<const Input> inputs,
        uint32_t formatVersion)
{
    auto result = gene.benchmark(inputs, formatVersion);
    std::vector<float> fitness(3, std::numeric_limits<float>::infinity());
    if (result.has_value()) {
        fitness[0] = result->compressedSize;
        fitness[1] = result->compressionTime.count();
        fitness[2] = result->decompressionTime.count();
        adjustResults(gene, fitness);
    }
    return fitness;
}

std::vector<float> AutomatedCompressorExplorer::computeFitness(
        const ACECompressor& gene)
{
    return computeFitness(gene, inputs_, formatVersion_);
}

std::vector<std::vector<float>> AutomatedCompressorExplorer::computeFitness(
        poly::span<const ACECompressor> genes)
{
    std::vector<std::future<std::vector<float>>> futures;
    futures.reserve(genes.size());
    for (const auto& gene : genes) {
        auto cache = cachedFitness_.find(gene.hash());
        if (cache != cachedFitness_.end()) {
            std::uniform_int_distribution<size_t> dist(0, cache->second.first);
            if (dist(rng()) != 0) {
                std::promise<std::vector<float>> promise;
                promise.set_value(cache->second.second);
                futures.emplace_back(promise.get_future());
                continue;
            }
        }
        futures.emplace_back(threadPool_.run(
                [&inputs = inputs_, &gene, formatVersion = formatVersion_] {
                    return computeFitness(gene, inputs, formatVersion);
                }));
    }

    std::vector<std::vector<float>> results;
    results.reserve(genes.size());
    for (size_t i = 0; i < genes.size(); ++i) {
        auto result            = futures[i].get();
        auto [cache, inserted] = cachedFitness_.emplace(
                genes[i].hash(), std::make_pair(1, result));
        if (!inserted) {
            for (size_t j = 0; j < result.size(); ++j) {
                cache->second.second[j] =
                        std::min(cache->second.second[j], result[j]);
            }
            ++cache->second.first;
        }
        results.push_back(std::move(result));
    }
    return results;
}

namespace {
struct ArenaDeleter {
    void operator()(Arena* arena) const
    {
        ALLOC_Arena_freeArena(arena);
    }
};
} // namespace

std::string AutomatedCompressorExplorer::savePopulation() const
{
    std::unique_ptr<Arena, ArenaDeleter> arena(ALLOC_HeapArena_create());
    auto a         = A1C_Arena_wrap(arena.get());
    A1C_Item* item = A1C_Item_root(&a);
    if (item == nullptr) {
        throw std::bad_alloc();
    }
    A1C_Item* array = A1C_Item_array(item, population().size(), &a);
    if (array == nullptr) {
        throw std::bad_alloc();
    }
    for (const auto& gene : population()) {
        const auto s = gene.serialize();
        if (!A1C_Item_string_copy(array++, s.data(), s.size(), &a)) {
            throw std::bad_alloc();
        }
    }
    const size_t size = A1C_Item_encodedSize(item);
    std::string serialized(size, '\0');
    const size_t actual = A1C_Item_encode(
            item, (uint8_t*)serialized.data(), serialized.size(), nullptr);
    if (size != actual) {
        throw Exception("Serialization failed");
    }
    return serialized;
}

void AutomatedCompressorExplorer::loadPopulation(poly::string_view snapshot)
{
    std::unique_ptr<Arena, ArenaDeleter> arena(ALLOC_HeapArena_create());
    A1C_Decoder decoder;
    A1C_Decoder_init(&decoder, A1C_Arena_wrap(arena.get()), {});
    const A1C_Item* item = A1C_Decoder_decode(
            &decoder, (const uint8_t*)snapshot.data(), snapshot.size());
    if (item == nullptr) {
        throw Exception("Failed to deserialize");
    }
    if (item->type != A1C_ItemType_array) {
        throw Exception("Must be an array");
    }
    std::vector<ACECompressor> population;
    population.reserve(item->array.size);
    for (size_t i = 0; i < item->array.size; ++i) {
        const A1C_Item& p = item->array.items[i];
        if (p.type != A1C_ItemType_string) {
            throw Exception("Array elements must be strings");
        }
        population.emplace_back(
                poly::string_view{ p.string.data, p.string.size });
    }
    extendPopulation(std::move(population));
}

} // namespace training
} // namespace openzl
