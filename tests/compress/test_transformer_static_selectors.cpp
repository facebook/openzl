// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/compress/implicit_conversion.h"
#include "openzl/compress/selectors/transformer/static_selectors.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_selector.h"
#include "openzl/zl_version.h"
#include "tests/zstrong/test_zstrong_fixture.h"

namespace openzl::tests {
namespace {

using StaticSelectorFn = ZL_GraphID (*)(
        const ZL_Selector*,
        const ZL_Input*,
        const ZL_GraphID*,
        size_t);

struct SelectorProbe {
    StaticSelectorFn selector{};
    size_t calls{ 0 };
    bool deterministic{ true };
    bool returnedValidGraph{ true };
    bool returnedCompatibleGraph{ true };
};

void freeSelectorProbe(void*, void* ptr) noexcept
{
    delete static_cast<SelectorProbe*>(ptr);
}

ZL_GraphID probingSelector(
        const ZL_Selector* selector,
        const ZL_Input* input,
        const ZL_GraphID* customGraphs,
        size_t nbCustomGraphs) noexcept
{
    auto* probe = const_cast<SelectorProbe*>(static_cast<const SelectorProbe*>(
            ZL_Selector_getOpaquePtr(selector)));
    const ZL_GraphID selected =
            probe->selector(selector, input, customGraphs, nbCustomGraphs);
    const ZL_GraphID repeated =
            probe->selector(selector, input, customGraphs, nbCustomGraphs);

    ++probe->calls;
    probe->deterministic &= selected.gid == repeated.gid;
    probe->returnedValidGraph &= ZL_GraphID_isValid(selected);
    if (ZL_GraphID_isValid(selected)) {
        const ZL_Type inputMask =
                ZL_Selector_getInput0MaskForGraph(selector, selected);
        probe->returnedCompatibleGraph &=
                ICONV_isCompatible(ZL_Input_type(input), inputMask);
    }
    return selected;
}

template <typename T>
std::string asBytes(const std::vector<T>& values)
{
    if (values.empty()) {
        return {};
    }
    return std::string(
            reinterpret_cast<const char*>(values.data()),
            values.size() * sizeof(T));
}

class TransformerStaticSelectorTest : public ZStrongTest {
   protected:
    template <typename T>
    void checkSelector(
            const char* selectorName,
            StaticSelectorFn selector,
            const char* inputName,
            const std::vector<T>& values,
            int formatVersion     = ZL_MAX_FORMAT_VERSION,
            bool expectInvocation = true)
    {
        SCOPED_TRACE(
                testing::Message() << "selector=" << selectorName << ", input="
                                   << inputName << ", width=" << sizeof(T)
                                   << ", formatVersion=" << formatVersion);

        reset();
        setParameter(ZL_CParam_formatVersion, formatVersion);

        auto* probe = new SelectorProbe{ selector };
        const ZL_SelectorDesc desc = {
            .selector_f   = probingSelector,
            .inStreamType = ZL_Type_numeric,
            .opaque       = {
                           .ptr           = probe,
                           .freeOpaquePtr = nullptr,
                           .freeFn        = freeSelectorProbe,
            },
        };
        const ZL_GraphID graph =
                ZL_Compressor_registerSelectorGraph(cgraph_, &desc);
        ASSERT_TRUE(ZL_GraphID_isValid(graph));

        finalizeGraph(graph, sizeof(T));
        testRoundTrip(asBytes(values));

        if (expectInvocation) {
            EXPECT_GT(probe->calls, 0);
        }
        EXPECT_TRUE(probe->deterministic);
        EXPECT_TRUE(probe->returnedValidGraph);
        EXPECT_TRUE(probe->returnedCompatibleGraph);
    }

    template <typename T>
    void checkAllSelectors(
            const char* inputName,
            const std::vector<T>& values,
            int formatVersion     = ZL_MAX_FORMAT_VERSION,
            bool expectInvocation = true)
    {
        struct SelectorCase {
            const char* name;
            StaticSelectorFn selector;
        };
        const SelectorCase selectors[] = {
            { "core", SI_transformer_static_core_select },
            { "index", SI_transformer_static_index_select },
            { "fallback", SI_transformer_static_fallback_select },
        };

        for (const SelectorCase& selector : selectors) {
            checkSelector(
                    selector.name,
                    selector.selector,
                    inputName,
                    values,
                    formatVersion,
                    expectInvocation);
        }
    }
};

TEST_F(TransformerStaticSelectorTest, SatisfiesBlackBoxContractAcrossInputs)
{
    checkAllSelectors(
            "empty", std::vector<uint8_t>{}, ZL_MAX_FORMAT_VERSION, false);
    checkAllSelectors(
            "singleton",
            std::vector<uint64_t>{ 42 },
            ZL_MAX_FORMAT_VERSION,
            false);

    std::vector<uint8_t> sawtooth(4096);
    for (size_t i = 0; i < sawtooth.size(); ++i) {
        sawtooth[i] = static_cast<uint8_t>(i % 251);
    }
    checkAllSelectors("sawtooth", sawtooth);

    std::vector<uint16_t> runs(4096);
    for (size_t i = 0; i < runs.size(); ++i) {
        runs[i] = static_cast<uint16_t>((i / 7) % 257);
    }
    checkAllSelectors("runs", runs);

    std::vector<uint32_t> monotonic(4096);
    for (size_t i = 0; i < monotonic.size(); ++i) {
        monotonic[i] = static_cast<uint32_t>(i * 1009);
    }
    checkAllSelectors("monotonic", monotonic);

    std::vector<uint64_t> irregular(4096);
    uint64_t state = UINT64_C(0x9E3779B97F4A7C15);
    for (uint64_t& value : irregular) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        value = state;
    }
    checkAllSelectors("irregular", irregular);
}

TEST_F(TransformerStaticSelectorTest, SatisfiesBlackBoxContractAcrossFormats)
{
    std::vector<uint64_t> values(2048);
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = (i / 5) * 1000;
    }

    for (const int formatVersion :
         { ZL_MIN_FORMAT_VERSION, 15, ZL_MAX_FORMAT_VERSION }) {
        checkAllSelectors("format compatibility", values, formatVersion);
    }
}

} // namespace
} // namespace openzl::tests
