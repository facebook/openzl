// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

constexpr std::string_view usage_hint =
        "input_gen usage: input_gen input_file output_file mode [additional params]\n"
        "\tavailable modes:\n"
        "\t- dispatchString_encode\n"
        "========\n"
        "Additional params for different modes:\n"
        "- dispatchString_encode\n"
        "  n a_1 a_2 ... a_n\n"
        "  n (optional): length of the custom dispatch loop (default: 8)\n"
        "  a_i (optional): if n is specified, the i-th value in the dispatch\n"
        "                  loop (default: a_i = i - 1)\n";

// keep in sync with defn in benchList.h
#define DISPATCH_STRING_NB_DSTS 8

/**
 * Generates a packed buffer containing
 *    - u32: nbStrs
 *    - u32[]: strLens
 *    - u8[]: indices
 *    - char[]: raw string buffer
 */
static std::string gen_dispatchString(
        const std::string& rawInput,
        const std::vector<uint8_t>& dispatchLoop)
{
    // split the string based on whitespace chars. When splitting, the
    // whitespace character will be part of the *first* string. e.g. "a b  c" ->
    // ["a ", "b ", " ", "c"]
    std::vector<uint32_t> rawStrLens;
    std::vector<uint32_t> strLens;
    size_t strStart = 0;
    while (1) {
        size_t pos = rawInput.find(' ', strStart);
        if (pos == std::string::npos) {
            rawStrLens.push_back(rawInput.size() - strStart);
            break;
        }
        rawStrLens.push_back(pos + 1 - strStart);
        strStart = pos + 1;
    }

    if (0) {
        // Experimental code to artificially vary the proportion of strings
        // larger than the default block size used by the transform. Coalesces
        // some portion of the "raw" strings into single large strings s.t. the
        // proportion of long strings to total strings is roughly @pct.
        double pct         = 0.5;
        uint32_t blockSize = 32;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);
        for (size_t i = 0; i < rawStrLens.size(); ++i) {
            if (dis(gen) >= pct) {
                strLens.push_back(rawStrLens[i]);
                continue;
            }
            uint32_t coalescedLen = 0;
            while (coalescedLen <= blockSize && i < rawStrLens.size()) {
                coalescedLen += rawStrLens[i];
                ++i;
            }
            --i;
            strLens.push_back(coalescedLen);
        }
    } else {
        strLens = rawStrLens;
    }

    uint32_t nbStrs = strLens.size();
    std::vector<uint8_t> indices(nbStrs);
    const auto loopSize = dispatchLoop.size();
    for (uint32_t i = 0; i < nbStrs; ++i) {
        indices[i] = dispatchLoop[i % loopSize];
    }

    // pack the metadata according to the spec
    const size_t metadataSize = sizeof(uint32_t) + nbStrs * sizeof(uint32_t)
            + nbStrs * sizeof(uint8_t);
    void* scratch = malloc(metadataSize);
    assert(scratch != nullptr);
    uint32_t* scratchPtru32 = (uint32_t*)scratch;
    scratchPtru32[0]        = nbStrs;
    for (uint32_t i = 0; i < nbStrs; ++i) {
        scratchPtru32[i + 1] = strLens[i];
    }
    uint8_t* scratchPtru8 = (uint8_t*)(scratchPtru32 + nbStrs + 1);
    for (uint32_t i = 0; i < nbStrs; ++i) {
        scratchPtru8[i] = indices[i];
    }

    std::string retval((char*)scratch, metadataSize);
    free(scratch);
    retval += rawInput;
    return retval;
}

/**
 * Massages a serial input file into the packed format expected by the
 * corresponding unitBench function. By default, will generate a new file but
 * can be used to overwrite the input as well.
 */
int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cout << usage_hint << std::endl;
        return 0;
    }
    std::string input_file  = argv[1];
    std::string output_file = argv[2];
    std::string mode        = argv[3];

    if (mode != "dispatchString_encode") {
        std::cout << usage_hint << std::endl;
        return 0;
    }

    // parse additional params
    std::vector<uint8_t> dispatchLoop;
    if (argc > 4) {
        int n = std::atoi(argv[4]);
        if (argc != 5 + n) {
            std::cout << usage_hint << std::endl;
            return 0;
        }
        for (int i = 0; i < n; ++i) {
            dispatchLoop.push_back(std::atoi(argv[5 + i]));
            assert(dispatchLoop.back() < DISPATCH_STRING_NB_DSTS);
        }
    } else {
        // default
        dispatchLoop = { 0, 1, 2, 3, 4, 5, 6, 7 };
    }

    std::ifstream input(input_file);
    std::stringstream buf;
    buf << input.rdbuf();
    input.close();

    const auto massaged = gen_dispatchString(buf.str(), dispatchLoop);
    std::ofstream output(output_file);
    output << massaged;
    output.close();

    return 0;
}
