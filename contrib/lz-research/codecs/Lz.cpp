// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "Lz.hpp"

#include "openzl/codecs/common/copy.h"
#include "openzl/codecs/common/fast_table.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_errors.h"

#include "CodecIDs.hpp"

namespace openzl::lz {

using offset_t               = uint16_t;
size_t constexpr kMinMatch   = 7;
size_t constexpr kSmallMatch = 5;
size_t constexpr kLargeMatch = 8;

inline uint32_t
matchLength(uint8_t const* match, uint8_t const* ip, uint8_t const* iend)
{
    uint32_t len = 0;
    while (ip + len < iend && ip[len] == match[len]) {
        ++len;
    }
    // if (ip - match < len) {
    //   len = ip - match;
    // }
    return len;
}

template <bool kIsIndex, size_t kLLBits, size_t kMLBits>
class Transform {
    static uint8_t constexpr kLLMask = ((1 << kLLBits) - 1);
    static uint8_t constexpr kMLMask = ((1 << kMLBits) - 1);

    static_assert(!kIsIndex, "Currently unsupported");

    template <typename T>
    static ZL_Report
    writeStream(ZL_Encoder* eictx, size_t idx, std::vector<T> const& data)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

        auto stream = ZL_Encoder_createTypedStream(
                eictx, idx, data.size(), sizeof(T));
        ZL_ERR_IF_NULL(stream, allocation);
        memcpy(ZL_Output_ptr(stream), data.data(), data.size() * sizeof(T));
        ZL_ERR_IF_ERR(ZL_Output_commit(stream, data.size()));
        return ZL_returnSuccess();
    }

   public:
    static void encode1(
            std::vector<uint8_t>& lits,
            std::vector<uint32_t>& litLens,
            std::vector<offset_t>& offsets,
            std::vector<uint32_t>& matchLens,
            uint8_t const* const istart,
            uint8_t const* const iend)
    {
        auto mem = std::make_unique<uint8_t[]>(ZS_FastTable_tableSize(14));
        ZS_FastTable table{};
        ZS_FastTable_init(&table, mem.get(), 14, kMinMatch);
        uint8_t const* anchor       = istart;
        uint8_t const* ip           = istart + 1;
        uint8_t const* const ilimit = iend - (kMinMatch + 1);

        while (ip < ilimit) {
            uint8_t const* match = istart
                    + ZS_FastTable_getAndUpdateT(
                                           &table, ip, ip - istart, kMinMatch);
            uint32_t const distance = ip - match;
            if (ZL_read32(match) == ZL_read32(ip)
                && ZL_uintFits(distance, sizeof(offset_t))) {
                uint32_t ml = matchLength(match, ip, iend);
                while (match > istart && ip > anchor && match[-1] == ip[-1]) {
                    --match;
                    --ip;
                    ++ml;
                }
                uint32_t const ll = ip - anchor;
                offset_t const offset =
                        kIsIndex ? (match - istart) : (ip - match);

                lits.insert(lits.end(), anchor, ip);
                litLens.push_back(ll);
                offsets.push_back(offset);
                matchLens.push_back(ml);
                ZS_FastTable_putT(&table, ip + 2, ip + 2 - istart, kMinMatch);
                ip += ml;
                if (ip < ilimit) {
                    ZS_FastTable_putT(
                            &table, ip - 2, ip - 2 - istart, kMinMatch);
                }
                anchor = ip;
            } else {
                ip += 1;
            }
        }
        lits.insert(lits.end(), anchor, iend);
    }

    static void encode2(
            std::vector<uint8_t>& lits,
            std::vector<uint32_t>& litLens,
            std::vector<offset_t>& offsets,
            std::vector<uint32_t>& matchLens,
            uint8_t const* const istart,
            uint8_t const* const iend)
    {
        auto smallMem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTable_tableSize(16));
        auto largeMem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTable_tableSize(17));
        ZS_FastTable smallT{};
        ZS_FastTable_init(&smallT, smallMem.get(), 16, kSmallMatch);
        ZS_FastTable largeT{};
        ZS_FastTable_init(&largeT, largeMem.get(), 17, kLargeMatch);
        uint8_t const* anchor       = istart;
        uint8_t const* ip           = istart + 1;
        uint8_t const* const ilimit = iend - kLargeMatch;

        auto emitMatch = [&](uint8_t const* match, uint8_t const* ptr) {
            uint32_t ml = matchLength(match, ptr, iend);
            if (ml < 4) {
                ZL_REQUIRE(false);
                ++ip;
                return;
            }
            ZL_ASSERT_GE(
                    ml,
                    4,
                    "%u to %u",
                    unsigned(match - istart),
                    unsigned(ptr - istart));
            while (match > istart && ptr > anchor && match[-1] == ptr[-1]) {
                --match;
                --ptr;
                ++ml;
            }
            uint32_t const ll     = ptr - anchor;
            offset_t const offset = kIsIndex ? (match - istart) : (ptr - match);

            lits.insert(lits.end(), anchor, ptr);
            litLens.push_back(ll);
            offsets.push_back(offset);
            matchLens.push_back(ml);
            //   ZS_FastTable_putT(&largeT, ip + 1, ip + 1 - istart,
            //   kLargeMatch);

            ZS_FastTable_putT(&smallT, ip + 2, ip + 2 - istart, kSmallMatch);
            ZS_FastTable_putT(&largeT, ip + 2, ip + 2 - istart, kLargeMatch);

            ip     = ptr + ml;
            anchor = ip;

            if (ip <= ilimit) {
                ZS_FastTable_putT(
                        &smallT, ip - 1, ip - 1 - istart, kSmallMatch);
                ZS_FastTable_putT(
                        &largeT, ip - 2, ip - 2 - istart, kLargeMatch);
            }
        };
        while (ip < ilimit) {
            uint8_t const* matchL =
                    istart
                    + ZS_FastTable_getAndUpdateT(
                            &largeT, ip, ip - istart, kLargeMatch);
            uint8_t const* matchS =
                    istart
                    + ZS_FastTable_getAndUpdateT(
                            &smallT, ip, ip - istart, kSmallMatch);
            if (ZL_read64(matchL) == ZL_read64(ip)
                && ZL_uintFits(ip - matchL, sizeof(offset_t))) {
                ZS_FastTable_putT(
                        &largeT, ip + 1, ip + 1 - istart, kLargeMatch);
                emitMatch(matchL, ip);
            } else if (
                    ZL_read32(matchS) == ZL_read32(ip)
                    && ZL_uintFits(ip - matchS, sizeof(offset_t))) {
                uint8_t const* matchL1 =
                        istart
                        + ZS_FastTable_getAndUpdateT(
                                &largeT, ip + 1, ip + 1 - istart, kLargeMatch);
                if (ZL_read64(matchL1) == ZL_read64(ip + 1)
                    && ZL_uintFits(ip + 1 - matchL1, sizeof(offset_t))) {
                    emitMatch(matchL1, ip + 1);
                } else {
                    emitMatch(matchS, ip);
                }
            } else {
                ip += 1;
            }
        }

        lits.insert(lits.end(), anchor, iend);
    }

    static ZL_Report encode(ZL_Encoder* eictx, ZL_Input const* input) noexcept
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
        if (kIsIndex && ZL_Input_numElts(input) >= 65536) {
            ZL_ERR(GENERIC, "Input too large for index mode");
        }

        auto level = ZL_Encoder_getCParam(eictx, ZL_CParam_compressionLevel);

        std::vector<uint8_t> lits;
        std::vector<uint32_t> litLens;
        std::vector<uint32_t> matchLens;
        std::vector<offset_t> offsets;
        uint8_t const* const istart = (uint8_t const*)ZL_Input_ptr(input);
        uint8_t const* const iend   = istart + ZL_Input_numElts(input);

        if (level == 1) {
            encode1(lits, litLens, offsets, matchLens, istart, iend);
        } else {
            encode2(lits, litLens, offsets, matchLens, istart, iend);
        }

        std::array<uint32_t, 256> llStats{ 0 };
        std::array<uint32_t, 256> mlStats{ 0 };
        for (size_t i = 0; i < litLens.size(); ++i) {
            ++llStats[std::min<uint32_t>(litLens[i], 255)];
            ++mlStats[std::min<uint32_t>(matchLens[i], 255)];
        }

        size_t llParam = ZL_Encoder_getLocalIntParam(eictx, 0).paramValue;
        if (llParam == 0) {
            size_t bestLLBits    = 0;
            size_t bestExtraLens = 2 * litLens.size() + 1;
            for (size_t llBits = 1; llBits < 8; ++llBits) {
                const size_t mlBits = 8 - llBits;
                const size_t llMask = (1u << llBits) - 1;
                const size_t mlMask = (1u << mlBits) - 1;

                size_t numExtraLL = 0;
                for (size_t i = llMask; i < llStats.size(); ++i) {
                    numExtraLL += llStats[i];
                }
                size_t numExtraML = 0;
                for (size_t i = mlMask; i < mlStats.size(); ++i) {
                    numExtraML += mlStats[i];
                }
                size_t numExtraLens = numExtraLL + numExtraML;

                // Need some way to take branch predictibility into account.
                // Weigh in favor of predictable branches when it is relatively
                // close.
                const size_t predictableCutoff = litLens.size() / 64;
                if (numExtraLL < predictableCutoff) {
                    numExtraLens -= numExtraLens / 16;
                }
                if (numExtraML < predictableCutoff) {
                    numExtraLens -= numExtraLens / 16;
                }

                // fprintf(stderr,
                //         "%zu: %zu -> %zu = %zu + %zu (%zu)\n",
                //         litLens.size(),
                //         llBits,
                //         numExtraLens,
                //         numExtraLL,
                //         numExtraML,
                //         bestExtraLens);
                if (numExtraLens < bestExtraLens) {
                    bestExtraLens = numExtraLens;
                    bestLLBits    = llBits;
                }
            }
            llParam = bestLLBits;
        }

        size_t llBits = llParam;
        size_t mlBits = 8 - llBits;
        // fprintf(stderr, "Selected llbits = %zu\n", llBits);

        std::vector<uint8_t> tokens;
        std::vector<uint32_t> extraLens;
        tokens.reserve(litLens.size());

        const uint32_t llMask = (1u << llBits) - 1;
        const uint32_t mlMask = (1u << mlBits) - 1;

        for (size_t i = 0; i < litLens.size(); ++i) {
            const uint32_t ll     = litLens[i];
            const uint32_t ml     = matchLens[i];
            uint8_t const llToken = ll < llMask ? ll : llMask;
            uint8_t const mlToken = ml < mlMask ? ml : mlMask;
            uint8_t const token   = llToken | (mlToken << llBits);

            tokens.push_back(token);
            if (llToken == llMask) {
                extraLens.push_back(ll - llMask);
            }
            if (mlToken == mlMask) {
                extraLens.push_back(ml - mlMask);
            }
        }

        uint8_t header[ZL_VARINT_LENGTH_64 + 1];
        header[0]               = llBits | (mlBits << 4);
        uint32_t const srcSize  = ZL_Input_numElts(input);
        const size_t headerSize = 1 + ZL_varintEncode(srcSize, header + 1);
        ZL_Encoder_sendCodecHeader(eictx, &header, headerSize);

        ZL_ERR_IF_ERR(writeStream(eictx, 0, lits));
        ZL_ERR_IF_ERR(writeStream(eictx, 1, tokens));
        ZL_ERR_IF_ERR(writeStream(eictx, 2, offsets));
        ZL_ERR_IF_ERR(writeStream(eictx, 3, extraLens));

        return ZL_returnSuccess();
    }

    template <size_t kLen>
    static void copy(void* dst, void const* src)
    {
        char tmp[kLen];
        memcpy(tmp, src, kLen);
        memcpy(dst, tmp, kLen);
    }

   public:
    static size_t decodeImpl2(
            uint8_t const* __restrict lits,
            size_t numLits,
            uint8_t const* __restrict tokens,
            offset_t const* __restrict offsets,
            uint32_t const* __restrict extraLens,
            size_t numSeqs,
            uint8_t* __restrict out,
            size_t outCapacity)
    {
        ZL_REQUIRE(kIsIndex);
        static_assert(kLLBits + kMLBits == 8, "");
        uint8_t* const base           = out;
        auto litEnd                   = lits + numLits;
        uint8_t const* const outLimit = out + outCapacity
                - 4 * std::max(16, 1 << std::max(kLLBits, kMLBits));
        uint8_t const* const litLimit =
                lits + numLits - 4 * std::max(16, 1 << kLLBits);
        size_t i = 0;
        for (;;) {
            if (i + 4 > numSeqs) {
                break;
            }
            if (out > outLimit) {
                break;
            }
            if (lits > litLimit) {
                break;
            }
            std::array<std::array<uint8_t, 1 << kMLBits>, 4> matches;
#pragma clang loop unroll(full)
            for (size_t u = 0; u < 4; ++u) {
                memcpy(matches[u].data(), base + offsets[i + u], 1 << kMLBits);
            }

            // TODO: If I control this loop carefully with intrinsics, can it be
            // faster?
#pragma clang loop unroll(full)
            for (size_t u = 0; u < 4; ++u) {
                size_t ll = tokens[i] & kLLMask;
                size_t ml = (tokens[i] >> kLLBits) & kMLMask;
                memcpy(out, lits, 1 << kLLBits);
                if (ZL_UNLIKELY(ll == kLLMask)) {
                    memcpy(out + kLLMask, lits + kLLMask, *extraLens);
                    ll += *extraLens++;
                    if (ZL_UNLIKELY(
                                out + ll > outLimit || lits + ll > litLimit)) {
                        out += ll;
                        lits += ll;
                        if (ZL_UNLIKELY(ml == kMLMask)) {
                            ml += *extraLens++;
                        }
                        memcpy(out, base + offsets[i], ml);
                        out += ml;
                        ++i;
                        break;
                    }
                }
                out += ll;
                lits += ll;

                memcpy(out, matches[u].data(), 1 << kMLBits);
                if (ZL_UNLIKELY(ml == kMLMask)) {
                    memcpy(out + kMLMask,
                           base + offsets[i] + kMLMask,
                           *extraLens);
                    ml += *extraLens++;
                    if (ZL_UNLIKELY(out + ml > outLimit)) {
                        out += ml;
                        ++i;
                        break;
                    }
                }
                out += ml;
                ++i;
            }
        }
        while (i < numSeqs) {
            size_t ll = tokens[i] & kLLMask;
            size_t ml = (tokens[i] >> kLLBits) & kMLMask;
            if (ll == kLLMask) {
                ll += *extraLens++;
            }
            if (ml == kMLMask) {
                ml += *extraLens++;
            }
            memcpy(out, lits, ll);
            out += ll;
            lits += ll;
            uint8_t const* const match =
                    kIsIndex ? base + offsets[i] : out - offsets[i];
            memcpy(out, match, ml);
            out += ml;
            ++i;
        }
        memcpy(out, lits, (litEnd - lits));
        out += litEnd - lits;
        return out - base;
    }

    // TODO: Lost speed here when fixing it
    static size_t decodeImpl(
            uint8_t const* __restrict lits,
            size_t numLits,
            uint8_t const* __restrict tokens,
            offset_t const* __restrict offsets,
            uint32_t const* __restrict extraLens,
            size_t numSeqs,
            uint8_t* __restrict out,
            size_t outCapacity)
    {
        static_assert(kLLBits + kMLBits == 8, "");
        uint8_t* const base           = out;
        auto litEnd                   = lits + numLits;
        uint8_t const* const outLimit = out + outCapacity
                - std::max(16, 1 << std::max(kLLBits, kMLBits)) - 16;
        uint8_t const* const litLimit =
                lits + numLits - std::max(16, 1 << kLLBits);
        for (size_t i = 0; i < numSeqs; ++i) {
            size_t ll = tokens[i] & kLLMask;
            size_t ml = (tokens[i] >> kLLBits) & kMLMask;
            if (ZL_LIKELY(out < outLimit && lits < litLimit)) {
                copy<1 << kLLBits>(out, lits);
            } else {
                ZS_safecopy(out, lits, ll, ZS_wo_no_overlap);
            }
            if (ZL_UNLIKELY(ll == kLLMask)) {
                ll += *extraLens;
                if (ZL_LIKELY(out + ll <= outLimit && lits + ll <= litLimit)) {
                    ZS_wildcopy(
                            out + kLLMask,
                            lits + kLLMask,
                            *extraLens,
                            ZS_wo_no_overlap);
                } else {
                    ZS_safecopy(
                            out + kLLMask,
                            lits + kLLMask,
                            *extraLens,
                            ZS_wo_no_overlap);
                }
                ++extraLens;
            }
            lits += ll;
            out += ll;
            uint8_t const* const match =
                    kIsIndex ? base + offsets[i] : out - offsets[i];
            if (ZL_LIKELY(out < outLimit)) {
                // copy<1 << kMLBits>(out, match);
                if (ZL_LIKELY(offsets[i] >= 16)) {
                    for (size_t i_2 = 0; i_2 < (1 << kMLBits); i_2 += 16) {
                        memcpy(out + i_2, match + i_2, 16);
                    }
                } else {
                    ZS_wildcopy(out, match, 1 << kMLBits, ZS_wo_src_before_dst);
                }
            } else {
                ZS_safecopy(out, match, ml, ZS_wo_src_before_dst);
            }
            if (ZL_UNLIKELY(ml == kMLMask)) {
                ml += *extraLens;
                if (ZL_LIKELY(out + ml <= outLimit)) {
                    ZS_wildcopy(
                            out + kMLMask,
                            match + kMLMask,
                            *extraLens,
                            ZS_wo_src_before_dst);
                } else {
                    ZS_safecopy(
                            out + kMLMask,
                            match + kMLMask,
                            *extraLens,
                            ZS_wo_src_before_dst);
                }
                ++extraLens;
            }
            out += ml;
        }
        memcpy(out, lits, (litEnd - lits));
        out += litEnd - lits;
        return out - base;
    }

   private:
    static ZL_Report decode(
            ZL_Decoder* dictx,
            ZL_Input const* inputs[]) noexcept
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);

        auto lits      = inputs[0];
        auto tokens    = inputs[1];
        auto offsets   = inputs[2];
        auto extraLens = inputs[3];

        auto header      = ZL_Decoder_getCodecHeader(dictx);
        const uint8_t* h = (const uint8_t*)header.start;
        ZL_ERR_IF_LT(header.size, 2, corruption);
        auto llBits = *h & 0xf;
        ++h;
        ZL_TRY_LET_CONST(
                uint64_t, srcSize, ZL_varintDecode(&h, h + header.size - 1));

        ZL_Output* out = ZL_Decoder_create1OutStream(dictx, srcSize, 1);
        // auto impl = kIsIndex ? decodeImpl2 : decodeImpl;

        auto impl = decodeImpl;
        if (llBits == 1) {
            impl = Transform<kIsIndex, 1, 8 - 1>::decodeImpl;
        } else if (llBits == 2) {
            impl = Transform<kIsIndex, 2, 8 - 2>::decodeImpl;
        } else if (llBits == 3) {
            impl = Transform<kIsIndex, 3, 8 - 3>::decodeImpl;
        } else if (llBits == 4) {
            impl = Transform<kIsIndex, 4, 8 - 4>::decodeImpl;
        } else if (llBits == 5) {
            impl = Transform<kIsIndex, 5, 8 - 5>::decodeImpl;
        } else {
            ZL_ERR(GENERIC, "Unsupported LLBits %u", llBits);
        }
        size_t const size =
                impl((uint8_t const*)ZL_Input_ptr(lits),
                     ZL_Input_numElts(lits),
                     (uint8_t const*)ZL_Input_ptr(tokens),
                     (offset_t const*)ZL_Input_ptr(offsets),
                     (uint32_t const*)ZL_Input_ptr(extraLens),
                     ZL_Input_numElts(tokens),
                     (uint8_t*)ZL_Output_ptr(out),
                     srcSize);
        ZL_ERR_IF_ERR(ZL_Output_commit(out, srcSize));
        ZL_ERR_IF_NE(size, srcSize, corruption);

        return ZL_returnSuccess();
    }

    static std::array<ZL_Type, 4> constexpr kOutTypes = {
        ZL_Type_serial,
        ZL_Type_serial,
        ZL_Type_numeric,
        ZL_Type_numeric,
    };

    static ZL_TypedGraphDesc gd()
    {
        ZL_TypedGraphDesc desc = { .CTid = int(CustomCodecIDs::LZ)
                                           | (kIsIndex ? 1 : 0),
                                   .inStreamType   = ZL_Type_serial,
                                   .outStreamTypes = kOutTypes.data(),
                                   .nbOutStreams   = kOutTypes.size() };
        return desc;
    }

   public:
    static ZL_NodeID create(ZL_Compressor* cgraph, LzParameters params)
    {
        ZL_NodeID node = ZL_Compressor_getNode(cgraph, "lz_research.lz");
        if (node == ZL_NODE_ILLEGAL) {
            ZL_TypedEncoderDesc desc = {
                .gd          = gd(),
                .transform_f = encode,
                .name        = "!lz_research.lz",
            };
            node = ZL_Compressor_registerTypedEncoder(cgraph, &desc);
        }
        LocalParams lp;
        lp.addIntParam(0, params.llBits);
        return ZL_Compressor_cloneNode(cgraph, node, lp.get());
    }

    // static ZL_GraphID storeGraph(ZL_Compressor* cgraph)
    // {
    //     auto node                     = create(cgraph);
    //     std::array<ZL_GraphID, 4> out = {
    //         ZL_GRAPH_STORE, ZL_GRAPH_STORE, ZL_GRAPH_STORE,
    //         ZL_GRAPH_STORE
    //     };
    //     return ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, node, out.data(), out.size());
    // }

    // static ZL_GraphID compressGraph(ZL_Compressor* cgraph, bool fast)
    // {
    //     std::array<ZL_GraphID, 2> q = { ZL_GRAPH_FSE, ZL_GRAPH_STORE };
    //     ZL_GraphID const quantize =
    //     ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, ZL_NODE_QUANTIZE_LENGTHS, q.data(), q.size());
    //     auto node = create(cgraph);
    //     // std::array<ZL_GraphID, 4> out = {
    //     //     ZL_GRAPH_HUFFMAN, ZL_GRAPH_HUFFMAN, ZL_GRAPH_STORE,
    //     quantize}; std::array<ZL_GraphID, 4> out = {
    //         ZL_GRAPH_HUFFMAN,
    //         ZL_GRAPH_HUFFMAN,
    //         fast ? ZL_GRAPH_STORE : varByteGraph(cgraph),
    //         smallIntGraph(
    //                 cgraph, fast ? ZL_GRAPH_STORE : ZL_GRAPH_HUFFMAN,
    //                 quantize)
    //     };
    //     return ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, node, out.data(), out.size());
    // }

    // static ZL_GraphID lz4Graph(ZL_Compressor* cgraph, bool huff)
    // {
    //     std::array<ZL_GraphID, 2> q = { ZL_GRAPH_FSE, ZL_GRAPH_STORE };
    //     ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, ZL_NODE_QUANTIZE_LENGTHS, q.data(), q.size());
    //     auto node      = create(cgraph);
    //     auto rangePack = ZL_Compressor_registerStaticGraph_fromNode1o(
    //             cgraph, ZL_NODE_RANGE_PACK, ZL_GRAPH_STORE);
    //     // std::array<ZL_GraphID, 4> out = {
    //     //     ZL_GRAPH_STORE, ZL_GRAPH_STORE, ZL_GRAPH_STORE, quantize};
    //     std::array<ZL_GraphID, 4> out = {
    //         huff ? ZL_GRAPH_HUFFMAN : ZL_GRAPH_STORE,
    //         ZL_GRAPH_STORE,
    //         ZL_GRAPH_STORE,
    //         smallIntGraph(cgraph, ZL_GRAPH_STORE, rangePack)
    //     };
    //     return ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, node, out.data(), out.size());
    // }

   public:
    static void registerDecoder(ZL_DCtx* dctx)
    {
        ZL_TypedDecoderDesc desc = {
            .gd          = gd(),
            .transform_f = decode,
            .name        = "!zl_research.lz",
        };
        ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &desc));
    }

    // static char const* str(Mode mode)
    // {
    //     switch (mode) {
    //         case Mode::kStore:
    //             return "store";
    //             break;
    //         case Mode::kLz4:
    //             return "lz4";
    //             break;
    //         case Mode::kLz4Huf:
    //             return "lz4-huf";
    //             break;
    //         case Mode::kZstdFast:
    //             return "zstd-fast";
    //             break;
    //         case Mode::kZstdSlow:
    //             return "zstd-slow";
    //             break;
    //     };
    // }

    // static std::vector<uint8_t>
    // compress(uint8_t const* src, size_t srcSize, Mode mode, int level)
    // {
    //     std::vector<uint8_t> compressed(srcSize * 4);
    //     auto cgraph = ZL_Compressor_create();
    //     ZL_GraphID graph;
    //     switch (mode) {
    //         case Mode::kStore:
    //             graph = storeGraph(cgraph);
    //             break;
    //         case Mode::kLz4:
    //             graph = lz4Graph(cgraph, false);
    //             break;
    //         case Mode::kLz4Huf:
    //             graph = lz4Graph(cgraph, true);
    //             break;
    //         case Mode::kZstdFast:
    //             graph = compressGraph(cgraph, true);
    //             break;
    //         case Mode::kZstdSlow:
    //             graph = compressGraph(cgraph, false);
    //             break;
    //     };
    //     // auto graph = store ? storeGraph(cgraph) : lz4Graph(cgraph);
    //     ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph,
    //     graph)); ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
    //             cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    //     ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
    //             cgraph, ZL_CParam_compressedChecksum,
    //             ZL_TernaryParam_disable));
    //     ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
    //             cgraph, ZL_CParam_contentChecksum,
    //             ZL_TernaryParam_disable));
    //     ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
    //             cgraph, ZL_CParam_compressionLevel, level));
    //     auto ret = ZL_compress_usingCompressor(
    //             compressed.data(), compressed.size(), src, srcSize,
    //             cgraph);
    //     ZL_REQUIRE_SUCCESS(ret);
    //     ZL_Compressor_free(cgraph);
    //     compressed.resize(ZL_validResult(ret));
    //     return compressed;
    // }

    // static void
    // benchmark(uint8_t const* src, size_t srcSize, Mode mode, int level)
    // {
    //     auto compressed = compress(src, srcSize, mode, level);
    //     std::vector<uint8_t> decompressed(srcSize);
    //     ZL_DCtx* dctx = ZL_DCtx_create();
    //     ZL_REQUIRE_SUCCESS(
    //             ZL_DCtx_setStreamArena(dctx, ZL_DataArenaType_stack));
    //     registerCustomTransforms(dctx);
    //     auto start = std::chrono::steady_clock::now();
    //     for (size_t i = 0; i < kIters; ++i) {
    //         ZL_REQUIRE_SUCCESS(ZL_DCtx_decompress(
    //                 dctx,
    //                 decompressed.data(),
    //                 decompressed.size(),
    //                 compressed.data(),
    //                 compressed.size()));
    //     }
    //     auto stop                         =
    //     std::chrono::steady_clock::now(); std::chrono::nanoseconds
    //     duration = stop - start; double const mbps  = (srcSize * kIters *
    //     1000.0) / duration.count(); double const ratio = double(srcSize)
    //     / compressed.size(); ZL_REQUIRE(!memcmp(src, decompressed.data(),
    //     srcSize)); fprintf(stderr,
    //             "llbits = %zu, mlbits = %zu, index = %u, mode = %9s,
    //             level = %d, ratio = %.2f, MB/s = %.2f\n", kLLBits,
    //             kMLBits, unsigned(kIsIndex ? 1 : 0), str(mode), level,
    //             ratio,
    //             mbps);
    //     ZL_DCtx_free(dctx);
    // }

    // static void write(
    //         char const* prefix,
    //         uint8_t const* src,
    //         size_t srcSize,
    //         Mode mode,
    //         int level)
    // {
    //     auto name = fmt::format(
    //             "{}.{}.{}.{}.{}.{}.zs",
    //             prefix,
    //             str(mode),
    //             level,
    //             kIsIndex ? 1 : 0,
    //             kLLBits,
    //             kMLBits);
    //     auto compressed = compress(src, srcSize, mode, level);
    //     ZL_REQUIRE(folly::writeFile(compressed, name.c_str()));
    // }
};

NodeID registerLz(Compressor& compressor, LzParameters params)
{
    return Transform<false, 0, 8>::create(compressor.get(), params);
}

void registerLz(DCtx& dctx)
{
    Transform<false, 0, 8>::registerDecoder(dctx.get());
}

} // namespace openzl::lz
