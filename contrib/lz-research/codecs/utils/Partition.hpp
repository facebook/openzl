// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

namespace openzl::lz::utils {

/**
 * Compute the optimal partition of data to minimize costFn.
 *
 * This algorithm is O(histogram.size()^2) in the worst case, and uses
 * O(histogram.size()) memory.
 *
 * @returns The beginning of each partition. The last partition implicitly
 * ends at data.size(). The first partition will start at 0.
 */
template <typename T, typename CostFn>
std::vector<size_t> partition(poly::span<const T> histogram, CostFn&& costFn)
{
    const size_t N  = histogram.size();
    const size_t N1 = N + 1;
    // opt[e] = The optimal cost to partion [1, e)
    std::vector<float> opt(N1, 0);
    // begin[e] = map from bucket end index to the optimal begin index
    std::vector<uint32_t> begin(N1, 0);
    // numBuckets[e] = number of buckets in the optimal partion of [0, e)
    // NOTE: This does not include a bucket for the remaining indices [e, N).
    std::vector<uint32_t> numBuckets(N1, 0);

    // NOTE: opt[0] = 0
    // NOTE: numBuckets[0] = 0
    for (size_t e = 1; e < N1; ++e) {
        for (size_t b = 0; b < e; ++b) {
            const float endCost = costFn(histogram.subspan(b, e - b));
            const float cost    = opt[b] + endCost;
            if (b == 0 || cost < opt[e]) {
                opt[e]   = cost;
                begin[e] = b;
            }
        }
        numBuckets[e] = numBuckets[begin[e]] + 1;
    }

    std::vector<size_t> partitions(numBuckets[N]);
    for (size_t e = N, pos = partitions.size(); e > 0; e = begin[e]) {
        partitions[--pos] = begin[e];
    }
    return partitions;
}

/**
 * Optimally partition a histogram into at most numBuckets buckets so that
 * `costFn()` is minimized. The cost of each bucket must depend only on the
 * histogram values in that bucket.
 *
 * This algorithm is O(histogram.size()^3) in the worst case, and uses
 * O(histogram.size() * numBuckets) memory.

 * @returns The beginning of each partition. The last partition implicitly
 * ends at histogram.size(). The first partition will start at 0.
 */
template <typename T, typename CostFn>
std::vector<size_t>
partition(poly::span<const T> histogram, size_t numBuckets, CostFn&& costFn)
{
    const size_t B  = std::min(numBuckets, histogram.size());
    const size_t B1 = B + 1;
    const size_t N  = histogram.size();
    const size_t N1 = N + 1;
    // opt[N1 * k + e] = The optimal cost to partion [0, e) with k buckets
    std::vector<float> opt(N1 * B1, std::numeric_limits<float>::infinity());
    // begin[N1 * k + e] = The beginning index for e with k buckets
    std::vector<uint32_t> begin(N1 * B1, std::numeric_limits<uint32_t>::max());

    opt[N1 * 0 + 0]   = 0;
    begin[N1 * 0 + 0] = 0;
    for (size_t e = 1; e < N1; ++e) {
        const size_t maxPossibleBuckets = std::min(e, B);
        for (size_t k = 1; k <= maxPossibleBuckets; ++k) {
            for (size_t b = e; b-- > k - 1;) {
                const float oldCost = opt[N1 * k + e];
                const float endCost = costFn(histogram.subspan(b, e - b));
                if (endCost > oldCost) {
                    // break;
                }
                const float newCost = opt[N1 * (k - 1) + b] + endCost;
                if (newCost < oldCost) {
                    opt[N1 * k + e]   = newCost;
                    begin[N1 * k + e] = b;
                }
            }
        }
    }

    std::vector<size_t> partitions(B, 0);
    for (size_t e = N, pos = partitions.size(); e > 0;) {
        auto b = begin[N1 * pos-- + e];
        assert(b < e);
        partitions[pos] = b;
        e               = b;
    }
    return partitions;
}

/**
 * Optimally partition a histogram into at most numBuckets buckets so that
 * `costFn()` is minimized. The cost of each bucket must depend only on the
 * histogram values in that bucket.
 *
 * This algorithm is O(histogram.size()^3) in the worst case, and uses
 * O(histogram.size() * numBuckets) memory.

 * @returns The beginning of each partition. The last partition implicitly
 * ends at histogram.size(). The first partition will start at 0.
 */
template <typename T, typename CostFn, typename SizeFn>
std::vector<size_t> partition(
        poly::span<const T> histogram,
        size_t numBuckets,
        size_t maxBucketSize,
        CostFn&& costFn,
        SizeFn&& idxFn)
{
    const size_t B  = std::min(numBuckets, histogram.size());
    const size_t B1 = B + 1;
    const size_t N  = histogram.size();
    const size_t N1 = N + 1;
    // opt[N1 * k + e] = The optimal cost to partion [0, e) with k buckets
    std::vector<float> opt(N1 * B1, std::numeric_limits<float>::infinity());
    // begin[N1 * k + e] = The beginning index for e with k buckets
    std::vector<uint32_t> begin(N1 * B1, std::numeric_limits<uint32_t>::max());

    opt[N1 * 0 + 0]   = 0;
    begin[N1 * 0 + 0] = 0;
    for (size_t e = 1; e < N1; ++e) {
        const size_t maxPossibleBuckets = std::min(e, B);
        for (size_t k = 1; k <= maxPossibleBuckets; ++k) {
            const auto maxSize = std::min(maxBucketSize, e - (k - 1));
            for (size_t size = 1; size <= maxSize;
                 size        = (e == N ? size + 1 : size * 2)) {
                const auto b        = e - size;
                const float oldCost = opt[N1 * k + e];
                const float endCost = costFn(histogram.subspan(b, e - b));
                const float newCost = opt[N1 * (k - 1) + b] + endCost;
                if (newCost < oldCost) {
                    if (0 && e != N && k != 1) {
                        assert(b != 0);
                        const auto bb = begin[N1 * (k - 1) + b];
                        assert(bb < b);
                        const auto bI             = idxFn(b);
                        const auto bucketSize     = idxFn(e) - bI;
                        const auto prevBucketSize = bI - idxFn(bb);
                        if (bucketSize < prevBucketSize) {
                            continue;
                        }
                    }
                    opt[N1 * k + e]   = newCost;
                    begin[N1 * k + e] = b;
                }
            }
        }
    }

    std::vector<size_t> partitions(B, 0);
    for (size_t e = N, pos = partitions.size(); e > 0;) {
        auto b = begin[N1 * pos-- + e];
        assert(b < e);
        partitions[pos] = b;
        e               = b;
    }
    return partitions;
}
} // namespace openzl::lz::utils
