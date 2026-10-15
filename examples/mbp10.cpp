// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// Databento MBP-10 (market by price, 10 levels) parser for OpenZL.
//
// The parser is a function graph that only runs standard codecs:
//   1. dispatch each record's fields into global streams (ts_event,
//      ts_recv, ts_in_delta, sequence) and a book stream;
//   2. dispatch the book stream by instrument_id;
//   3. dispatch each instrument's book records into one stream per field,
//      and delta-code the price and book level fields;
//   4. send every stream, read as little-endian integers, to a successor
//      graph, by default the Transformer.
// Frames decode with the universal OpenZL decoder: `-d` below registers
// nothing.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>
#include "openzl/codecs/zl_clustering.h"
#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_delta.h"
#include "openzl/codecs/zl_dispatch.h"
#include "openzl/codecs/zl_field_lz.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_transformer.h"
#include "openzl/codecs/zl_zigzag.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_version.h"

namespace {

// DBN Mbp10Msg (DBN v1-v3, little-endian, 368 bytes): record header
// (length, rtype, publisher_id, instrument_id, ts_event), event fields,
// then 10 levels of {bid_px, ask_px, bid_sz, ask_sz, bid_ct, ask_ct}.
constexpr size_t kRecordSize  = 368;
constexpr uint8_t kMbp10Rtype = 0x0A;
constexpr size_t kLevels      = 10;

// Step 1: byte ranges of a record and the stream each one goes to.
// Records are sorted by ts_recv, so timestamps and the venue sequence
// number compress best in record order, across instruments.
enum Stream : unsigned { kBook, kEvent, kRecv, kInDelta, kSequence, kOther };
struct Range {
    size_t offset, width;
    Stream stream;
};
constexpr Range kRecordRanges[] = {
    { 0, 8, kBook },      // length, rtype, publisher_id, instrument_id
    { 8, 8, kEvent },     // ts_event
    { 16, 16, kBook },    // price, size, action, side, flags, depth
    { 32, 8, kRecv },     // ts_recv
    { 40, 4, kInDelta },  // ts_in_delta
    { 44, 4, kSequence }, // sequence
    { 48, 320, kBook },   // levels
};
// What remains of a record in the book stream.
constexpr size_t kBookSize         = 344;
constexpr size_t kInstrumentInBook = 4;

// Step 3: fields of a book record, each one a stream. Within one
// instrument, consecutive prices and book levels barely change, so those
// fields are delta-coded (then zigzag-coded) before the successor.
struct Field {
    size_t width;
    bool delta;
};
const std::vector<Field>& bookFields()
{
    static const std::vector<Field> fields = [] {
        std::vector<Field> f = {
            { 2, false }, // length, rtype
            { 2, false }, // publisher_id
            { 4, false }, // instrument_id
            { 8, true },  // price
            { 4, false }, // size
            { 2, false }, // action, side
            { 1, false }, // flags
            { 1, false }, // depth
        };
        for (size_t lvl = 0; lvl < kLevels; ++lvl) {
            f.push_back({ 8, true }); // bid_px
            f.push_back({ 8, true }); // ask_px
            f.push_back({ 4, true }); // bid_sz
            f.push_back({ 4, true }); // ask_sz
            f.push_back({ 4, true }); // bid_ct
            f.push_back({ 4, true }); // ask_ct
        }
        return f;
    }();
    return fields;
}

// Stream tags for ZL_CLUSTERING_TAG_METADATA_ID: stable across files and
// instruments, so a trained clustering graph can reuse its decisions.
constexpr int kTagBookField   = 0; // + field index
constexpr int kTagEvent       = 100;
constexpr int kTagRecv        = 101;
constexpr int kTagInDelta     = 102;
constexpr int kTagSequence    = 103;
constexpr int kTagInstruments = 104;
constexpr int kTagRunLengths  = 105;

uint32_t readLE32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8
            | static_cast<uint32_t>(p[2]) << 16
            | static_cast<uint32_t>(p[3]) << 24;
}

bool isMbp10(const uint8_t* p, size_t avail)
{
    return avail >= kRecordSize && p[0] * 4u == kRecordSize
            && p[1] == kMbp10Rtype;
}

// Size of the DBN metadata block at the start of a file, or 0.
size_t dbnHeaderSize(const uint8_t* src, size_t size)
{
    if (size < 8 || std::memcmp(src, "DBN", 3) != 0)
        return 0;
    const size_t len = 8 + static_cast<size_t>(readLE32(src + 4));
    return len <= size ? len : 0;
}

// Local int parameters: 0 disables the per-instrument dispatch; set when
// the successor graph accepts any number of inputs.
constexpr int kSplitByInstrumentPid = 1;
constexpr int kMultiInputPid        = 2;
// Keeps a frame under the runtime node limit. Instruments beyond this many
// share one set of streams.
constexpr size_t kMaxInstruments = 32;

// Dispatch instructions that merge consecutive segments with the same tag.
struct Segments {
    std::vector<size_t> sizes;
    std::vector<unsigned> tags;
    unsigned nbTags = 0;
    void add(unsigned tag, size_t size)
    {
        if (!tags.empty() && tags.back() == tag) {
            sizes.back() += size;
        } else {
            tags.push_back(tag);
            sizes.push_back(size);
        }
    }
    ZL_RESULT_OF(ZL_EdgeList) run(ZL_Edge* edge) const
    {
        ZL_DispatchInstructions in{};
        in.segmentSizes = sizes.data();
        in.tags         = tags.data();
        in.nbSegments   = sizes.size();
        in.nbTags       = nbTags;
        return ZL_Edge_runDispatchNode(edge, &in);
    }
};

// Reads a serial edge as little-endian integers, optionally delta- and
// zigzag-codes it, and queues it for the successor graph.
ZL_Report toNumeric(
        ZL_Graph* graph,
        ZL_Edge* edge,
        size_t width,
        int tag,
        bool delta,
        std::vector<ZL_Edge*>& numeric)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_TRY_LET(
            ZL_EdgeList,
            conv,
            ZL_Edge_runNode(edge, ZL_Node_interpretAsLE(8 * width)));
    edge = conv.edges[0];
    if (delta) {
        ZL_TRY_LET(ZL_EdgeList, d, ZL_Edge_runNode(edge, ZL_NODE_DELTA_INT));
        ZL_TRY_LET(ZL_EdgeList, z, ZL_Edge_runNode(d.edges[0], ZL_NODE_ZIGZAG));
        edge = z.edges[0];
    }
    ZL_ERR_IF_ERR(
            ZL_Edge_setIntMetadata(edge, ZL_CLUSTERING_TAG_METADATA_ID, tag));
    numeric.push_back(edge);
    return ZL_returnSuccess();
}

// Step 3: one stream per field of the book records in @p records.
ZL_Report
splitFields(ZL_Graph* graph, ZL_Edge* records, std::vector<ZL_Edge*>& numeric)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    const auto& fields = bookFields();
    const size_t nbRecs =
            ZL_Input_numElts(ZL_Edge_getData(records)) / kBookSize;
    Segments seg;
    seg.nbTags = static_cast<unsigned>(fields.size());
    seg.sizes.reserve(nbRecs * fields.size());
    seg.tags.reserve(nbRecs * fields.size());
    for (size_t r = 0; r < nbRecs; ++r) {
        for (size_t f = 0; f < fields.size(); ++f) {
            seg.tags.push_back(static_cast<unsigned>(f));
            seg.sizes.push_back(fields[f].width);
        }
    }
    ZL_TRY_LET(ZL_EdgeList, out, seg.run(records));
    // Field tags and sizes repeat every record: generic LZ removes them.
    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(out.edges[0], ZL_GRAPH_COMPRESS_GENERIC));
    ZL_ERR_IF_ERR(
            ZL_Edge_setDestination(out.edges[1], ZL_GRAPH_COMPRESS_GENERIC));
    for (size_t f = 0; f < fields.size(); ++f)
        ZL_ERR_IF_ERR(toNumeric(
                graph,
                out.edges[f + 2],
                fields[f].width,
                kTagBookField + static_cast<int>(f),
                fields[f].delta,
                numeric));
    return ZL_returnSuccess();
}

// Step 2: book records by instrument, then step 3 for each instrument.
ZL_Report
splitInstruments(ZL_Graph* graph, ZL_Edge* book, std::vector<ZL_Edge*>& numeric)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    const auto* src =
            static_cast<const uint8_t*>(ZL_Input_ptr(ZL_Edge_getData(book)));
    const size_t size = ZL_Input_numElts(ZL_Edge_getData(book));
    std::unordered_map<uint32_t, unsigned> slots;
    Segments seg;
    for (size_t pos = 0; pos < size; pos += kBookSize) {
        const uint32_t id = readLE32(src + pos + kInstrumentInBook);
        auto it           = slots.find(id);
        if (it == slots.end()) {
            const auto slot = static_cast<unsigned>(
                    std::min(slots.size(), kMaxInstruments));
            it = slots.emplace(id, slot).first;
        }
        seg.add(it->second, kBookSize);
    }
    seg.nbTags =
            static_cast<unsigned>(std::min(slots.size(), kMaxInstruments + 1));
    ZL_TRY_LET(ZL_EdgeList, out, seg.run(book));
    // Which instrument each run of records belongs to, and the run lengths.
    ZL_ERR_IF_ERR(ZL_Edge_setIntMetadata(
            out.edges[0], ZL_CLUSTERING_TAG_METADATA_ID, kTagInstruments));
    ZL_ERR_IF_ERR(ZL_Edge_setIntMetadata(
            out.edges[1], ZL_CLUSTERING_TAG_METADATA_ID, kTagRunLengths));
    numeric.push_back(out.edges[0]);
    numeric.push_back(out.edges[1]);
    for (size_t i = 0; i < seg.nbTags; ++i)
        ZL_ERR_IF_ERR(splitFields(graph, out.edges[i + 2], numeric));
    return ZL_returnSuccess();
}

ZL_Report mbp10Parser(ZL_Graph* graph, ZL_Edge* inputs[], size_t n) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ERR_IF_NE(n, 1, graphParameter_invalid);
    const auto* input     = ZL_Edge_getData(inputs[0]);
    const auto* src       = static_cast<const uint8_t*>(ZL_Input_ptr(input));
    const size_t size     = ZL_Input_numElts(input);
    const auto successors = ZL_Graph_getCustomGraphs(graph);
    ZL_ERR_IF_NE(successors.nbGraphIDs, 1, graphParameter_invalid);
    const auto split = ZL_Graph_getLocalIntParam(graph, kSplitByInstrumentPid);
    const bool byInstrument =
            split.paramId != kSplitByInstrumentPid || split.paramValue != 0;
    try {
        // Step 1. Anything that is not an MBP-10 record (the DBN metadata
        // block, other record types, trailing bytes) is kept verbatim.
        Segments seg;
        seg.nbTags       = kOther;
        size_t pos       = dbnHeaderSize(src, size);
        size_t nbRecords = 0;
        if (pos) {
            seg.add(kOther, pos);
            seg.nbTags = kOther + 1;
        }
        while (pos < size) {
            if (isMbp10(src + pos, size - pos)) {
                for (const auto& r : kRecordRanges)
                    seg.add(r.stream, r.width);
                pos += kRecordSize;
                ++nbRecords;
                continue;
            }
            const size_t len  = src[pos] * 4u;
            const size_t take = (len && len <= size - pos) ? len : size - pos;
            seg.add(kOther, take);
            seg.nbTags = kOther + 1;
            pos += take;
        }
        if (nbRecords == 0)
            return ZL_Edge_setDestination(inputs[0], ZL_GRAPH_COMPRESS_GENERIC);
        ZL_TRY_LET(ZL_EdgeList, out, seg.run(inputs[0]));
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                out.edges[0], ZL_GRAPH_COMPRESS_GENERIC));
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                out.edges[1], ZL_GRAPH_COMPRESS_GENERIC));
        ZL_Edge** streams = out.edges + 2;
        if (seg.nbTags > kOther)
            ZL_ERR_IF_ERR(ZL_Edge_setDestination(
                    streams[kOther], ZL_GRAPH_COMPRESS_GENERIC));

        std::vector<ZL_Edge*> numeric;
        ZL_ERR_IF_ERR(toNumeric(
                graph, streams[kEvent], 8, kTagEvent, false, numeric));
        ZL_ERR_IF_ERR(
                toNumeric(graph, streams[kRecv], 8, kTagRecv, false, numeric));
        ZL_ERR_IF_ERR(toNumeric(
                graph, streams[kInDelta], 4, kTagInDelta, false, numeric));
        ZL_ERR_IF_ERR(toNumeric(
                graph, streams[kSequence], 4, kTagSequence, false, numeric));
        if (byInstrument) {
            ZL_ERR_IF_ERR(splitInstruments(graph, streams[kBook], numeric));
        } else {
            ZL_ERR_IF_ERR(splitFields(graph, streams[kBook], numeric));
        }
        // A multi-input successor (the Transformer, a trained clustering
        // graph) receives every stream at once.
        const auto multi = ZL_Graph_getLocalIntParam(graph, kMultiInputPid);
        if (multi.paramId == kMultiInputPid && multi.paramValue) {
            ZL_ERR_IF_ERR(ZL_Edge_setParameterizedDestination(
                    numeric.data(),
                    numeric.size(),
                    successors.graphids[0],
                    nullptr));
        } else {
            for (ZL_Edge* edge : numeric)
                ZL_ERR_IF_ERR(ZL_Edge_setParameterizedDestination(
                        &edge, 1, successors.graphids[0], nullptr));
        }
        return ZL_returnSuccess();
    } catch (const std::bad_alloc&) {
        ZL_ERR(allocation);
    } catch (...) {
        ZL_ERR(GENERIC);
    }
}

} // namespace

/// Registers the MBP-10 parser. Every field stream goes to @p successor,
/// for example ZL_GRAPH_TRANSFORMER_NUMERIC.
ZL_GraphID registerMbp10Parser(
        ZL_Compressor* compressor,
        ZL_GraphID successor,
        bool splitByInstrument)
{
    const ZL_Type serial       = ZL_Type_serial;
    const ZL_IntParam params[] = {
        { kSplitByInstrumentPid, splitByInstrument ? 1 : 0 },
        { kMultiInputPid,
          ZL_Compressor_Graph_isVariableInput(compressor, successor) ? 1 : 0 },
    };
    ZL_FunctionGraphDesc desc{};
    desc.name                  = "mbp10_parser";
    desc.graph_f               = mbp10Parser;
    desc.inputTypeMasks        = &serial;
    desc.nbInputs              = 1;
    desc.customGraphs          = &successor;
    desc.nbCustomGraphs        = 1;
    desc.localParams.intParams = { params, 2 };
    return ZL_Compressor_registerFunctionGraph(compressor, &desc);
}

namespace {

// ---------------------------------------------------------------------------
// Command-line driver
// ---------------------------------------------------------------------------

enum class Successor { Transformer, FieldLz, Generic };

struct Options {
    Successor successor    = Successor::Transformer;
    bool splitByInstrument = true;
    size_t frameRecords    = 1u << 20;
};

using Bytes = std::string;

bool compressFrame(const Options& opt, const char* src, size_t size, Bytes& out)
{
    ZL_Compressor* compressor = ZL_Compressor_create();
    ZL_CCtx* cctx             = ZL_CCtx_create();
    bool ok                   = compressor && cctx;
    if (ok) {
        ZL_GraphID next = ZL_GRAPH_TRANSFORMER_NUMERIC;
        if (opt.successor == Successor::FieldLz)
            next = ZL_GRAPH_FIELD_LZ;
        if (opt.successor == Successor::Generic)
            next = ZL_GRAPH_COMPRESS_GENERIC;
        const ZL_GraphID parser =
                registerMbp10Parser(compressor, next, opt.splitByInstrument);
        ok = !ZL_isError(ZL_Compressor_setParameter(
                     compressor,
                     ZL_CParam_formatVersion,
                     ZL_MAX_FORMAT_VERSION))
                && !ZL_isError(
                        ZL_Compressor_selectStartingGraphID(compressor, parser))
                && !ZL_isError(ZL_CCtx_refCompressor(cctx, compressor));
    }
    if (ok) {
        const size_t start = out.size();
        out.resize(start + ZL_compressBound(size));
        const ZL_Report r = ZL_CCtx_compress(
                cctx, &out[start], out.size() - start, src, size);
        if (ZL_isError(r)) {
            std::fprintf(
                    stderr,
                    "compression failed: %s\n",
                    ZL_CCtx_getErrorContextString(cctx, r));
            ok = false;
        } else {
            out.resize(start + ZL_validResult(r));
        }
    }
    ZL_CCtx_free(cctx);
    ZL_Compressor_free(compressor);
    return ok;
}

// End of the next frame starting at @p pos: at most @p maxRecords whole
// records, plus the DBN metadata block for the first frame. Returns @p pos
// when more input is needed to complete a record, unless @p eof.
size_t frameEnd(
        const uint8_t* src,
        size_t size,
        size_t pos,
        bool first,
        size_t maxRecords,
        bool eof)
{
    size_t end = first ? dbnHeaderSize(src, size) : pos;
    for (size_t recs = 0; end < size && recs < maxRecords; ++recs) {
        const size_t len = src[end] * 4u;
        if (!len || len > size - end)
            return eof ? size : end;
        end += len;
    }
    return end;
}

// Every frame holds whole records, so frames are independent.
bool compress(const Options& opt, const Bytes& in, Bytes& out)
{
    const auto* src = reinterpret_cast<const uint8_t*>(in.data());
    size_t pos      = 0;
    do {
        const size_t end =
                frameEnd(src, in.size(), pos, pos == 0, opt.frameRecords, true);
        if (!compressFrame(opt, in.data() + pos, end - pos, out))
            return false;
        pos = end;
    } while (pos < in.size());
    return true;
}

// Same frames as compress(), reading the input a frame at a time.
bool compressFile(
        const Options& opt,
        FILE* in,
        FILE* out,
        size_t& inSize,
        size_t& outSize)
{
    Bytes buf, frame;
    bool eof = false, first = true;
    size_t extra = 0;
    inSize  = 0;
    outSize = 0;
    while (!eof || !buf.empty() || first) {
        const size_t want =
                opt.frameRecords * kRecordSize + (first ? 1 << 20 : 0) + extra;
        if (!eof && buf.size() < want) {
            const size_t have = buf.size();
            buf.resize(want);
            const size_t n = std::fread(&buf[have], 1, want - have, in);
            buf.resize(have + n);
            eof = n < want - have;
            if (std::ferror(in))
                return false;
            inSize += n;
        }
        const auto* src = reinterpret_cast<const uint8_t*>(buf.data());
        const size_t end =
                frameEnd(src, buf.size(), 0, first, opt.frameRecords, eof);
        if (end == 0 && !eof) {
            extra += want;
            continue;
        }
        extra = 0;
        frame.clear();
        if (!compressFrame(opt, buf.data(), end, frame)
            || std::fwrite(frame.data(), 1, frame.size(), out) != frame.size())
            return false;
        outSize += frame.size();
        buf.erase(0, end);
        first = false;
    }
    return true;
}

// Universal decoder only: no graph is registered to decompress.
template <typename Sink>
bool decompress(const Bytes& in, Sink&& sink)
{
    ZL_DCtx* dctx = ZL_DCtx_create();
    bool ok       = dctx != nullptr;
    Bytes frame;
    for (size_t pos = 0; ok && pos < in.size();) {
        const ZL_Report csize =
                ZL_getCompressedSize(in.data() + pos, in.size() - pos);
        const ZL_Report dsize =
                ZL_getDecompressedSize(in.data() + pos, in.size() - pos);
        ok = !ZL_isError(csize) && !ZL_isError(dsize);
        if (!ok)
            break;
        frame.resize(ZL_validResult(dsize));
        const ZL_Report r = ZL_DCtx_decompress(
                dctx,
                &frame[0],
                frame.size(),
                in.data() + pos,
                ZL_validResult(csize));
        ok = !ZL_isError(r) && ZL_validResult(r) == frame.size() && sink(frame);
        pos += ZL_validResult(csize);
    }
    ZL_DCtx_free(dctx);
    return ok;
}

bool decompress(const Bytes& in, Bytes& out)
{
    return decompress(in, [&](const Bytes& f) {
        out += f;
        return true;
    });
}

// ---------------------------------------------------------------------------
// Synthetic MBP-10 data (no exchange data is shipped with this example)
// ---------------------------------------------------------------------------

struct Rng {
    uint64_t s;
    uint64_t next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    uint64_t below(uint64_t n)
    {
        return next() % n;
    }
};

void put(Bytes& b, size_t off, uint64_t v, size_t width)
{
    for (size_t i = 0; i < width; ++i)
        b[off + i] = static_cast<char>(v >> (8 * i));
}

// A DBN-shaped file: metadata block, then MBP-10 records from a few
// interleaved instruments, each with its own random-walk order book.
Bytes synthetic(size_t nbRecords, uint64_t seed, size_t nbInstruments = 6)
{
    struct Book {
        uint32_t id;
        int64_t mid;
        uint32_t sz[2][kLevels], ct[2][kLevels], seq;
    };
    constexpr int64_t kTick = 250000000; // 0.25 in 1e-9 units
    Rng rng{ seed | 1 };
    std::vector<Book> books;
    for (uint32_t i = 0; i < nbInstruments; ++i) {
        Book b{};
        b.id  = 1000 + 37 * i;
        b.mid = (6000 + 100 * i) * 4 * kTick;
        for (auto& side : b.sz)
            for (auto& v : side)
                v = 1 + static_cast<uint32_t>(rng.below(60));
        for (auto& side : b.ct)
            for (auto& v : side)
                v = 1 + static_cast<uint32_t>(rng.below(12));
        books.push_back(b);
    }
    const char meta[] = "synthetic MBP-10 sample, not market data";
    Bytes out("DBN\3", 4);
    out.append(4, '\0');
    put(out, 4, sizeof meta, 4);
    out.append(meta, sizeof meta);
    uint64_t ts = 1762300800000000000ull;
    for (size_t r = 0; r < nbRecords; ++r) {
        // Front instruments trade far more often than back ones.
        Book& b = r < books.size()
                ? books[r]
                : books[rng.below(3) ? rng.below(2) : rng.below(books.size())];
        const int side   = static_cast<int>(rng.below(2));
        const size_t lvl = rng.below(4) ? rng.below(3) : rng.below(kLevels);
        char action      = 'A';
        switch (rng.below(10)) {
            case 0:
                action = 'T';
                break;
            case 1:
            case 2:
            case 3:
                action          = 'C';
                b.sz[side][lvl] = 1 + b.sz[side][lvl] / 2;
                break;
            case 4:
                action = 'M';
                b.mid += (rng.below(2) ? 1 : -1) * kTick;
                break;
            default:
                b.sz[side][lvl] += 1 + static_cast<uint32_t>(rng.below(5));
                b.ct[side][lvl] += 1;
        }
        ts += rng.below(4) ? rng.below(2000) : rng.below(5000000);
        Bytes rec(kRecordSize, '\0');
        put(rec, 0, kRecordSize / 4, 1);
        put(rec, 1, kMbp10Rtype, 1);
        put(rec, 2, 1, 2);
        put(rec, 4, b.id, 4);
        put(rec, 8, ts, 8);
        const int64_t px =
                b.mid + (side ? 1 : -1) * kTick * (1 + 2 * (int64_t)lvl);
        put(rec, 16, static_cast<uint64_t>(px / 2), 8);
        put(rec, 24, b.sz[side][lvl], 4);
        rec[28] = action;
        rec[29] = side ? 'A' : 'B';
        put(rec, 30, 128, 1);
        put(rec, 31, lvl, 1);
        put(rec, 32, ts + 20000 + rng.below(3000), 8);
        put(rec, 40, 10000 + rng.below(500), 4);
        put(rec, 44, ++b.seq, 4);
        for (size_t l = 0; l < kLevels; ++l) {
            const size_t o    = 48 + 32 * l;
            const int64_t off = kTick * (1 + 2 * static_cast<int64_t>(l)) / 2;
            put(rec, o, static_cast<uint64_t>((b.mid - 2 * off) / 2), 8);
            put(rec, o + 8, static_cast<uint64_t>((b.mid + 2 * off) / 2), 8);
            put(rec, o + 16, b.sz[0][l], 4);
            put(rec, o + 20, b.sz[1][l], 4);
            put(rec, o + 24, b.ct[0][l], 4);
            put(rec, o + 28, b.ct[1][l], 4);
        }
        out += rec;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

bool roundTrips(
        const char* name,
        const Options& opt,
        const Bytes& in,
        size_t* csize)
{
    Bytes c, d;
    const bool ok = compress(opt, in, c) && decompress(c, d) && d == in;
    std::fprintf(
            stderr,
            "%-34s %9zu -> %8zu bytes  %s\n",
            name,
            in.size(),
            c.size(),
            ok ? "ok" : "FAILED");
    if (csize)
        *csize = c.size();
    return ok;
}

int selfTest()
{
    bool ok = true;
    Options opt;
    const Bytes data = synthetic(20000, 42);
    const Bytes manyInstruments = synthetic(1000, 43, 100);
    size_t split = 0, merged = 0;
    ok &= roundTrips("synthetic, per instrument", opt, data, &split);
    ok &= roundTrips(
            "synthetic, 100 instruments", opt, manyInstruments, nullptr);
    Options noSplit           = opt;
    noSplit.splitByInstrument = false;
    ok &= roundTrips("synthetic, no instrument split", noSplit, data, &merged);
    Options small      = opt;
    small.frameRecords = 777;
    ok &= roundTrips("synthetic, 777-record frames", small, data, nullptr);
    Options fieldLz   = opt;
    fieldLz.successor = Successor::FieldLz;
    ok &= roundTrips("synthetic, field LZ successor", fieldLz, data, nullptr);
    if (split >= merged) {
        std::fprintf(stderr, "per-instrument streams did not help\n");
        ok = false;
    }

    // Reading the input a frame at a time gives the same frames.
    {
        Bytes whole, streamed(2 * data.size(), '\0');
        FILE* in      = std::tmpfile();
        FILE* out     = std::tmpfile();
        size_t inSize = 0, outSize = 0;
        const bool same = in && out
                && std::fwrite(data.data(), 1, data.size(), in) == data.size()
                && std::fseek(in, 0, SEEK_SET) == 0
                && compressFile(small, in, out, inSize, outSize)
                && compress(small, data, whole);
        if (same) {
            std::fseek(out, 0, SEEK_SET);
            streamed.resize(std::fread(&streamed[0], 1, streamed.size(), out));
            ok &= streamed == whole && outSize == whole.size()
                    && inSize == data.size();
        } else {
            ok = false;
        }
        if (in)
            std::fclose(in);
        if (out)
            std::fclose(out);
        std::fprintf(
                stderr,
                "file streaming matches in-memory     %s\n",
                ok ? "ok" : "FAILED");
    }

    // A non-DBN stream whose first byte is zero needs more input before EOF.
    // This exercises the reader's no-frame-boundary growth path.
    {
        Options edge = opt;
        edge.frameRecords = 1;
        Bytes edgeData((1u << 20) + 4096, '\x01');
        edgeData[0] = '\0';
        FILE* in  = std::tmpfile();
        FILE* out = std::tmpfile();
        Bytes encoded, restored;
        size_t inSize = 0, outSize = 0;
        bool edgeOk = in && out
                && std::fwrite(edgeData.data(), 1, edgeData.size(), in)
                        == edgeData.size()
                && std::fseek(in, 0, SEEK_SET) == 0
                && compressFile(edge, in, out, inSize, outSize)
                && std::fseek(out, 0, SEEK_END) == 0;
        if (edgeOk) {
            const long encodedSize = std::ftell(out);
            edgeOk = encodedSize >= 0 && std::fseek(out, 0, SEEK_SET) == 0;
            if (edgeOk) {
                encoded.resize(static_cast<size_t>(encodedSize));
                edgeOk = std::fread(encoded.data(), 1, encoded.size(), out)
                        == encoded.size();
            }
        }
        edgeOk = edgeOk && decompress(encoded, restored) && restored == edgeData
                && inSize == edgeData.size() && outSize == encoded.size();
        if (in)
            std::fclose(in);
        if (out)
            std::fclose(out);
        ok &= edgeOk;
        std::fprintf(
                stderr,
                "streamed zero-length prefix grows buffer  %s\n",
                edgeOk ? "ok" : "FAILED");
    }

    // Inputs the parser must carry through unchanged.
    const Bytes records = data.substr(dbnHeaderSize(
            reinterpret_cast<const uint8_t*>(data.data()), data.size()));
    ok &= roundTrips("records without DBN header", opt, records, nullptr);
    Bytes mixed = records.substr(0, 50 * kRecordSize);
    mixed += Bytes("\x04\x16\x01\x00\x10\x00\x00\x00", 8); // short record
    mixed += records.substr(50 * kRecordSize, 50 * kRecordSize);
    mixed[3 * kRecordSize + 1] = 0x01; // an MBP-1 rtype with MBP-10 length
    mixed += "trailing";
    ok &= roundTrips("other records and trailing bytes", opt, mixed, nullptr);
    ok &= roundTrips("empty input", opt, Bytes(), nullptr);
    Rng rng{ 7 };
    Bytes noise(5000, '\0');
    for (auto& ch : noise)
        ch = static_cast<char>(rng.next());
    ok &= roundTrips("random bytes", opt, noise, nullptr);

    // Altered frames must fail to decode, never decode to different data.
    const Bytes part = data.substr(0, 3000 * kRecordSize);
    Bytes frame;
    ok &= compress(opt, part, frame);
    size_t rejected = 0;
    for (size_t i = 0; i < 64; ++i) {
        Bytes bad = frame, d;
        bad[(i * 7919 + 13) % bad.size()] ^= 0x20;
        if (!decompress(bad, d))
            ++rejected;
        else if (d != part)
            ok = false;
    }
    ok &= rejected > 0;
    std::fprintf(
            stderr,
            "altered frames rejected          %2zu/64 %s\n",
            rejected,
            ok ? "ok" : "FAILED");
    std::fprintf(stderr, "self-test %s\n", ok ? "passed" : "FAILED");
    return ok ? 0 : 1;
}

bool readFile(const char* path, Bytes& out)
{
    FILE* f = std::fopen(path, "rb");
    if (!f)
        return false;
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

bool writeFile(const char* path, const Bytes& data)
{
    FILE* f = std::fopen(path, "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return (std::fclose(f) == 0) && ok;
}

int usage()
{
    std::fprintf(
            stderr,
            "usage: mbp10 [options] IN.dbn OUT.zl   compress\n"
            "       mbp10 -d IN.zl OUT.dbn          decompress (universal decoder)\n"
            "       mbp10 --generate N OUT.dbn      write N synthetic records\n"
            "       mbp10 --self-test\n"
            "options: --frame-records N (default 1048576)\n"
            "         --no-instrument-split\n"
            "         --successor transformer|field-lz|generic\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    std::vector<const char*> args;
    bool decode = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--self-test")
            return selfTest();
        if (a == "--generate" && i + 2 < argc) {
            const Bytes data = synthetic(std::stoul(argv[i + 1]), 42);
            return writeFile(argv[i + 2], data) ? 0 : 1;
        }
        if (a == "-d") {
            decode = true;
        } else if (a == "--no-instrument-split") {
            opt.splitByInstrument = false;
        } else if (a == "--frame-records" && i + 1 < argc) {
            opt.frameRecords = std::stoul(argv[++i]);
        } else if (a == "--successor" && i + 1 < argc) {
            const std::string s = argv[++i];
            if (s == "transformer")
                opt.successor = Successor::Transformer;
            else if (s == "field-lz")
                opt.successor = Successor::FieldLz;
            else if (s == "generic")
                opt.successor = Successor::Generic;
            else
                return usage();
        } else if (a.size() > 1 && a[0] == '-') {
            return usage();
        } else {
            args.push_back(argv[i]);
        }
    }
    if (args.size() != 2 || opt.frameRecords == 0)
        return usage();
    FILE* out = std::fopen(args[1], "wb");
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", args[1]);
        return 1;
    }
    bool ok       = false;
    size_t inSize = 0, outSize = 0;
    if (decode) {
        Bytes in;
        ok     = readFile(args[0], in);
        inSize = in.size();
        ok     = ok && decompress(in, [&](const Bytes& f) {
                 outSize += f.size();
                 return std::fwrite(f.data(), 1, f.size(), out) == f.size();
             });
    } else if (FILE* in = std::fopen(args[0], "rb")) {
        ok = compressFile(opt, in, out, inSize, outSize);
        std::fclose(in);
    }
    ok = (std::fclose(out) == 0) && ok;
    if (!ok) {
        std::fprintf(
                stderr,
                "%s failed\n",
                decode ? "decompression" : "compression");
        return 1;
    }
    std::fprintf(stderr, "%zu -> %zu bytes\n", inSize, outSize);
    return 0;
}
