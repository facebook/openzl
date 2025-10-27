// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/ace/ace_compressor.h"

#include <sstream>
#include <type_traits>

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/allocation.h"
#include "openzl/shared/a1cbor.h"
#include "openzl/shared/xxhash.h"

namespace openzl {
namespace training {
namespace {

/// Hasher to implement hash() for ACECompressor, ACENodeCompressor, and
/// ACEGraphCompressor
class Hasher {
   public:
    Hasher()
    {
        XXH3_64bits_reset(&state_);
    }

    template <
            typename Int,
            typename =
                    typename std::enable_if<std::is_integral<Int>::value>::type>
    void update(Int i)
    {
        XXH3_64bits_update(&state_, &i, sizeof(i));
    }

    void update(const std::string& s)
    {
        update(s.size());
        XXH3_64bits_update(&state_, s.data(), s.size());
    }

    template <typename T>
    void update(const std::vector<T>& v)
    {
        update(v.size());
        for (const auto& e : v) {
            update(e);
        }
    }

    template <typename T>
    void update(poly::span<const T> v)
    {
        update(v.size());
        for (const auto& e : v) {
            update(e);
        }
    }

    template <typename T>
    void update(const std::unique_ptr<T>& v)
    {
        update(v == nullptr ? 0 : 1);
        update(*v);
    }

    template <typename T>
    void update(const poly::optional<T>& v)
    {
        update(int(v.has_value() ? 1 : 0));
        if (v.has_value()) {
            update(*v);
        }
    }

    void update(Type type)
    {
        update(int(type));
    }

    void update(TypeMask type)
    {
        update(int(type));
    }

    void update(const ZL_IntParam& param)
    {
        update(param.paramId);
        update(param.paramValue);
    }

    void update(const ZL_CopyParam& param)
    {
        update(param.paramId);
        update(param.paramSize);
        XXH3_64bits_update(&state_, param.paramPtr, param.paramSize);
    }

    void update(const ZL_RefParam& param)
    {
        update(param.paramId);
        update(param.paramSize);
        // paramPtr is not stable across runs, so don't hash it
    }

    void update(const LocalParams& params)
    {
        update(params.getIntParams());
        update(params.getCopyParams());
        update(params.getRefParams());
    }

    void update(const NodeParameters& params)
    {
        update(params.name);
        update(params.localParams);
    }

    void update(const GraphParameters& params)
    {
        update(params.name);
        update(params.localParams);
        if (params.customNodes.has_value() && !params.customNodes->empty()) {
            throw Exception("Custom nodes not supported!");
        }
        if (params.customGraphs.has_value() && !params.customGraphs->empty()) {
            throw Exception("Custom nodes not supported!");
        }
    }

    void update(const ACENode& node)
    {
        update(node.name);
        update(node.params);
        update(node.inputType);
        update(node.outputTypes);
    }

    void update(const ACEGraph& graph)
    {
        update(graph.name);
        update(graph.params);
        update(graph.inputTypeMask);
    }

    void update(const ACENodeCompressor& node)
    {
        update(node.node);
        update(node.successors);
    }

    void update(const ACEGraphCompressor& graph)
    {
        update(graph.graph);
    }

    void update(const ACECompressor& compressor)
    {
        update(compressor.hash());
    }

    uint64_t digest() const
    {
        return XXH3_64bits_digest(&state_);
    }

   private:
    XXH3_state_t state_{};
};
} // namespace

uint64_t ACENodeCompressor::hash() const
{
    Hasher h;
    h.update(*this);
    return h.digest();
}

ACENodeCompressor::ACENodeCompressor(const ACENodeCompressor& other) noexcept
        : node(other.node)
{
    successors.reserve(other.successors.size());
    for (const auto& s : other.successors) {
        successors.push_back(std::make_unique<ACECompressor>(*s));
    }
}

ACENodeCompressor& ACENodeCompressor::operator=(
        const ACENodeCompressor& other) noexcept
{
    if (this != &other) {
        node = other.node;
        successors.clear();
        successors.reserve(other.successors.size());
        for (const auto& s : other.successors) {
            successors.push_back(std::make_unique<ACECompressor>(*s));
        }
    }
    return *this;
}

GraphID ACENodeCompressor::build(Compressor& compressor) const
{
    auto nodeID = compressor.getNode(node.name);
    if (!nodeID.has_value()) {
        throw Exception("Node not found: " + node.name);
    }
    if (node.params.has_value()) {
        nodeID = compressor.parameterizeNode(*nodeID, *node.params);
    }
    std::vector<GraphID> graphs;
    graphs.reserve(successors.size());
    for (const auto& s : successors) {
        graphs.push_back(s->build(compressor));
    }
    return compressor.buildStaticGraph(*nodeID, graphs);
}

uint64_t ACEGraphCompressor::hash() const
{
    Hasher h;
    h.update(*this);
    return h.digest();
}

GraphID ACEGraphCompressor::build(Compressor& compressor) const
{
    auto graphID = compressor.getGraph(graph.name);
    if (!graphID.has_value()) {
        throw Exception("Graph not found: " + graph.name);
    }
    if (graph.params.has_value()) {
        graphID = compressor.parameterizeGraph(*graphID, *graph.params);
    }
    return *graphID;
}

ACECompressor::ACECompressor(
        ACENode node,
        std::vector<ACECompressor>&& successors)
{
    std::vector<std::unique_ptr<ACECompressor>> s;
    s.reserve(successors.size());
    for (auto& successor : successors) {
        s.push_back(std::make_unique<ACECompressor>(std::move(successor)));
    }
    node_ = ACENodeCompressor(std::move(node), std::move(s));
    hash_ = computeHash();
}

uint64_t ACECompressor::computeHash() const
{
    Hasher h;
    h.update(isNode() ? 1 : 0);
    if (isNode()) {
        h.update(asNode());
    } else {
        h.update(asGraph());
    }
    return h.digest();
}

namespace {
struct ArenaDeleter {
    void operator()(Arena* arena) const
    {
        ALLOC_Arena_freeArena(arena);
    }
};

void fillCBOR(A1C_Item* item, const LocalParams& params, A1C_Arena* a)
{
    if (!params.getRefParams().empty() || !params.getCopyParams().empty()) {
        throw Exception("Copy & Ref params not currently supported");
    }
    auto map = A1C_Item_map(item, 1, a);
    if (map == nullptr) {
        throw std::bad_alloc();
    }
    A1C_Item_string_refCStr(&map[0].key, "intParams");
    auto intParams = params.getIntParams();
    map            = A1C_Item_map(&map[0].val, intParams.size(), a);
    if (map == nullptr) {
        throw std::bad_alloc();
    }
    for (size_t i = 0; i < intParams.size(); ++i) {
        A1C_Item_int64(&map[i].key, intParams[i].paramId);
        A1C_Item_int64(&map[i].val, intParams[i].paramValue);
    }
}

void fillCBOR(A1C_Item* item, const GraphParameters& params, A1C_Arena* a)
{
    if (params.customGraphs.has_value() && !params.customGraphs->empty()) {
        throw Exception("customGraphs not supported");
    }
    if (params.customNodes.has_value() && !params.customNodes->empty()) {
        throw Exception("customNodes not supported");
    }

    auto map = A1C_Item_map(item, 2, a);
    if (map == nullptr) {
        throw std::bad_alloc();
    }

    A1C_Item_string_refCStr(&map[0].key, "name");
    if (params.name.has_value()) {
        A1C_Item_string_refCStr(&map[0].val, params.name->c_str());
    } else {
        A1C_Item_null(&map[0].val);
    }

    A1C_Item_string_refCStr(&map[1].key, "localParams");
    if (params.localParams.has_value()) {
        fillCBOR(&map[1].val, *params.localParams, a);
    } else {
        A1C_Item_null(&map[1].val);
    }
}

void fillCBOR(A1C_Item* item, const ACEGraph& graph, A1C_Arena* a)
{
    auto map = A1C_Item_map(item, 4, a);
    if (map == nullptr) {
        throw std::bad_alloc();
    }
    A1C_Item_string_refCStr(&map[0].key, "name");
    A1C_Item_string_refCStr(&map[0].val, graph.name.c_str());

    A1C_Item_string_refCStr(&map[2].key, "params");
    if (graph.params.has_value()) {
        fillCBOR(&map[2].val, *graph.params, a);
    } else {
        A1C_Item_null(&map[2].val);
    }

    A1C_Item_string_refCStr(&map[3].key, "inputTypeMask");
    A1C_Item_int64(&map[3].val, int(graph.inputTypeMask));
}

void fillCBOR(A1C_Item* item, const NodeParameters& params, A1C_Arena* a)
{
    auto map = A1C_Item_map(item, 2, a);
    if (map == nullptr) {
        throw std::bad_alloc();
    }
    A1C_Item_string_refCStr(&map[0].key, "name");
    if (params.name.has_value()) {
        A1C_Item_string_refCStr(&map[0].val, params.name->c_str());
    } else {
        A1C_Item_null(&map[0].val);
    }
    A1C_Item_string_refCStr(&map[1].key, "localParams");
    if (params.localParams.has_value()) {
        fillCBOR(&map[1].val, *params.localParams, a);
    } else {
        A1C_Item_null(&map[1].val);
    }
}

void fillCBOR(
        A1C_Item* item,
        const std::vector<Type>& outputTypes,
        A1C_Arena* a)
{
    auto arr = A1C_Item_array(item, outputTypes.size(), a);
    if (arr == nullptr) {
        throw std::bad_alloc();
    }
    for (size_t i = 0; i < outputTypes.size(); ++i) {
        A1C_Item_int64(&arr[i], int(outputTypes[i]));
    }
}

void fillCBOR(A1C_Item* item, const ACECompressor& compressor, A1C_Arena* a);
void fillCBOR(
        A1C_Item* item,
        const std::vector<std::unique_ptr<ACECompressor>>& successors,
        A1C_Arena* a)
{
    auto arr = A1C_Item_array(item, successors.size(), a);
    if (arr == nullptr) {
        throw std::bad_alloc();
    }
    for (size_t i = 0; i < successors.size(); ++i) {
        fillCBOR(&arr[i], *successors[i], a);
    }
}

void fillCBOR(A1C_Item* item, const ACENodeCompressor& node, A1C_Arena* a)
{
    auto map = A1C_Item_map(item, 6, a);
    if (map == nullptr) {
        throw std::bad_alloc();
    }

    A1C_Item_string_refCStr(&map[0].key, "name");
    A1C_Item_string_refCStr(&map[0].val, node.node.name.c_str());

    A1C_Item_string_refCStr(&map[2].key, "params");
    if (node.node.params.has_value()) {
        fillCBOR(&map[2].val, *node.node.params, a);
    } else {
        A1C_Item_null(&map[2].val);
    }

    A1C_Item_string_refCStr(&map[3].key, "inputType");
    A1C_Item_int64(&map[3].val, int(node.node.inputType));

    A1C_Item_string_refCStr(&map[4].key, "outputTypes");
    fillCBOR(&map[4].val, node.node.outputTypes, a);

    A1C_Item_string_refCStr(&map[5].key, "successors");
    fillCBOR(&map[5].val, node.successors, a);
}

void fillCBOR(A1C_Item* item, const ACECompressor& compressor, A1C_Arena* a)
{
    if (compressor.isGraph()) {
        auto map = A1C_Item_map(item, 1, a);
        if (map == nullptr) {
            throw std::bad_alloc();
        }
        A1C_Item_string_refCStr(&map[0].key, "graph");
        fillCBOR(&map[0].val, compressor.asGraph().graph, a);
    } else {
        auto map = A1C_Item_map(item, 1, a);
        if (map == nullptr) {
            throw std::bad_alloc();
        }
        A1C_Item_string_refCStr(&map[0].key, "node");
        fillCBOR(&map[0].val, compressor.asNode(), a);
    }
}

A1C_Item* toCBOR(const ACECompressor& compressor, A1C_Arena* a)
{
    A1C_Item* root = A1C_Item_root(a);
    if (root == nullptr) {
        throw std::bad_alloc();
    }
    fillCBOR(root, compressor, a);
    return root;
}

void typeCheck(const A1C_Item& item, A1C_ItemType expectedType)
{
    if (item.type != expectedType) {
        throw Exception("Type mismatch");
    }
}

bool isNull(const A1C_Item& item)
{
    return item.type == A1C_ItemType_null;
}

poly::span<const A1C_Pair> asMap(const A1C_Item& item)
{
    typeCheck(item, A1C_ItemType_map);
    return { item.map.items, item.map.size };
}

const A1C_Item& mapGet(const A1C_Item& item, const char* key)
{
    typeCheck(item, A1C_ItemType_map);
    A1C_Item* val = A1C_Map_get_cstr(&item.map, key);
    if (val == nullptr) {
        throw Exception("Key not found");
    }
    return *val;
}

const A1C_Item* mapTryGet(const A1C_Item& item, const char* key)
{
    typeCheck(item, A1C_ItemType_map);
    return A1C_Map_get_cstr(&item.map, key);
}

poly::span<const A1C_Item> asArray(const A1C_Item& item)
{
    typeCheck(item, A1C_ItemType_array);
    return { item.array.items, item.array.size };
}

poly::string_view asString(const A1C_Item& item)
{
    typeCheck(item, A1C_ItemType_string);
    return { item.string.data, item.string.size };
}

poly::optional<poly::string_view> asOptionalString(const A1C_Item& item)
{
    if (isNull(item)) {
        return poly::nullopt;
    }
    typeCheck(item, A1C_ItemType_string);
    return poly::string_view{ item.string.data, item.string.size };
}

int64_t asInt(const A1C_Item& item)
{
    typeCheck(item, A1C_ItemType_int64);
    return item.int64;
}

poly::optional<LocalParams> deserializeLocalParams(const A1C_Item& item)
{
    if (isNull(item)) {
        return poly::nullopt;
    }
    LocalParams localParams;
    auto intParams = mapGet(item, "intParams");
    if (!isNull(intParams)) {
        for (auto [key, val] : asMap(intParams)) {
            localParams.addIntParam(int(asInt(key)), int(asInt(val)));
        }
    }
    return localParams;
}

ACEGraphCompressor deserializeGraph(const A1C_Item& item)
{
    ACEGraph graph;

    graph.name          = asString(mapGet(item, "name"));
    graph.inputTypeMask = TypeMask(asInt(mapGet(item, "inputTypeMask")));

    const auto& params = mapGet(item, "params");
    if (!isNull(params)) {
        graph.params       = GraphParameters{};
        graph.params->name = asOptionalString(mapGet(params, "name"));
        graph.params->localParams =
                deserializeLocalParams(mapGet(params, "localParams"));
    }

    return ACEGraphCompressor(std::move(graph));
}

ACECompressor deserializeCompressor(const A1C_Item& item);

ACENodeCompressor deserializeNode(const A1C_Item& item)
{
    ACENode node;
    node.name      = asString(mapGet(item, "name"));
    node.inputType = Type(asInt(mapGet(item, "inputType")));
    for (const auto& outputType : asArray(mapGet(item, "outputTypes"))) {
        node.outputTypes.push_back(Type(asInt(outputType)));
    }
    const auto& params = mapGet(item, "params");
    if (!isNull(params)) {
        node.params       = NodeParameters{};
        node.params->name = asOptionalString(mapGet(params, "name"));
        node.params->localParams =
                deserializeLocalParams(mapGet(params, "localParams"));
    }

    std::vector<std::unique_ptr<ACECompressor>> successors;
    for (const auto& successor : asArray(mapGet(item, "successors"))) {
        successors.push_back(
                std::make_unique<ACECompressor>(
                        deserializeCompressor(successor)));
    }

    return ACENodeCompressor(std::move(node), std::move(successors));
}

ACECompressor deserializeCompressor(const A1C_Item& item)
{
    const auto* node  = mapTryGet(item, "node");
    const auto* graph = mapTryGet(item, "graph");

    if ((node == nullptr) == (graph == nullptr)) {
        throw Exception("Exactly one of \"node\" or \"graph\" must be present");
    }
    if (node != nullptr) {
        return deserializeNode(*node);
    } else {
        return deserializeGraph(*graph);
    }
}
} // namespace

ACECompressor::ACECompressor(poly::string_view serialized)
{
    std::unique_ptr<Arena, ArenaDeleter> arena(ALLOC_HeapArena_create());
    A1C_Decoder decoder;
    A1C_Decoder_init(
            &decoder,
            A1C_Arena_wrap(arena.get()),
            {
                    .maxDepth = 100,
            });
    A1C_Item* root = A1C_Decoder_decode(
            &decoder, (const uint8_t*)serialized.data(), serialized.size());
    if (root == nullptr) {
        throw Exception(
                std::string("Failed to decode: ")
                + A1C_ErrorType_getString(decoder.error.type));
    }
    *this = deserializeCompressor(*root);
}

std::string ACECompressor::serialize() const
{
    std::unique_ptr<Arena, ArenaDeleter> arena(ALLOC_HeapArena_create());
    A1C_Arena a1cArena = A1C_Arena_wrap(arena.get());
    auto item          = toCBOR(*this, &a1cArena);
    const size_t size  = A1C_Item_encodedSize(item);
    std::string serialized(size, '\0');
    const size_t actual = A1C_Item_encode(
            item, (uint8_t*)serialized.data(), serialized.size(), nullptr);
    if (actual != size) {
        throw Exception("Serialization failed");
    }
    return serialized;
}

namespace {
void prettyPrintParams(
        std::stringstream& ss,
        const std::string& prefix,
        const poly::optional<LocalParams>& localParams)
{
    if (localParams.has_value() && !localParams->getIntParams().empty()) {
        ss << prefix;
        ss << "params={";
        bool first = true;
        for (const auto& p : localParams->getIntParams()) {
            if (!first) {
                ss << ", ";
            }
            ss << p.paramId << ": " << p.paramValue;
            first = false;
        }
        ss << "}";
    }
}

void prettyPrintImpl(
        std::stringstream& ss,
        const ACECompressor& compressor,
        size_t depth)
{
    const auto depthStr = std::string(depth * 4, ' ');
    ss << depthStr;
    if (compressor.isGraph()) {
        const auto& graph = compressor.asGraph();
        ss << "Graph(name=\"" << graph.graph.name << "\"";
        if (graph.graph.params.has_value()) {
            prettyPrintParams(ss, ", ", graph.graph.params->localParams);
        }
        ss << ")";
    } else {
        const auto& node = compressor.asNode();
        ss << "Node(\n";
        ss << depthStr << "  name=\"" << node.node.name << "\"";
        if (node.node.params.has_value()) {
            prettyPrintParams(
                    ss, ",\n" + depthStr + "  ", node.node.params->localParams);
        }
        ss << ",\n" << depthStr << "  ";
        ss << "successors=[\n";
        for (const auto& s : node.successors) {
            prettyPrintImpl(ss, *s, depth + 1);
            ss << ",\n";
        }
        ss << depthStr << "  ]";
        ss << "\n" << depthStr << ")";
    }
}
} // namespace

std::string ACECompressor::prettyPrint() const
{
    std::stringstream ss;
    prettyPrintImpl(ss, *this, 0);
    return ss.str();
}

poly::optional<ACECompressionResult> benchmark(
        const Compressor& compressor,
        poly::span<const poly::span<const Input>> inputs)
{
    CCtx cctx;

    DCtx dctx;

    ACECompressionResult result{};
    for (const auto& input : inputs) {
        // TODO: For non-string inputs pre-reserve IO buffers
        std::string compressed;
        auto cStart = std::chrono::steady_clock::now();
        try {
            cctx.refCompressor(compressor);
            cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
            compressed = cctx.compress(input);
        } catch (const Exception&) {
            return poly::nullopt;
        }
        auto cStop = std::chrono::steady_clock::now();

        auto dStart       = std::chrono::steady_clock::now();
        auto roundTripped = dctx.decompress(compressed);
        auto dStop        = std::chrono::steady_clock::now();

        if (roundTripped.size() != input.size()) {
            throw Exception("Bad round trip!");
        }
        for (size_t i = 0; i < input.size(); ++i) {
            if (roundTripped[i] != input[i]) {
                throw std::runtime_error("Bad round trip!");
            }
        }

        size_t originalSize = 0;
        for (const auto& i : input) {
            originalSize += i.contentSize();
            if (i.type() == Type::String) {
                originalSize += i.numElts() * sizeof(uint32_t);
            }
        }

        result += ACECompressionResult{
            .originalSize      = originalSize,
            .compressedSize    = compressed.size(),
            .compressionTime   = cStop - cStart,
            .decompressionTime = dStop - dStart,
        };
    }

    return result;
}

poly::optional<ACECompressionResult> benchmark(
        const Compressor& compressor,
        poly::span<const Input> inputs)
{
    std::vector<poly::span<const Input>> multiInputs;
    multiInputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        multiInputs.push_back({ &input, 1 });
    }
    return benchmark(compressor, multiInputs);
}

poly::optional<ACECompressionResult> ACECompressor::benchmark(
        poly::span<const Input> inputs) const
{
    Compressor compressor;
    // TODO(terrelln): Allow parameterization
    compressor.selectStartingGraph(build(compressor));
    return openzl::training::benchmark(compressor, inputs);
}
} // namespace training
} // namespace openzl
