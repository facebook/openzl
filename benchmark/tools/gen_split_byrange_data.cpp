// Copyright (c) Meta Platforms, Inc. and affiliates.

/*
 * Generate golden benchmark data files for split_byrange.
 *
 * Usage:
 *   gen_split_byrange_data <output_file> <mode> [nbElts]
 *
 * Modes:
 *   nosplit    - uniform random u32 values (single range, no split possible)
 *   2ranges   - two non-overlapping ranges, equal halves
 *   5ranges   - five non-overlapping ranges, equal segments
 *   10ranges  - ten non-overlapping ranges, equal segments
 *   ascending - values from ascending ranges with increasing base
 *   valley    - high-low-high pattern (descending then ascending ranges)
 *
 * Default nbElts: 262144 (1 MB of u32)
 *
 * Examples:
 *   gen_split_byrange_data /tmp/openzl_bench/nosplit.bin nosplit
 *   gen_split_byrange_data /tmp/openzl_bench/5ranges.bin 5ranges 1000000
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_NB_ELTS (256 * 1024) /* 1 MB of u32 */

/* Simple xorshift64 PRNG for reproducibility */
// NOLINTNEXTLINE(facebook-avoid-non-const-global-variables)
static uint64_t rng_state = 0x123456789ABCDEF0ULL;

static uint64_t xorshift64(void)
{
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static uint32_t randRange(uint32_t lo, uint32_t hi)
{
    if (lo >= hi)
        return lo;
    return lo + (uint32_t)(xorshift64() % (hi - lo + 1));
}

/* Fill buffer with random u32 values in [lo, hi] */
static void fillRange(uint32_t* buf, size_t n, uint32_t lo, uint32_t hi)
{
    for (size_t i = 0; i < n; i++) {
        buf[i] = randRange(lo, hi);
    }
}

static int writeFile(const char* path, const uint32_t* data, size_t nbElts)
{
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s for writing\n", path);
        return 1;
    }
    size_t written = fwrite(data, sizeof(uint32_t), nbElts, f);
    fclose(f);
    if (written != nbElts) {
        fprintf(stderr, "Error: wrote %zu of %zu elements\n", written, nbElts);
        return 1;
    }
    printf("Wrote %zu elements (%zu bytes) to %s\n",
           nbElts,
           nbElts * sizeof(uint32_t),
           path);
    return 0;
}

/* Generate N equal segments with non-overlapping ranges.
 * Ranges are separated by gaps of ~1000. */
static int genNRanges(const char* path, size_t nbElts, int nbRanges)
{
    uint32_t* data = (uint32_t*)malloc(nbElts * sizeof(uint32_t));
    if (!data) {
        fprintf(stderr, "Error: allocation failed\n");
        return 1;
    }

    size_t segSize           = nbElts / (size_t)nbRanges;
    uint32_t cursor          = 100;
    uint32_t const amplitude = 500;
    uint32_t const gap       = 1000;

    for (int r = 0; r < nbRanges; r++) {
        size_t start = (size_t)r * segSize;
        size_t end   = (r == nbRanges - 1) ? nbElts : start + segSize;
        fillRange(data + start, end - start, cursor, cursor + amplitude);
        cursor += amplitude + gap;
    }

    int ret = writeFile(path, data, nbElts);
    if (!ret) {
        printf("  Pattern: %d equal ranges, amplitude=%u, gap=%u\n",
               nbRanges,
               amplitude,
               gap);
    }
    free(data);
    return ret;
}

/* ascending: ranges with increasing base and varying amplitude */
static int genAscending(const char* path, size_t nbElts)
{
    int const nbRanges = 6;
    uint32_t* data     = (uint32_t*)malloc(nbElts * sizeof(uint32_t));
    if (!data) {
        fprintf(stderr, "Error: allocation failed\n");
        return 1;
    }

    size_t segSize  = nbElts / (size_t)nbRanges;
    uint32_t cursor = 0;

    uint32_t const amplitudes[] = { 200, 50, 800, 100, 300, 500 };
    uint32_t const gaps[]       = { 500, 2000, 300, 5000, 1000, 0 };

    for (int r = 0; r < nbRanges; r++) {
        size_t start = (size_t)r * segSize;
        size_t end   = (r == nbRanges - 1) ? nbElts : start + segSize;
        fillRange(data + start, end - start, cursor, cursor + amplitudes[r]);
        cursor += amplitudes[r] + gaps[r];
    }

    int ret = writeFile(path, data, nbElts);
    if (!ret) {
        printf("  Pattern: %d ascending ranges with varying amplitudes\n",
               nbRanges);
    }
    free(data);
    return ret;
}

/* valley: high-low-high pattern */
static int genValley(const char* path, size_t nbElts)
{
    int const nbRanges = 5;
    uint32_t* data     = (uint32_t*)malloc(nbElts * sizeof(uint32_t));
    if (!data) {
        fprintf(stderr, "Error: allocation failed\n");
        return 1;
    }

    /* Ranges: 10000-10500, 5000-5200, 100-300, 6000-6800, 12000-12400 */
    uint32_t const lo[] = { 10000, 5000, 100, 6000, 12000 };
    uint32_t const hi[] = { 10500, 5200, 300, 6800, 12400 };

    size_t segSize = nbElts / (size_t)nbRanges;
    for (int r = 0; r < nbRanges; r++) {
        size_t start = (size_t)r * segSize;
        size_t end   = (r == nbRanges - 1) ? nbElts : start + segSize;
        fillRange(data + start, end - start, lo[r], hi[r]);
    }

    int ret = writeFile(path, data, nbElts);
    if (!ret) {
        printf("  Pattern: valley (high-low-high) with %d ranges\n", nbRanges);
    }
    free(data);
    return ret;
}

static void printUsage(void)
{
    fprintf(stderr,
            "Usage: gen_split_byrange_data <output_file> <mode> [nbElts]\n"
            "\n"
            "Modes:\n"
            "  nosplit    - uniform random (no range boundaries)\n"
            "  2ranges    - two non-overlapping ranges\n"
            "  5ranges    - five non-overlapping ranges\n"
            "  10ranges   - ten non-overlapping ranges\n"
            "  ascending  - ascending ranges with varying amplitudes\n"
            "  valley     - high-low-high pattern\n"
            "\n"
            "Default nbElts: %d (= %d KB of u32)\n",
            DEFAULT_NB_ELTS,
            (int)(DEFAULT_NB_ELTS * 4 / 1024));
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        printUsage();
        return 1;
    }

    const char* path = argv[1];
    const char* mode = argv[2];
    size_t nbElts    = DEFAULT_NB_ELTS;
    if (argc >= 4) {
        nbElts = (size_t)atol(argv[3]);
        if (nbElts == 0) {
            fprintf(stderr, "Error: nbElts must be > 0\n");
            return 1;
        }
    }

    /* Seed PRNG deterministically */
    rng_state = 0x123456789ABCDEF0ULL;

    if (strcmp(mode, "nosplit") == 0) {
        return genNRanges(path, nbElts, 1);
    } else if (strcmp(mode, "2ranges") == 0) {
        return genNRanges(path, nbElts, 2);
    } else if (strcmp(mode, "5ranges") == 0) {
        return genNRanges(path, nbElts, 5);
    } else if (strcmp(mode, "10ranges") == 0) {
        return genNRanges(path, nbElts, 10);
    } else if (strcmp(mode, "ascending") == 0) {
        return genAscending(path, nbElts);
    } else if (strcmp(mode, "valley") == 0) {
        return genValley(path, nbElts);
    } else {
        fprintf(stderr, "Error: unknown mode '%s'\n", mode);
        printUsage();
        return 1;
    }
}
