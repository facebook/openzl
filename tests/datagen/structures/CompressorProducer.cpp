// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/datagen/structures/CompressorProducer.h"

#include <set>

#include "openzl/zl_dyngraph.h"
#include "openzl/zl_graphs.h"
#include "openzl/zl_reflection.h"
#include "openzl/zl_selector.h"

#include "tests/utils.h"

namespace openzl {
namespace tests {
namespace datagen {

namespace {
struct ZS2_Compressor_Deleter {
    void operator()(ZL_Compressor* compressor)
    {
        ZL_Compressor_free(compressor);
    }
};
} // anonymous namespace

CompressorProducer::Compressor CompressorProducer::make()
{
    RandomCompressorMultiBuilder rcmb(rw_);
    return std::move(rcmb).make();
}

std::pair<
        std::vector<CompressorProducer::Compressor>,
        std::vector<CompressorProducer::Compressor>>
CompressorProducer::make_multi(
        const size_t num_full_compressors,
        const size_t num_base_compressors)
{
    RandomCompressorMultiBuilder rcmb(rw_);
    return std::move(rcmb).make_multi(
            num_full_compressors, num_base_compressors);
}

std::string RandomCompressorMultiBuilder::make_name(const std::string& prefix)
{
    std::string suffix{ "123456789" };
    ZL_REQUIRE_EQ(
            snprintf(
                    suffix.data(),
                    suffix.size(),
                    "%08x",
                    rw_->u32("component_name")),
            8);
    suffix.resize(8);
    return prefix + suffix;
}

ZL_IDType RandomCompressorMultiBuilder::make_ctid()
{
    return next_ctid_++;
}

LocalParams RandomCompressorMultiBuilder::make_params()
{
    auto params = lpp_("RandomCompressorMultiBuilder::make_params");
    params_.push_back(params.copy());
    return params;
}

LocalParams RandomCompressorMultiBuilder::pick_params()
{
    const auto idx = rw_->range("params_idx", (size_t)0, params_.size());
    if (idx == params_.size()) {
        return {};
    }
    return params_[idx].copy();
}

LocalParams RandomCompressorMultiBuilder::get_params()
{
    if (rw_->range("should_make_new_params", 0, 3) == 0) {
        return make_params();
    }
    return pick_params();
}

std::vector<ZL_Type> RandomCompressorMultiBuilder::make_types_vec()
{
    std::vector<ZL_Type> out_types;
    const size_t num_outputs = rw_->range("num_outputs", 1, 4);
    for (size_t i = 0; i < num_outputs; i++) {
        ZL_Type type{};
        switch (rw_->range("output_type", 0, 3)) {
            case 0:
                type = ZL_Type_serial;
                break;
            case 1:
                type = ZL_Type_struct;
                break;
            case 2:
                type = ZL_Type_numeric;
                break;
            case 3:
                type = ZL_Type_string;
                break;
        }
        out_types.push_back(type);
    }
    return out_types;
}

std::vector<ZL_Type> RandomCompressorMultiBuilder::make_input_types_vec(
        const TypeSpec ts)
{
    std::vector<ZL_Type> input_types;
    size_t num_inputs;
    if (ts.multi_) {
        num_inputs = rw_->range("num_inputs", 1, 4);
    } else {
        num_inputs = 1;
    }
    for (size_t i = 0; i < num_inputs; i++) {
        bool ser = ts.serial_ || rw_->boolean("input_type_include_serial");
        bool stu = ts.struct_ || rw_->boolean("input_type_include_struct");
        bool num = ts.numeric_ || rw_->boolean("input_type_include_numeric");
        bool str = ts.string_ || rw_->boolean("input_type_include_string");
        auto t   = TypeSpec{ ser, stu, num, str, false }.types();
        if (t == 0) {
            t = ZL_Type_serial;
        }
        input_types.push_back(t);
    }
    return input_types;
}

void RandomCompressorMultiBuilder::record_node(
        std::vector<std::string> node_names)
{
    ZL_ASSERT_EQ(node_names.size(), all_compressors_.size());
    bool is_multi   = false;
    bool is_serial  = false;
    bool is_struct  = false;
    bool is_numeric = false;
    bool is_string  = false;
    for (size_t i = 0; i < all_compressors_.size(); i++) {
        const auto* c    = all_compressors_[i];
        const auto& name = node_names[i];
        const auto nid   = ZL_Compressor_getNode(c, name.c_str());
        ZL_ASSERT(ZL_NodeID_isValid(nid));
        const auto num_inputs = ZL_Compressor_Node_getNumInputs(c, nid);
        if (num_inputs != 1) {
            if (i == 0) {
                is_multi = true;
            } else {
                ZL_ASSERT(is_multi);
            }
        } else {
            ZL_ASSERT(!is_multi);
            const auto type_mask = ZL_Compressor_Node_getInput0Type(c, nid);
            if (type_mask & ZL_Type_serial) {
                if (i == 0) {
                    is_serial = true;
                } else {
                    ZL_ASSERT(is_serial);
                }
            } else {
                ZL_ASSERT(!is_serial);
            }
            if (type_mask & ZL_Type_struct) {
                if (i == 0) {
                    is_struct = true;
                } else {
                    ZL_ASSERT(is_struct);
                }
            } else {
                ZL_ASSERT(!is_struct);
            }
            if (type_mask & ZL_Type_numeric) {
                if (i == 0) {
                    is_numeric = true;
                } else {
                    ZL_ASSERT(is_numeric);
                }
            } else {
                ZL_ASSERT(!is_numeric);
            }
            if (type_mask & ZL_Type_string) {
                if (i == 0) {
                    is_string = true;
                } else {
                    ZL_ASSERT(is_string);
                }
            } else {
                ZL_ASSERT(!is_string);
            }
        }
    }

    if (is_multi) {
        multi_input_nodes_.push_back(node_names);
    } else {
        if (is_serial) {
            single_input_serial_nodes_.push_back(node_names);
        }
        if (is_struct) {
            single_input_struct_nodes_.push_back(node_names);
        }
        if (is_numeric) {
            single_input_numeric_nodes_.push_back(node_names);
        }
        if (is_string) {
            single_input_string_nodes_.push_back(node_names);
        }
    }
    nodes_.push_back(std::move(node_names));
}

void RandomCompressorMultiBuilder::record_standard_nodes()
{
    static const std::vector<ZL_NodeID> std_nodes{ {
            ZL_NODE_DELTA_INT,
            ZL_NODE_TRANSPOSE_SPLIT,
            ZL_NODE_ZIGZAG,
            ZL_NODE_DISPATCH,
            ZL_NODE_DISPATCH_STRING,
            ZL_NODE_FLOAT32_DECONSTRUCT,
            ZL_NODE_BFLOAT16_DECONSTRUCT,
            ZL_NODE_FLOAT16_DECONSTRUCT,
            ZL_NODE_FIELD_LZ,
            ZL_NODE_CONVERT_SERIAL_TO_TOKENX,
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN4,
            ZL_NODE_CONVERT_SERIAL_TO_TOKEN8,
            ZL_NODE_CONVERT_TOKEN_TO_SERIAL,
            ZL_NODE_INTERPRET_TOKEN_AS_LE,
            ZL_NODE_CONVERT_NUM_TO_TOKEN,
            ZL_NODE_INTERPRET_AS_LE8,
            ZL_NODE_INTERPRET_AS_LE16,
            ZL_NODE_INTERPRET_AS_LE32,
            ZL_NODE_INTERPRET_AS_LE64,
            ZL_NODE_CONVERT_NUM_TO_SERIAL,
            ZL_NODE_SEPARATE_STRING_COMPONENTS,
            ZS2_NODE_BITUNPACK,
            ZL_NODE_RANGE_PACK,
            ZL_NODE_MERGE_SORTED,
            ZL_NODE_PREFIX,
            ZL_NODE_DIVIDE_BY,
            ZL_NODE_CONCAT_SERIAL,
            ZL_NODE_CONCAT_NUMERIC,
            ZL_NODE_CONCAT_STRUCT,
            ZL_NODE_DEDUP_NUMERIC,
    } };

    for (const auto& nid : std_nodes) {
        auto names = for_all_compressors(
                [nid](ZL_Compressor* const c) -> std::string {
                    return ZL_Compressor_Node_getName(c, nid);
                });

        record_node(std::move(names));
    }
}

std::vector<std::string> RandomCompressorMultiBuilder::register_pipe_node()
{
    const auto dst_bound = [](const void*, size_t srcSize) noexcept {
        return srcSize;
    };
    const auto transform =
            [](void* dst, size_t, const void* src, size_t srcSize) noexcept {
                memcpy(dst, src, srcSize);
                return srcSize;
            };
    const auto name = make_name("!tests.rand_graph.nodes.pipe.");
    const auto desc = (ZL_PipeEncoderDesc){
        .CTid        = make_ctid(),
        .transform_f = transform,
        .dstBound_f  = dst_bound,
        .name        = name.c_str(),
    };
    auto names =
            for_all_compressors([&](ZL_Compressor* const c) -> std::string {
                const auto nid = ZL_Compressor_registerPipeEncoder(c, &desc);
                ZL_ASSERT(ZL_NodeID_isValid(nid));
                return ZL_Compressor_Node_getName(c, nid);
            });
    record_node(names);
    return names;
}

std::vector<std::string> RandomCompressorMultiBuilder::register_split_node()
{
    const auto name = make_name("!tests.rand_graph.nodes.split.");
    const auto transform =
            [](ZL_Encoder*, size_t[], const void*, size_t) noexcept {
                ZL_RET_R_ERR(GENERIC, "Unimplemented! Can't actually run.");
            };
    const auto lp   = get_params();
    const auto desc = (ZL_SplitEncoderDesc){
        .CTid            = make_ctid(),
        .transform_f     = transform,
        .nbOutputStreams = rw_->range("num_outputs", (size_t)1, (size_t)8),
        .localParams     = *lp,
        .name            = name.c_str(),
    };
    auto names =
            for_all_compressors([&](ZL_Compressor* const c) -> std::string {
                const auto nid = ZL_Compressor_registerSplitEncoder(c, &desc);
                ZL_ASSERT(ZL_NodeID_isValid(nid));
                return ZL_Compressor_Node_getName(c, nid);
            });
    record_node(names);
    return names;
}

std::vector<std::string> RandomCompressorMultiBuilder::register_typed_node(
        const TypeSpec ts)
{
    const auto name      = make_name("!tests.rand_graph.nodes.typed.");
    const auto transform = [](ZL_Encoder*, const ZL_Input*) noexcept {
        ZL_RET_R_ERR(GENERIC, "Unimplemented! Can't actually run.");
    };
    const auto lp        = get_params();
    const auto state_mgr = (ZL_CodecStateManager){
        .stateAlloc      = NULL, // TODO?
        .stateFree       = NULL, // TODO?
        .optionalStateID = 0,    // TODO?
    };
    const auto out_types = make_types_vec();
    const auto gd        = (ZL_TypedGraphDesc){
               .CTid           = make_ctid(),
               .inStreamType   = ts.types(),
               .outStreamTypes = out_types.data(),
               .nbOutStreams   = out_types.size(),
    };
    const auto desc = (ZL_TypedEncoderDesc){
        .gd          = gd,
        .transform_f = transform,
        .localParams = *lp,
        .name        = name.c_str(),
        .trStateMgr  = state_mgr,
    };
    auto names =
            for_all_compressors([&](ZL_Compressor* const c) -> std::string {
                const auto nid = ZL_Compressor_registerTypedEncoder(c, &desc);
                ZL_ASSERT(ZL_NodeID_isValid(nid));
                return ZL_Compressor_Node_getName(c, nid);
            });
    record_node(names);
    return names;
}

std::vector<std::string> RandomCompressorMultiBuilder::register_vo_node(
        const TypeSpec ts)
{
    const auto name      = make_name("!tests.rand_graph.nodes.vo.");
    const auto transform = [](ZL_Encoder*, const ZL_Input*) noexcept {
        ZL_RET_R_ERR(GENERIC, "Unimplemented! Can't actually run.");
    };
    const auto lp        = get_params();
    const auto state_mgr = (ZL_CodecStateManager){
        .stateAlloc      = NULL, // TODO?
        .stateFree       = NULL, // TODO?
        .optionalStateID = 0,    // TODO?
    };
    const auto so_types = make_types_vec();
    const auto vo_types = make_types_vec();
    const auto gd       = (ZL_VOGraphDesc){
              .CTid           = make_ctid(),
              .inStreamType   = ts.types(),
              .singletonTypes = so_types.data(),
              .nbSingletons   = so_types.size(),
              .voTypes        = vo_types.data(),
              .nbVOs          = vo_types.size(),
    };
    const auto desc = (ZL_VOEncoderDesc){
        .gd          = gd,
        .transform_f = transform,
        .localParams = *lp,
        .name        = name.c_str(),
        .trStateMgr  = state_mgr,
    };
    auto names =
            for_all_compressors([&](ZL_Compressor* const c) -> std::string {
                const auto nid = ZL_Compressor_registerVOEncoder(c, &desc);
                ZL_ASSERT(ZL_NodeID_isValid(nid));
                return ZL_Compressor_Node_getName(c, nid);
            });
    record_node(names);
    return names;
}

std::vector<std::string> RandomCompressorMultiBuilder::register_mi_node()
{
    const auto name      = make_name("!tests.rand_graph.nodes.mi.");
    const auto transform = [](ZL_Encoder*, const ZL_Input*[], size_t) noexcept {
        ZL_RET_R_ERR(GENERIC, "Unimplemented! Can't actually run.");
    };
    const auto lp        = get_params();
    const auto state_mgr = (ZL_CodecStateManager){
        .stateAlloc      = NULL, // TODO?
        .stateFree       = NULL, // TODO?
        .optionalStateID = 0,    // TODO?
    };
    const auto in_types = make_input_types_vec();
    const auto so_types = make_types_vec();
    const auto vo_types = make_types_vec();
    const auto gd       = (ZL_MIGraphDesc){
              .CTid                = make_ctid(),
              .inputTypes          = in_types.data(),
              .nbInputs            = in_types.size(),
              .lastInputIsVariable = rw_->boolean("last_input_is_variable"),
              .soTypes             = so_types.data(),
              .nbSOs               = so_types.size(),
              .voTypes             = vo_types.data(),
              .nbVOs               = vo_types.size(),
    };
    const auto desc = (ZL_MIEncoderDesc){
        .gd          = gd,
        .transform_f = transform,
        .localParams = *lp,
        .name        = name.c_str(),
        .trStateMgr  = state_mgr,
    };
    auto names =
            for_all_compressors([&](ZL_Compressor* const c) -> std::string {
                const auto nid = ZL_Compressor_registerMIEncoder(c, &desc);
                ZL_ASSERT(ZL_NodeID_isValid(nid));
                return ZL_Compressor_Node_getName(c, nid);
            });
    record_node(names);
    return names;
}

std::vector<std::string> RandomCompressorMultiBuilder::register_node(
        const TypeSpec ts)
{
    if (!ts.is_all()) {
        if (ts.multi_) {
            return register_mi_node();
        } else {
            switch (rw_->range("kind_of_node_to_register", 0, 1)) {
                // TODO: do these work???
                // case 0:
                //     return register_pipe_node();
                // case 1:
                //     return register_split_node();
                case 0:
                    return register_typed_node(ts);
                case 1:
                    return register_vo_node(ts);
            }
        }
    }

    switch (rw_->range("kind_of_node_to_register", 0, 4)) {
        case 0:
            return register_pipe_node();
        case 1:
            return register_split_node();
        case 2:
            return register_typed_node(ts);
        case 3:
            return register_vo_node(ts);
        case 4:
            return register_mi_node();
    }
    ZL_REQUIRE_FAIL("Unreachable!");
}

std::optional<std::vector<std::string>>
RandomCompressorMultiBuilder::try_pick_node(const TypeSpec ts)
{
    std::vector<NameVec>* choices = nullptr;
    if (ts.is_all()) {
        choices = &nodes_;
    } else {
        ts.assert_singular();
        if (ts.multi_) {
            choices = &multi_input_nodes_;
        } else if (ts.serial_) {
            choices = &single_input_serial_nodes_;
        } else if (ts.struct_) {
            choices = &single_input_struct_nodes_;
        } else if (ts.numeric_) {
            choices = &single_input_numeric_nodes_;
        } else if (ts.string_) {
            choices = &single_input_string_nodes_;
        } else {
            ZL_REQUIRE_FAIL("Unreachable??");
        }
    }

    if (choices->empty()) {
        return std::nullopt;
    }
    auto idx = rw_->range("node_idx", (size_t)0, choices->size() - 1);
    return (*choices)[idx];
}

std::vector<std::string> RandomCompressorMultiBuilder::clone_node(
        const std::vector<std::string>& base_names)
{
    const auto selection = rw_->range("how_to_transform_local_params", 0, 5);
    auto transform_localparams = [this, selection](LocalParams base_lp) {
        switch (selection) {
            case 0:
                return base_lp;
            case 1:
                return LocalParams{};
            case 2:
                return make_params();
            case 3:
                return pick_params();
            case 4:
                return lpp_.mutateParamsPreservingEquality(base_lp);
            case 5:
                return lpp_.mutateParamsPerturbingEquality(base_lp);
        }
        ZL_REQUIRE_FAIL("Unreachable!");
    };

    std::vector<std::string> names;
    for (size_t i = 0; i < full_compressors_.size(); i++) {
        ZL_Compressor* c      = full_compressors_[i].get();
        const auto& base_name = base_names[i];
        const auto base_nid   = ZL_Compressor_getNode(c, base_name.c_str());
        const auto base_localparams =
                ZL_Compressor_Node_getLocalParams(c, base_nid);
        const auto new_params =
                transform_localparams(LocalParams{ base_localparams });
        auto new_localparams      = *new_params;
        new_localparams.refParams = base_localparams.refParams;
        const auto new_nid =
                ZL_Compressor_cloneNode(c, base_nid, &new_localparams);
        const auto new_name = ZL_Compressor_Node_getName(c, new_nid);
        names.emplace_back(new_name);
    }
    for (size_t i = 0; i < base_compressors_.size(); i++) {
        names.emplace_back(base_names[i + full_compressors_.size()]);
    }
    nodes_.push_back(names);
    return names;
}

std::vector<std::string> RandomCompressorMultiBuilder::get_node(
        const TypeSpec ts)
{
    if (rw_->boolean("should_try_to_pick_node")) {
        auto opt = try_pick_node(ts);
        if (opt) {
            return std::move(*opt);
        }
    }

    if (rw_->boolean("should_try_clone_node")) {
        auto opt = try_pick_node(ts);
        if (opt) {
            return clone_node(*opt);
        }
    }

    return register_node(ts);
}

void RandomCompressorMultiBuilder::record_graph(
        std::vector<std::string> graph_names)
{
    ZL_ASSERT_EQ(graph_names.size(), all_compressors_.size());
    bool is_multi   = false;
    bool is_serial  = false;
    bool is_struct  = false;
    bool is_numeric = false;
    bool is_string  = false;
    for (size_t i = 0; i < full_compressors_.size(); i++) {
        const auto* c    = full_compressors_[i].get();
        const auto& name = graph_names[i];
        const auto gid   = ZL_Compressor_getGraph(c, name.c_str());
        ZL_ASSERT(ZL_GraphID_isValid(gid));
        const auto num_inputs = ZL_Compressor_Graph_getNumInputs(c, gid);
        if (num_inputs != 1) {
            if (i == 0) {
                is_multi = true;
            } else {
                ZL_ASSERT(is_multi);
            }
        } else {
            ZL_ASSERT(!is_multi);
            const auto type_mask = ZL_Compressor_Graph_getInput0Mask(c, gid);
            if (type_mask & ZL_Type_serial) {
                if (i == 0) {
                    is_serial = true;
                } else {
                    ZL_ASSERT(is_serial);
                }
            } else {
                ZL_ASSERT(!is_serial);
            }
            if (type_mask & ZL_Type_struct) {
                if (i == 0) {
                    is_struct = true;
                } else {
                    ZL_ASSERT(is_struct);
                }
            } else {
                ZL_ASSERT(!is_struct);
            }
            if (type_mask & ZL_Type_numeric) {
                if (i == 0) {
                    is_numeric = true;
                } else {
                    ZL_ASSERT(is_numeric);
                }
            } else {
                ZL_ASSERT(!is_numeric);
            }
            if (type_mask & ZL_Type_string) {
                if (i == 0) {
                    is_string = true;
                } else {
                    ZL_ASSERT(is_string);
                }
            } else {
                ZL_ASSERT(!is_string);
            }
        }
    }

    if (is_multi) {
        multi_input_graphs_.push_back(graph_names);
    } else {
        if (is_serial) {
            single_input_serial_graphs_.push_back(graph_names);
        }
        if (is_struct) {
            single_input_struct_graphs_.push_back(graph_names);
        }
        if (is_numeric) {
            single_input_numeric_graphs_.push_back(graph_names);
        }
        if (is_string) {
            single_input_string_graphs_.push_back(graph_names);
        }
    }
    graphs_.push_back(std::move(graph_names));
}

void RandomCompressorMultiBuilder::record_standard_graphs()
{
    static const std::vector<ZL_GraphID> std_graphs{ {
            ZL_GRAPH_STORE,
            ZL_GRAPH_FSE,
            ZL_GRAPH_HUFFMAN,
            ZL_GRAPH_ENTROPY,
            ZL_GRAPH_CONSTANT,
            ZL_GRAPH_ZSTD,
            ZL_GRAPH_BITPACK,
            ZL_GRAPH_FLATPACK,
            ZL_GRAPH_FIELD_LZ,
            ZL_GRAPH_COMPRESS_GENERIC,
            ZL_GRAPH_GENERIC_LZ_BACKEND,
            ZL_GRAPH_NUMERIC,
    } };

    for (const auto& gid : std_graphs) {
        auto names = for_all_compressors(
                [gid](ZL_Compressor* const c) -> std::string {
                    return ZL_Compressor_Graph_getName(c, gid);
                });

        record_graph(std::move(names));
    }
}

std::optional<std::vector<std::string>>
RandomCompressorMultiBuilder::try_pick_graph(const TypeSpec ts)
{
    auto pick = [this](const std::vector<NameVec>& vec)
            -> std::optional<std::vector<std::string>> {
        if (vec.empty()) {
            return std::nullopt;
        }
        auto idx = rw_->range("graph_idx", (size_t)0, vec.size() - 1);
        return vec[idx];
    };

    if (ts.is_all()) {
        return pick(graphs_);
    }
    ts.assert_singular();
    if (ts.multi_) {
        return pick(multi_input_graphs_);
    }
    if (ts.serial_) {
        return pick(single_input_serial_graphs_);
    }
    if (ts.struct_) {
        return pick(single_input_struct_graphs_);
    }
    if (ts.numeric_) {
        return pick(single_input_numeric_graphs_);
    }
    if (ts.string_) {
        return pick(single_input_string_graphs_);
    }
    ZL_ASSERT_FAIL("Unreachable!");
    return std::nullopt;
}

std::vector<std::vector<std::string>>
RandomCompressorMultiBuilder::get_successor_graphs_for_node(
        const std::vector<std::string>& node_names,
        size_t depth)
{
    std::vector<NameVec> successor_names;
    auto* const c  = all_compressors_[0];
    const auto nid = ZL_Compressor_getNode(c, node_names[0].c_str());
    ZL_ASSERT(ZL_NodeID_isValid(nid));
    const auto num_outputs = ZL_Compressor_Node_getNumOutcomes(c, nid);
    // TODO: handle variable outputs???

    for (size_t i = 0; i < num_outputs; i++) {
        const auto output_type = ZL_Compressor_Node_getOutputType(c, nid, i);
        auto graph_names       = build_graph(output_type, depth + 1);
        successor_names.push_back(std::move(graph_names));
    }
    return successor_names;
}

std::vector<std::string>
RandomCompressorMultiBuilder::make_graph_by_composing_node(
        const TypeSpec ts,
        size_t depth)
{
    const auto node_names = get_node(ts);

    const auto successor_names =
            get_successor_graphs_for_node(node_names, depth);

    std::vector<std::string> graph_names;
    for (size_t i = 0; i < full_compressors_.size(); i++) {
        auto* const c  = full_compressors_[i].get();
        const auto nid = ZL_Compressor_getNode(c, node_names[i].c_str());
        ZL_ASSERT(ZL_NodeID_isValid(nid));
        std::vector<ZL_GraphID> successor_gids;
        for (size_t j = 0; j < successor_names.size(); j++) {
            const auto gid =
                    ZL_Compressor_getGraph(c, successor_names[j][i].c_str());
            ZL_ASSERT(ZL_GraphID_isValid(gid));
            successor_gids.push_back(gid);
        }

        const auto gid = ZL_Compressor_registerStaticGraph_fromNode(
                c, nid, successor_gids.data(), successor_gids.size());
        ZL_ASSERT(ZL_GraphID_isValid(gid));
        const auto name = ZL_Compressor_Graph_getName(c, gid);
        graph_names.emplace_back(name);
    }

    for (size_t i = 0; i < base_compressors_.size(); i++) {
        // graphs produced by composition don't need to be constructed on
        // non-full compressors. They'll be regenerated by materializing a
        // serialized compressor.
        auto* const c         = base_compressors_[i].get();
        const auto store_name = ZL_Compressor_Graph_getName(c, ZL_GRAPH_STORE);
        graph_names.emplace_back(store_name);
    }
    record_graph(graph_names);
    return graph_names;
}

std::vector<std::string> RandomCompressorMultiBuilder::make_multi_input_graph(
        const TypeSpec ts,
        size_t depth)
{
    const auto name       = make_name("!tests.rand_graph.graphs.multi.");
    const auto graph_func = [](ZL_Graph*, ZL_Edge*[], size_t) noexcept {
        ZL_RET_R_ERR(GENERIC, "Unimplemented! Can't actually run.");
    };
    const auto validate_func = [](const ZL_Compressor*,
                                  const ZL_FunctionGraphDesc*) noexcept {
        // TODO: actually do some validation?
        return 1;
    };

    const auto num_graphs = rw_->range("num_successor_graphs", 0u, 3u);
    std::vector<std::vector<std::string>> successor_graph_names;
    for (size_t i = 0; i < num_graphs; i++) {
        auto graph = build_graph(TypeSpec::all(), depth + 1);
        successor_graph_names.push_back(std::move(graph));
    }

    const auto num_nodes = rw_->range("num_successor_nodes", 0u, 3u);
    std::vector<std::vector<std::string>> successor_node_names;
    for (size_t i = 0; i < num_nodes; i++) {
        auto node = get_node(TypeSpec::all());
        successor_node_names.push_back(std::move(node));
    }

    auto params = get_params();

    auto in_types = make_input_types_vec(ts);

    std::vector<std::string> graph_names;
    for (size_t i = 0; i < all_compressors_.size(); i++) {
        ZL_Compressor* const c = all_compressors_[i];

        std::vector<ZL_GraphID> gids;
        for (size_t j = 0; j < successor_graph_names.size(); j++) {
            const auto& graph_name = successor_graph_names[j][i];
            const auto gid = ZL_Compressor_getGraph(c, graph_name.c_str());
            ZL_ASSERT(ZL_GraphID_isValid(gid));
            gids.push_back(gid);
        }

        std::vector<ZL_NodeID> nids;
        for (size_t j = 0; j < successor_node_names.size(); j++) {
            const auto& node_name = successor_node_names[j][i];
            const auto nid        = ZL_Compressor_getNode(c, node_name.c_str());
            ZL_ASSERT(ZL_NodeID_isValid(nid));
            nids.push_back(nid);
        }

        const auto desc = (ZL_FunctionGraphDesc){
            .name                = name.c_str(),
            .graph_f             = graph_func,
            .validate_f          = validate_func,
            .inputTypeMasks      = in_types.data(),
            .nbInputs            = in_types.size(),
            .lastInputIsVariable = rw_->boolean("last_input_is_variable"),
            .customGraphs        = gids.data(),
            .nbCustomGraphs      = gids.size(),
            .customNodes         = nids.data(),
            .nbCustomNodes       = nids.size(),
            .localParams         = *params,
        };
        const auto gid = ZL_Compressor_registerFunctionGraph(c, &desc);
        ZL_ASSERT(ZL_GraphID_isValid(gid));
        const auto graph_name = ZL_Compressor_Graph_getName(c, gid);
        graph_names.emplace_back(graph_name);
    }
    record_graph(graph_names);
    return graph_names;
}

std::vector<std::string> RandomCompressorMultiBuilder::clone_graph(
        const std::vector<std::string>& base_names,
        size_t depth)
{
    const auto override_graphs = rw_->boolean("clone_should_override_graphs");
    std::vector<std::vector<std::string>> successor_graph_names;
    if (override_graphs) {
        const auto num_graphs = rw_->range("num_graphs", 0u, 3u);
        for (size_t i = 0; i < num_graphs; i++) {
            auto graph = build_graph(TypeSpec::all(), depth + 1);
            successor_graph_names.push_back(std::move(graph));
        }
    }

    const auto override_nodes = rw_->boolean("clone_should_override_nodes");
    std::vector<std::vector<std::string>> successor_node_names;
    if (override_nodes) {
        const auto num_nodes = rw_->range("num_nodes", 0u, 3u);
        for (size_t i = 0; i < num_nodes; i++) {
            auto node = get_node(TypeSpec::all());
            successor_node_names.push_back(std::move(node));
        }
    }

    std::optional<LocalParams> opt_lp;
    if (rw_->boolean("clone_should_override_params")) {
        opt_lp = get_params();

        const ZL_Compressor* const c = full_compressors_[0].get();
        const auto base_gid = ZL_Compressor_getGraph(c, base_names[0].c_str());
        const auto base_lp  = ZL_Compressor_Graph_getLocalParams(c, base_gid);
        LocalParams wrapped_lp{ base_lp };

        // refParams can't change.
        opt_lp->setRefParams(wrapped_lp.refParams());
    }

    std::vector<std::string> graph_names;
    for (size_t i = 0; i < full_compressors_.size(); i++) {
        ZL_Compressor* const c = full_compressors_[i].get();

        const auto& base_name = base_names[i];
        const auto base_gid   = ZL_Compressor_getGraph(c, base_name.c_str());
        ZL_ASSERT(ZL_GraphID_isValid(base_gid));

        std::vector<ZL_GraphID> gids;
        if (override_graphs) {
            for (size_t j = 0; j < successor_graph_names.size(); j++) {
                const auto& graph_name = successor_graph_names[j][i];
                const auto gid = ZL_Compressor_getGraph(c, graph_name.c_str());
                ZL_ASSERT(ZL_GraphID_isValid(gid));
                gids.push_back(gid);
            }
        }

        std::vector<ZL_NodeID> nids;
        if (override_nodes) {
            for (size_t j = 0; j < successor_node_names.size(); j++) {
                const auto& node_name = successor_node_names[j][i];
                const auto nid = ZL_Compressor_getNode(c, node_name.c_str());
                ZL_ASSERT(ZL_NodeID_isValid(nid));
                nids.push_back(nid);
            }
        }

        const auto desc = (ZL_ParameterizedGraphDesc){
            .name           = NULL,
            .graph          = base_gid,
            .customGraphs   = override_graphs ? gids.data() : NULL,
            .nbCustomGraphs = override_graphs ? gids.size() : 0,
            .customNodes    = override_nodes ? nids.data() : NULL,
            .nbCustomNodes  = override_nodes ? nids.size() : 0,
            .localParams    = opt_lp ? &**opt_lp : NULL,
        };
        const auto gid = ZL_Compressor_registerParameterizedGraph(c, &desc);
        ZL_ASSERT(ZL_GraphID_isValid(gid));
        const auto graph_name = ZL_Compressor_Graph_getName(c, gid);
        graph_names.emplace_back(graph_name);
    }

    for (size_t i = 0; i < base_compressors_.size(); i++) {
        // graphs produced by re-parameterization don't need to be
        // constructed on non-full compressors. They'll be regenerated by
        // materializing a serialized compressor.
        auto* const c         = base_compressors_[i].get();
        const auto store_name = ZL_Compressor_Graph_getName(c, ZL_GRAPH_STORE);
        graph_names.emplace_back(store_name);
    }
    record_graph(graph_names);
    return graph_names;
}

std::vector<std::string> RandomCompressorMultiBuilder::make_graph(
        TypeSpec ts,
        size_t depth)
{
    if (ts.is_all()) {
        switch (rw_->range("graph_should_be_multi", 0, 1)) {
            case 0:
                ts = TypeSpec::multi();
                break;
            case 1:
                switch (rw_->range("graph_input_type", 0, 3)) {
                    case 0:
                        ts = TypeSpec{ ZL_Type_serial };
                        break;
                    case 1:
                        ts = TypeSpec{ ZL_Type_struct };
                        break;
                    case 2:
                        ts = TypeSpec{ ZL_Type_numeric };
                        break;
                    case 3:
                        ts = TypeSpec{ ZL_Type_string };
                        break;
                }
                break;
        }
    }
    if (rw_->boolean("should_try_clone_graph")) {
        auto graph_names_opt = try_pick_graph(ts);
        if (graph_names_opt) {
            return clone_graph(*graph_names_opt, depth);
        }
    }
    switch (rw_->range("make_graph_kind_of_graph", 0, 1)) {
        case 0:
            return make_graph_by_composing_node(ts, depth);
        case 1:
            return make_multi_input_graph(ts, depth);
            // TODO make a selector
    }
    ZL_REQUIRE_FAIL("Unreachable!");
}

std::vector<std::string> RandomCompressorMultiBuilder::build_graph(
        const TypeSpec ts,
        size_t depth)
{
    if (depth > 20 || graphs_.size() > 1000 || nodes_.size() > 1000
        || rw_->boolean("build_graph_should_try_pick_existing")) {
        auto opt = try_pick_graph(ts);
        if (opt) {
            return std::move(opt).value();
        }
    }

    return make_graph(ts, depth);
}

std::pair<
        std::vector<RandomCompressorMultiBuilder::Compressor>,
        std::vector<RandomCompressorMultiBuilder::Compressor>>
RandomCompressorMultiBuilder::make_multi(
        size_t num_full_compressors,
        size_t num_base_compressors) &&
{
    for (size_t i = 0; i < num_full_compressors; i++) {
        auto c = ZL_Compressor_create();
        ZL_ASSERT_NN(c);
        auto p = std::unique_ptr<ZL_Compressor, ZS2_Compressor_Deleter>(c);
        full_compressors_.push_back(std::move(p));
    }

    for (size_t i = 0; i < num_base_compressors; i++) {
        auto c = ZL_Compressor_create();
        ZL_ASSERT_NN(c);
        auto p = std::unique_ptr<ZL_Compressor, ZS2_Compressor_Deleter>(c);
        base_compressors_.push_back(std::move(p));
    }

    for (const auto& c : full_compressors_) {
        all_compressors_.push_back(c.get());
    }
    for (const auto& c : base_compressors_) {
        all_compressors_.push_back(c.get());
    }

    record_standard_nodes();
    record_standard_graphs();

    const auto starting_graph_names = build_graph(TypeSpec::all(), 0);

    if (rw_->boolean("should_make_extraneous_nodes")) {
        const size_t num_other_nodes =
                rw_->range("num_extraneous_nodes_to_make", 0, 32);
        for (size_t i = 0; i < num_other_nodes; i++) {
            get_node(TypeSpec::all());
        }
    }

    for (size_t i = 0; i < full_compressors_.size(); i++) {
        auto* const c = full_compressors_[i].get();
        const auto gid =
                ZL_Compressor_getGraph(c, starting_graph_names[i].c_str());
        ZL_ASSERT(ZL_GraphID_isValid(gid));
        ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(c, gid));
    }

    return { std::move(full_compressors_), std::move(base_compressors_) };
}

RandomCompressorMultiBuilder::Compressor RandomCompressorMultiBuilder::make() &&
{
    auto compressors = std::move(*this).make_multi(1, 0);
    return std::move(compressors.first[0]);
}

} // namespace datagen
} // namespace tests
} // namespace openzl
