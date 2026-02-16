// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/assembler/Assembler.h"
#include "tools/sddl2/assembler/OpcodesGenerated.h"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace openzl {
namespace sddl2 {

using namespace opcodes;

namespace {

void validateRange(int64_t val, ParamType type)
{
    switch (type) {
        case ParamType::U32:
            if (val < std::numeric_limits<uint32_t>::min()
                || val > std::numeric_limits<uint32_t>::max()) {
                throw AssemblerError("Param value out of range for U32!");
            }
            break;
        case ParamType::I32:
            if (val < std::numeric_limits<int32_t>::min()
                || val > std::numeric_limits<int32_t>::max()) {
                throw AssemblerError("Param value out of range for I32!");
            }
            break;
        case ParamType::I64:
            // All int64_t values are valid for I64.
            break;
        default:
            throw AssemblerError("Unknown param type!");
    }
}

template <typename T>
void writeNum(T val, std::vector<uint8_t>& out)
{
    for (size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<uint8_t>(val >> (i * 8)));
    }
}

void encodeParameter(int64_t value, ParamType type, std::vector<uint8_t>& out)
{
    validateRange(value, type);

    switch (type) {
        case ParamType::U32: {
            writeNum(static_cast<uint32_t>(value), out);
            break;
        }
        case ParamType::I32: {
            writeNum(static_cast<int32_t>(value), out);
            break;
        }
        case ParamType::I64: {
            writeNum(value, out);
            break;
        }
        default: {
            throw AssemblerError("Unknown param type!");
        }
    }
}

bool isInstruction(const std::string& mnemonic)
{
    return getInstruction(mnemonic).has_value();
}

// ── Tokenization ─────────────────────────────────────────────────────────────

struct ParsedInstruction {
    std::string mnemonic;
    std::vector<int64_t> params;
};

void consume_ws(poly::string_view& input)
{
    poly::string_view nl{};
    while (!input.empty()) {
        // Consume whitespace
        if (std::isspace(input[0])) {
            if (input[0] == '\n' && nl.empty()) {
                nl = input.substr(0, 1);
            }
            input.remove_prefix(1);
        }
        // Consume comments
        else if (input[0] == '#' || input[0] == ';') {
            while (!input.empty() && input[0] != '\n') {
                input.remove_prefix(1);
            }
        } else {
            break;
        }
    }
}

std::string read_mnemonic(poly::string_view& input)
{
    auto mnemonic = input;
    while (!input.empty()
           && (std::isalnum(input[0]) || input[0] == '.' || input[0] == '_')) {
        input.remove_prefix(1);
    }
    mnemonic = mnemonic.substr(0, mnemonic.size() - input.size());

    auto mnemonic_str = std::string(mnemonic);

    if (!isInstruction(mnemonic_str)) {
        throw AssemblerError("Unknown instruction: " + mnemonic_str);
    }

    return mnemonic_str;
}

int64_t read_num(poly::string_view& input)
{
    poly::string_view num = input;

    while (!input.empty() && (std::isalnum(input[0]) || input[0] == '-')) {
        input.remove_prefix(1);
    }
    num = num.substr(0, num.size() - input.size());

    size_t read = 0;
    int64_t val;
    try {
        val = std::stoll(std::string{ num }, &read, 0);
    } catch (const std::out_of_range&) {
        throw AssemblerError("Couldn't parse integer literal: out of range.");
    } catch (const std::exception&) {
        throw AssemblerError(
                "Couldn't parse integer literal: " + std::string{ num });
    }
    if (read != num.size()) {
        throw AssemblerError("Couldn't parse integer literal.");
    }
    return val;
}

std::vector<ParsedInstruction> tokenize(std::string_view source)
{
    // Group tokens into (instruction, params)
    std::vector<ParsedInstruction> instructions;
    while (true) {
        // Consume whitespace
        consume_ws(source);
        if (source.empty()) {
            break;
        }

        // Read instruction
        auto mnemonic   = read_mnemonic(source);
        auto num_params = getInstruction(mnemonic)->params.size();

        // Read the parameters (with whitespace between them)
        std::vector<int64_t> params(num_params);
        for (size_t i = 0; i < num_params; ++i) {
            consume_ws(source);
            params[i] = read_num(source);
        }
        instructions.push_back({ std::move(mnemonic), params });
    }

    return instructions;
}

// ── Single instruction assembly ─────────────────────────────────────────────

void assembleInstruction(
        const ParsedInstruction& parsed,
        std::vector<uint8_t>& out)
{
    const auto& instr = getInstruction(parsed.mnemonic);

    if (!instr.has_value()) {
        throw AssemblerError("Unknown instruction: " + parsed.mnemonic);
    }

    // Encode instruction word: (family_id << 16) | opcode
    uint16_t family = static_cast<uint16_t>(instr->family);
    uint16_t opcode = instr->opcode;
    uint32_t word   = (family << 16) | opcode;
    writeNum(word, out);

    // Encode immediate parameters
    if (instr->params.size() != parsed.params.size()) {
        throw AssemblerError("Wrong number of parameters for instruction!");
    }
    for (size_t j = 0; j < instr->params.size(); ++j) {
        encodeParameter(parsed.params.at(j), instr->params.at(j), out);
    }
}

} // namespace

std::vector<uint8_t> Assembler::assemble(poly::string_view source) const
{
    // Parse the source into instructions
    auto instructions = tokenize(source);

    // Generate the bytecode
    std::vector<uint8_t> bytecode;
    for (const auto& instr : instructions) {
        assembleInstruction(instr, bytecode);
    }

    return bytecode;
}

} // namespace sddl2
} // namespace openzl
