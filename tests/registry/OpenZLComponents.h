// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tests/registry/OpenZLComponent.h"

/**
 * To add a new component:
 * 1. Add a new enum value to OpenZLComponentID (call it ${COMPONENT})
 * 2. Add your function declaration to the components namespace called
 *    make${COMPONENT}Component()
 * 3. Add your component to the switch statement in makeComponent().
 * 4. Add your function definition to components/. Prefer to put it
 *    in components/${COMPONENT}.cpp, unless the implementation is shared
 *    with a family of components.
 *
 * Notes:
 * - Components do not need to map 1:1 to OpenZL codecs, nodes, or graphs.
 *   A single codec, node, or graph could have multiple components if it is
 *   used in multiple disparate ways.
 * - All OpenZL codecs, nodes, and graphs should be registered, otherwise they
 *   will not be covered by OpenZL's full test and fuzzer suite.
 */

namespace openzl::tests {

enum class OpenZLComponentID {
    Zstd,
    Lz4,
    Delta,
    Zigzag,
    Transpose,
    Float32Deconstruct,
    BFloat16Deconstruct,
    Float16Deconstruct,
    ConvertStructToSerial,
    ConvertSerialToStruct,
    ConvertNumToSerialLE,
    ConvertNumToStructLE,
    ConvertStructToNumLE,
    ConvertStructToNumBE,
    ConvertSerialToNumLE,
    ConvertSerialToNumBE,
    SeparateStringComponents,
    Bitunpack,
    RangePack,
    MergeSorted,
    Prefix,
    DivideBy,
    ConcatSerial,
    ConcatNumeric,
    ConcatStruct,
    ConcatString,
    DedupNumeric,
    ParseInt,
    InterleaveString,
    TokenizeStruct,
    TokenizeNumeric,
    TokenizeString,
    QuantizeOffsets,
    QuantizeLengths,
    Partition,
    Store,
    StoreString,
    Fse,
    Huffman,
    Entropy,
    Bitpack,
    Flatpack,
    Constant,
    FieldLz,
    CompressGeneric,
    BitSplit,
    BitSplitTop8,
    TryParseInt,
    SplitByParam,
    SplitStructByParam,
    SplitNumericByParam,
    SplitByExtParser,
    BitSplitFP,
    SplitByRange,
    BitSplitBF16,
    PartitionBitpack,
    SegmentNumeric,
    SegmentNumFromSerial,
    SentinelByte,
    SentinelNum,
    // Must be last enum value
    NumComponents,
};

/**
 * @returns The component for the ID
 */
inline std::unique_ptr<OpenZLComponent> makeComponent(
        OpenZLComponentID component);

namespace components {
std::unique_ptr<OpenZLComponent> makeZstdComponent();
std::unique_ptr<OpenZLComponent> makeLz4Component();
std::unique_ptr<OpenZLComponent> makeDeltaComponent();
std::unique_ptr<OpenZLComponent> makeZigzagComponent();
std::unique_ptr<OpenZLComponent> makeTransposeComponent();
std::unique_ptr<OpenZLComponent> makeFloat32DeconstructComponent();
std::unique_ptr<OpenZLComponent> makeBFloat16DeconstructComponent();
std::unique_ptr<OpenZLComponent> makeFloat16DeconstructComponent();
std::unique_ptr<OpenZLComponent> makeConvertStructToSerialComponent();
std::unique_ptr<OpenZLComponent> makeConvertSerialToStructComponent();
std::unique_ptr<OpenZLComponent> makeConvertNumToSerialLEComponent();
std::unique_ptr<OpenZLComponent> makeConvertNumToStructLEComponent();
std::unique_ptr<OpenZLComponent> makeConvertStructToNumLEComponent();
std::unique_ptr<OpenZLComponent> makeConvertStructToNumBEComponent();
std::unique_ptr<OpenZLComponent> makeConvertSerialToNumLEComponent();
std::unique_ptr<OpenZLComponent> makeConvertSerialToNumBEComponent();
std::unique_ptr<OpenZLComponent> makeSeparateStringComponentsComponent();
std::unique_ptr<OpenZLComponent> makeBitunpackComponent();
std::unique_ptr<OpenZLComponent> makeRangePackComponent();
std::unique_ptr<OpenZLComponent> makeMergeSortedComponent();
std::unique_ptr<OpenZLComponent> makePrefixComponent();
std::unique_ptr<OpenZLComponent> makeDivideByComponent();
std::unique_ptr<OpenZLComponent> makeConcatSerialComponent();
std::unique_ptr<OpenZLComponent> makeConcatNumericComponent();
std::unique_ptr<OpenZLComponent> makeConcatStructComponent();
std::unique_ptr<OpenZLComponent> makeConcatStringComponent();
std::unique_ptr<OpenZLComponent> makeDedupNumericComponent();
std::unique_ptr<OpenZLComponent> makeParseIntComponent();
std::unique_ptr<OpenZLComponent> makeInterleaveStringComponent();
std::unique_ptr<OpenZLComponent> makeTokenizeStructComponent();
std::unique_ptr<OpenZLComponent> makeTokenizeNumericComponent();
std::unique_ptr<OpenZLComponent> makeTokenizeStringComponent();
std::unique_ptr<OpenZLComponent> makeQuantizeOffsetsComponent();
std::unique_ptr<OpenZLComponent> makeQuantizeLengthsComponent();
std::unique_ptr<OpenZLComponent> makePartitionComponent();
std::unique_ptr<OpenZLComponent> makeStoreComponent();
std::unique_ptr<OpenZLComponent> makeStoreStringComponent();
std::unique_ptr<OpenZLComponent> makeFseComponent();
std::unique_ptr<OpenZLComponent> makeHuffmanComponent();
std::unique_ptr<OpenZLComponent> makeEntropyComponent();
std::unique_ptr<OpenZLComponent> makeBitpackComponent();
std::unique_ptr<OpenZLComponent> makeFlatpackComponent();
std::unique_ptr<OpenZLComponent> makeConstantComponent();
std::unique_ptr<OpenZLComponent> makeFieldLzComponent();
std::unique_ptr<OpenZLComponent> makeCompressGenericComponent();
std::unique_ptr<OpenZLComponent> makeBitSplitComponent();
std::unique_ptr<OpenZLComponent> makeBitSplitTop8Component();
std::unique_ptr<OpenZLComponent> makeTryParseIntComponent();
std::unique_ptr<OpenZLComponent> makeSplitByParamComponent();
std::unique_ptr<OpenZLComponent> makeSplitStructByParamComponent();
std::unique_ptr<OpenZLComponent> makeSplitNumericByParamComponent();
std::unique_ptr<OpenZLComponent> makeSplitByExtParserComponent();
std::unique_ptr<OpenZLComponent> makeBitSplitFPComponent();
std::unique_ptr<OpenZLComponent> makeSplitByRangeComponent();
std::unique_ptr<OpenZLComponent> makeBitSplitBF16Component();
std::unique_ptr<OpenZLComponent> makePartitionBitpackComponent();
std::unique_ptr<OpenZLComponent> makeSegmentNumericComponent();
std::unique_ptr<OpenZLComponent> makeSegmentNumFromSerialComponent();
std::unique_ptr<OpenZLComponent> makeSentinelByteComponent();
std::unique_ptr<OpenZLComponent> makeSentinelNumComponent();

} // namespace components

inline std::unique_ptr<OpenZLComponent> makeOpenZLComponent(
        OpenZLComponentID component)
{
    switch (component) {
        case OpenZLComponentID::Zstd:
            return components::makeZstdComponent();
        case OpenZLComponentID::Lz4:
            return components::makeLz4Component();
        case OpenZLComponentID::Delta:
            return components::makeDeltaComponent();
        case OpenZLComponentID::Zigzag:
            return components::makeZigzagComponent();
        case OpenZLComponentID::Transpose:
            return components::makeTransposeComponent();
        case OpenZLComponentID::Float32Deconstruct:
            return components::makeFloat32DeconstructComponent();
        case OpenZLComponentID::BFloat16Deconstruct:
            return components::makeBFloat16DeconstructComponent();
        case OpenZLComponentID::Float16Deconstruct:
            return components::makeFloat16DeconstructComponent();
        case OpenZLComponentID::ConvertStructToSerial:
            return components::makeConvertStructToSerialComponent();
        case OpenZLComponentID::ConvertSerialToStruct:
            return components::makeConvertSerialToStructComponent();
        case OpenZLComponentID::ConvertNumToSerialLE:
            return components::makeConvertNumToSerialLEComponent();
        case OpenZLComponentID::ConvertNumToStructLE:
            return components::makeConvertNumToStructLEComponent();
        case OpenZLComponentID::ConvertStructToNumLE:
            return components::makeConvertStructToNumLEComponent();
        case OpenZLComponentID::ConvertStructToNumBE:
            return components::makeConvertStructToNumBEComponent();
        case OpenZLComponentID::ConvertSerialToNumLE:
            return components::makeConvertSerialToNumLEComponent();
        case OpenZLComponentID::ConvertSerialToNumBE:
            return components::makeConvertSerialToNumBEComponent();
        case OpenZLComponentID::SeparateStringComponents:
            return components::makeSeparateStringComponentsComponent();
        case OpenZLComponentID::Bitunpack:
            return components::makeBitunpackComponent();
        case OpenZLComponentID::RangePack:
            return components::makeRangePackComponent();
        case OpenZLComponentID::MergeSorted:
            return components::makeMergeSortedComponent();
        case OpenZLComponentID::Prefix:
            return components::makePrefixComponent();
        case OpenZLComponentID::DivideBy:
            return components::makeDivideByComponent();
        case OpenZLComponentID::ConcatSerial:
            return components::makeConcatSerialComponent();
        case OpenZLComponentID::ConcatNumeric:
            return components::makeConcatNumericComponent();
        case OpenZLComponentID::ConcatStruct:
            return components::makeConcatStructComponent();
        case OpenZLComponentID::ConcatString:
            return components::makeConcatStringComponent();
        case OpenZLComponentID::DedupNumeric:
            return components::makeDedupNumericComponent();
        case OpenZLComponentID::ParseInt:
            return components::makeParseIntComponent();
        case OpenZLComponentID::InterleaveString:
            return components::makeInterleaveStringComponent();
        case OpenZLComponentID::TokenizeStruct:
            return components::makeTokenizeStructComponent();
        case OpenZLComponentID::TokenizeNumeric:
            return components::makeTokenizeNumericComponent();
        case OpenZLComponentID::TokenizeString:
            return components::makeTokenizeStringComponent();
        case OpenZLComponentID::QuantizeOffsets:
            return components::makeQuantizeOffsetsComponent();
        case OpenZLComponentID::QuantizeLengths:
            return components::makeQuantizeLengthsComponent();
        case OpenZLComponentID::Partition:
            return components::makePartitionComponent();
        case OpenZLComponentID::Store:
            return components::makeStoreComponent();
        case OpenZLComponentID::StoreString:
            return components::makeStoreStringComponent();
        case OpenZLComponentID::Fse:
            return components::makeFseComponent();
        case OpenZLComponentID::Huffman:
            return components::makeHuffmanComponent();
        case OpenZLComponentID::Entropy:
            return components::makeEntropyComponent();
        case OpenZLComponentID::Bitpack:
            return components::makeBitpackComponent();
        case OpenZLComponentID::Flatpack:
            return components::makeFlatpackComponent();
        case OpenZLComponentID::Constant:
            return components::makeConstantComponent();
        case OpenZLComponentID::FieldLz:
            return components::makeFieldLzComponent();
        case OpenZLComponentID::CompressGeneric:
            return components::makeCompressGenericComponent();
        case OpenZLComponentID::BitSplit:
            return components::makeBitSplitComponent();
        case OpenZLComponentID::BitSplitTop8:
            return components::makeBitSplitTop8Component();
        case OpenZLComponentID::TryParseInt:
            return components::makeTryParseIntComponent();
        case OpenZLComponentID::SplitByParam:
            return components::makeSplitByParamComponent();
        case OpenZLComponentID::SplitStructByParam:
            return components::makeSplitStructByParamComponent();
        case OpenZLComponentID::SplitNumericByParam:
            return components::makeSplitNumericByParamComponent();
        case OpenZLComponentID::SplitByExtParser:
            return components::makeSplitByExtParserComponent();
        case OpenZLComponentID::BitSplitFP:
            return components::makeBitSplitFPComponent();
        case OpenZLComponentID::SplitByRange:
            return components::makeSplitByRangeComponent();
        case OpenZLComponentID::BitSplitBF16:
            return components::makeBitSplitBF16Component();
        case OpenZLComponentID::PartitionBitpack:
            return components::makePartitionBitpackComponent();
        case OpenZLComponentID::SegmentNumeric:
            return components::makeSegmentNumericComponent();
        case OpenZLComponentID::SegmentNumFromSerial:
            return components::makeSegmentNumFromSerialComponent();
        case OpenZLComponentID::SentinelByte:
            return components::makeSentinelByteComponent();
        case OpenZLComponentID::SentinelNum:
            return components::makeSentinelNumComponent();
        case OpenZLComponentID::NumComponents:
        default:
            throw std::runtime_error("Invalid component");
    }
}

poly::span<const std::unique_ptr<OpenZLComponent>> getAllOpenZLComponents();

} // namespace openzl::tests
