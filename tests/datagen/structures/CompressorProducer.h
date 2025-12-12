// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <ostream>

#include "openzl/zl_compressor.h"

#include "openzl/common/assertion.h"

#include "tests/datagen/DataProducer.h"
#include "tests/datagen/random_producer/RandWrapper.h"
#include "tests/datagen/structures/LocalParamsProducer.h"

namespace openzl::tests::datagen {

/**
 * This is a work in progress class. The motivation was originally to provide
 * good inputs for the compressor serialization module, which requires both
 * a source compressor which is fully configured as well as destination
 * compressors which are only partially configured. So this has the capability
 * to produce multiple copies of the same logical compressor, some copies of
 * which only have the non-serializable components set up.
 *
 * TODO: Currently the compressors that this produces **do not actually work**.
 * You can't actually invoke compression on them. Work to change that will
 * follow. Your contributions are welcome.
 *
 * TODO: This class does a lot. It should probably be split up.
 */
class CompressorProducer : public DataProducer<std::shared_ptr<ZL_Compressor>> {
   public:
    using Compressor = std::shared_ptr<ZL_Compressor>;

   public:
    explicit CompressorProducer(std::shared_ptr<RandWrapper> generator)
            : DataProducer<Compressor>(), rw_(std::move(generator))
    {
    }

    /**
     * Constructs a single randomly generated compressor.
     */
    Compressor operator()(RandWrapper::NameType) override
    {
        return make();
    }

    void print(std::ostream& os) const override
    {
        os << "CompressorProducer()";
    }

    /**
     * Constructs a single randomly generated compressor.
     */
    Compressor make();

    /**
     * Constructs multiple copies of the same compressor.
     *
     * Full compressors are fully constructed.
     *
     * Base compressors have only the base graph components that wouldn't be
     * set up by serialized compressor materialization. I.e., they are
     * compressors into which it is suitable to deserialize a serialized
     * version of a full compressor. When that's done, the result should be
     * logically identical to the full compressor.
     */
    std::pair<std::vector<Compressor>, std::vector<Compressor>> make_multi(
            size_t num_full_compressors,
            size_t num_base_compressors);

   private:
    std::shared_ptr<RandWrapper> rw_;
};

/**
 * Single-use class to build one or more randomly-constructed compressors. The
 * CompressorProducer delegates each generation request to an instance of this
 * class constructed for that request.
 */
class RandomCompressorMultiBuilder {
   public:
    using Compressor = std::shared_ptr<ZL_Compressor>;

   public:
    explicit RandomCompressorMultiBuilder(std::shared_ptr<RandWrapper> rw)
            : rw_(std::move(rw)), lpp_(rw_)
    {
    }

    /**
     * Constructs a single randomly generated compressor.
     */
    Compressor make() &&;

    /**
     * Constructs multiple copies of the same compressor.
     *
     * Full compressors are fully constructed.
     *
     * Base compressors have only the base graph components that wouldn't be
     * set up by serialized compressor materialization. I.e., they are
     * compressors into which it is suitable to deserialize a serialized
     * version of a full compressor. When that's done, the result should be
     * logically identical to the full compressor.
     */
    std::pair<std::vector<Compressor>, std::vector<Compressor>> make_multi(
            size_t num_full_compressors,
            size_t num_base_compressors) &&;

   private:
    // The name of a graph component in each of the compressors being built.
    using NameVec = std::vector<std::string>;

    struct TypeSpec {
        TypeSpec(bool ser, bool stu, bool num, bool str, bool mul)
                : serial_(ser),
                  struct_(stu),
                  numeric_(num),
                  string_(str),
                  multi_(mul)
        {
        }

        /* implicit */ TypeSpec(ZL_Type t)
                : TypeSpec(
                          !!(t & ZL_Type_serial),
                          !!(t & ZL_Type_struct),
                          !!(t & ZL_Type_numeric),
                          !!(t & ZL_Type_string),
                          false)
        {
        }

        static TypeSpec all()
        {
            return TypeSpec(true, true, true, true, true);
        }

        static TypeSpec multi()
        {
            return TypeSpec(false, false, false, false, true);
        }

        bool is_all() const
        {
            return serial_ && struct_ && numeric_ && string_ && multi_;
        }

        bool is_singular() const
        {
            return serial_ + struct_ + numeric_ + string_ + multi_ == 1;
        }

        void assert_singular() const
        {
            ZL_ASSERT(is_singular());
        }

        ZL_Type types() const
        {
            return static_cast<ZL_Type>(
                    (serial_ ? ZL_Type_serial : 0)
                    | (struct_ ? ZL_Type_struct : 0)
                    | (numeric_ ? ZL_Type_numeric : 0)
                    | (string_ ? ZL_Type_string : 0));
        }

        ZL_Type type() const
        {
            ZL_ASSERT(!multi_);
            assert_singular();
            if (serial_) {
                return ZL_Type_serial;
            }
            if (struct_) {
                return ZL_Type_struct;
            }
            if (numeric_) {
                return ZL_Type_numeric;
            }
            if (string_) {
                return ZL_Type_string;
            }
            ZL_REQUIRE_FAIL("Unreachable??");
        }

        bool serial_{ false };
        bool struct_{ false };
        bool numeric_{ false };
        bool string_{ false };
        bool multi_{ false };
    };

    std::string make_name(const std::string& prefix);

    ZL_IDType make_ctid();

    LocalParams make_params();
    LocalParams pick_params();
    LocalParams get_params();

    std::vector<ZL_Type> make_types_vec();
    std::vector<ZL_Type> make_input_types_vec(TypeSpec ts = TypeSpec::all());

    void record_node(NameVec node_names);
    void record_standard_nodes();

    NameVec register_pipe_node();
    NameVec register_split_node();
    NameVec register_typed_node(TypeSpec ts = TypeSpec::all());
    NameVec register_vo_node(TypeSpec ts = TypeSpec::all());
    NameVec register_mi_node();
    NameVec register_node(TypeSpec ts = TypeSpec::all());
    std::optional<NameVec> try_pick_node(TypeSpec ts = TypeSpec::all());
    NameVec clone_node(const NameVec& base_nodes);

    NameVec get_node(TypeSpec ts);

    std::vector<NameVec> get_successor_graphs_for_node(
            const std::vector<std::string>& node_names,
            size_t depth);

    void record_graph(NameVec graph_names);
    void record_standard_graphs();

    std::optional<NameVec> try_pick_graph(TypeSpec ts);

    NameVec make_graph_by_composing_node(TypeSpec ts, size_t depth);
    NameVec make_multi_input_graph(TypeSpec ts, size_t depth);
    NameVec clone_graph(
            const std::vector<std::string>& base_names,
            size_t depth);
    NameVec make_graph(TypeSpec ts, size_t depth);

    NameVec build_graph(TypeSpec ts, size_t depth);

    template <
            typename F,
            typename R = typename std::invoke_result<F, ZL_Compressor*>::type>
    std::vector<R> for_all_compressors(const F& func)
    {
        std::vector<R> results;
        results  = for_full_compressors(func);
        auto tmp = for_base_compressors(func);
        results.insert(
                results.end(),
                std::make_move_iterator(tmp.begin()),
                std::make_move_iterator(tmp.end()));
        return results;
    }

    template <
            typename F,
            typename R = typename std::invoke_result<F, ZL_Compressor*>::type>
    std::vector<R> for_full_compressors(const F& func)
    {
        return for_compressors(full_compressors_, func);
    }

    template <
            typename F,
            typename R = typename std::invoke_result<F, ZL_Compressor*>::type>
    std::vector<R> for_base_compressors(const F& func)
    {
        return for_compressors(base_compressors_, func);
    }

    template <
            typename F,
            typename R = typename std::invoke_result<F, ZL_Compressor*>::type>
    std::vector<R> for_compressors(
            const std::vector<Compressor>& compressors,
            const F& func)
    {
        std::vector<R> results;
        for (const auto& c : compressors) {
            auto ptr    = c.get();
            auto result = func(ptr);
            results.push_back(std::move(result));
        }
        return results;
    }

    std::shared_ptr<RandWrapper> rw_;

    LocalParamsProducer lpp_;

    std::vector<Compressor> full_compressors_;
    std::vector<Compressor> base_compressors_;
    std::vector<ZL_Compressor*> all_compressors_;

    // Each inner vec records the name of the component in question across all
    // the compressors that are being built.
    std::vector<NameVec> nodes_;

    std::vector<NameVec> single_input_serial_nodes_;
    std::vector<NameVec> single_input_struct_nodes_;
    std::vector<NameVec> single_input_numeric_nodes_;
    std::vector<NameVec> single_input_string_nodes_;
    std::vector<NameVec> multi_input_nodes_;

    std::vector<NameVec> graphs_;

    std::vector<NameVec> single_input_serial_graphs_;
    std::vector<NameVec> single_input_struct_graphs_;
    std::vector<NameVec> single_input_numeric_graphs_;
    std::vector<NameVec> single_input_string_graphs_;
    std::vector<NameVec> multi_input_graphs_;

    std::vector<LocalParams> params_;

    ZL_IDType next_ctid_{ 1 };
};

} // namespace openzl::tests::datagen
