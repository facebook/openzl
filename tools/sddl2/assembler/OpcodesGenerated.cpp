// Copyright (c) Meta Platforms, Inc. and affiliates.

// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
//
// Generated from: src/openzl/compress/graphs/sddl2/sddl2_opcodes.def
// Generated at: 2026-02-25 19:35:34 UTC
// Generator: generate_opcodes.py
//
// To regenerate: python3 generate_opcodes.py

#include "tools/sddl2/assembler/OpcodesGenerated.h"
#include <unordered_map>
namespace openzl {
namespace sddl2 {
namespace opcodes {
namespace {

const std::unordered_map<std::string, Instruction> INSTRUCTIONS = {
    // CONTROL family (0x0005)
    { "halt",
      Instruction{ Family::CONTROL, 0x0001, std::vector<ParamType>{} } },
    { "expect_true",
      Instruction{ Family::CONTROL, 0x0002, std::vector<ParamType>{} } },
    { "trace.start",
      Instruction{ Family::CONTROL, 0x0004, std::vector<ParamType>{} } },

    // PUSH family (0x0001)
    { "push.zero",
      Instruction{ Family::PUSH, 0x0001, std::vector<ParamType>{} } },
    { "push.u32",
      Instruction{ Family::PUSH,
                   0x0002,
                   std::vector<ParamType>{ ParamType::U32 } } },
    { "push.i32",
      Instruction{ Family::PUSH,
                   0x0003,
                   std::vector<ParamType>{ ParamType::I32 } } },
    { "push.i64",
      Instruction{ Family::PUSH,
                   0x0004,
                   std::vector<ParamType>{ ParamType::I64 } } },
    { "push.tag",
      Instruction{ Family::PUSH,
                   0x0005,
                   std::vector<ParamType>{ ParamType::U32 } } },
    { "push.current_pos",
      Instruction{ Family::PUSH, 0x0080, std::vector<ParamType>{} } },
    { "push.remaining",
      Instruction{ Family::PUSH, 0x0081, std::vector<ParamType>{} } },
    { "push.stack_depth",
      Instruction{ Family::PUSH, 0x0082, std::vector<ParamType>{} } },
    { "push.type.bytes",
      Instruction{ Family::PUSH, 0x0100, std::vector<ParamType>{} } },
    { "push.type.u8",
      Instruction{ Family::PUSH, 0x0110, std::vector<ParamType>{} } },
    { "push.type.i8",
      Instruction{ Family::PUSH, 0x0111, std::vector<ParamType>{} } },
    { "push.type.u16le",
      Instruction{ Family::PUSH, 0x0112, std::vector<ParamType>{} } },
    { "push.type.u16be",
      Instruction{ Family::PUSH, 0x0113, std::vector<ParamType>{} } },
    { "push.type.i16le",
      Instruction{ Family::PUSH, 0x0114, std::vector<ParamType>{} } },
    { "push.type.i16be",
      Instruction{ Family::PUSH, 0x0115, std::vector<ParamType>{} } },
    { "push.type.u32le",
      Instruction{ Family::PUSH, 0x0116, std::vector<ParamType>{} } },
    { "push.type.u32be",
      Instruction{ Family::PUSH, 0x0117, std::vector<ParamType>{} } },
    { "push.type.i32le",
      Instruction{ Family::PUSH, 0x0118, std::vector<ParamType>{} } },
    { "push.type.i32be",
      Instruction{ Family::PUSH, 0x0119, std::vector<ParamType>{} } },
    { "push.type.u64le",
      Instruction{ Family::PUSH, 0x011A, std::vector<ParamType>{} } },
    { "push.type.u64be",
      Instruction{ Family::PUSH, 0x011B, std::vector<ParamType>{} } },
    { "push.type.i64le",
      Instruction{ Family::PUSH, 0x011C, std::vector<ParamType>{} } },
    { "push.type.i64be",
      Instruction{ Family::PUSH, 0x011D, std::vector<ParamType>{} } },
    { "push.type.f8",
      Instruction{ Family::PUSH, 0x0130, std::vector<ParamType>{} } },
    { "push.type.f16le",
      Instruction{ Family::PUSH, 0x0131, std::vector<ParamType>{} } },
    { "push.type.f16be",
      Instruction{ Family::PUSH, 0x0132, std::vector<ParamType>{} } },
    { "push.type.bf16le",
      Instruction{ Family::PUSH, 0x0133, std::vector<ParamType>{} } },
    { "push.type.bf16be",
      Instruction{ Family::PUSH, 0x0134, std::vector<ParamType>{} } },
    { "push.type.f32le",
      Instruction{ Family::PUSH, 0x0135, std::vector<ParamType>{} } },
    { "push.type.f32be",
      Instruction{ Family::PUSH, 0x0136, std::vector<ParamType>{} } },
    { "push.type.f64le",
      Instruction{ Family::PUSH, 0x0137, std::vector<ParamType>{} } },
    { "push.type.f64be",
      Instruction{ Family::PUSH, 0x0138, std::vector<ParamType>{} } },

    // STACK family (0x0007)
    { "stack.dup",
      Instruction{ Family::STACK, 0x0001, std::vector<ParamType>{} } },
    { "stack.over",
      Instruction{ Family::STACK, 0x0002, std::vector<ParamType>{} } },
    { "stack.drop",
      Instruction{ Family::STACK, 0x0003, std::vector<ParamType>{} } },
    { "stack.swap",
      Instruction{ Family::STACK, 0x0004, std::vector<ParamType>{} } },
    { "stack.rot",
      Instruction{ Family::STACK, 0x0005, std::vector<ParamType>{} } },
    { "stack.drop_if",
      Instruction{ Family::STACK, 0x0010, std::vector<ParamType>{} } },

    // MATH family (0x0002)
    { "math.add",
      Instruction{ Family::MATH, 0x0001, std::vector<ParamType>{} } },
    { "math.sub",
      Instruction{ Family::MATH, 0x0002, std::vector<ParamType>{} } },
    { "math.mul",
      Instruction{ Family::MATH, 0x0003, std::vector<ParamType>{} } },
    { "math.div",
      Instruction{ Family::MATH, 0x0004, std::vector<ParamType>{} } },
    { "math.mod",
      Instruction{ Family::MATH, 0x0005, std::vector<ParamType>{} } },
    { "math.abs",
      Instruction{ Family::MATH, 0x0006, std::vector<ParamType>{} } },
    { "math.neg",
      Instruction{ Family::MATH, 0x0007, std::vector<ParamType>{} } },

    // CMP family (0x0003)
    { "cmp.eq", Instruction{ Family::CMP, 0x0001, std::vector<ParamType>{} } },
    { "cmp.ne", Instruction{ Family::CMP, 0x0002, std::vector<ParamType>{} } },
    { "cmp.lt", Instruction{ Family::CMP, 0x0003, std::vector<ParamType>{} } },
    { "cmp.le", Instruction{ Family::CMP, 0x0004, std::vector<ParamType>{} } },
    { "cmp.gt", Instruction{ Family::CMP, 0x0005, std::vector<ParamType>{} } },
    { "cmp.ge", Instruction{ Family::CMP, 0x0006, std::vector<ParamType>{} } },

    // LOGIC family (0x0004)
    { "logic.and",
      Instruction{ Family::LOGIC, 0x0001, std::vector<ParamType>{} } },
    { "logic.or",
      Instruction{ Family::LOGIC, 0x0002, std::vector<ParamType>{} } },
    { "logic.xor",
      Instruction{ Family::LOGIC, 0x0003, std::vector<ParamType>{} } },
    { "logic.not",
      Instruction{ Family::LOGIC, 0x0004, std::vector<ParamType>{} } },

    // LOAD family (0x0006)
    { "load.u8",
      Instruction{ Family::LOAD, 0x0001, std::vector<ParamType>{} } },
    { "load.i8",
      Instruction{ Family::LOAD, 0x0002, std::vector<ParamType>{} } },
    { "load.u16le",
      Instruction{ Family::LOAD, 0x0010, std::vector<ParamType>{} } },
    { "load.i16le",
      Instruction{ Family::LOAD, 0x0011, std::vector<ParamType>{} } },
    { "load.u32le",
      Instruction{ Family::LOAD, 0x0020, std::vector<ParamType>{} } },
    { "load.i32le",
      Instruction{ Family::LOAD, 0x0021, std::vector<ParamType>{} } },
    { "load.i64le",
      Instruction{ Family::LOAD, 0x0030, std::vector<ParamType>{} } },
    { "load.u16be",
      Instruction{ Family::LOAD, 0x0110, std::vector<ParamType>{} } },
    { "load.i16be",
      Instruction{ Family::LOAD, 0x0111, std::vector<ParamType>{} } },
    { "load.u32be",
      Instruction{ Family::LOAD, 0x0120, std::vector<ParamType>{} } },
    { "load.i32be",
      Instruction{ Family::LOAD, 0x0121, std::vector<ParamType>{} } },
    { "load.i64be",
      Instruction{ Family::LOAD, 0x0130, std::vector<ParamType>{} } },

    // TYPE family (0x0008)
    { "type.fixed_array",
      Instruction{ Family::TYPE, 0x0001, std::vector<ParamType>{} } },
    { "type.structure",
      Instruction{ Family::TYPE, 0x0002, std::vector<ParamType>{} } },
    { "type.sizeof",
      Instruction{ Family::TYPE, 0x0010, std::vector<ParamType>{} } },

    // VAR family (0x0009)
    { "var.store",
      Instruction{ Family::VAR, 0x0001, std::vector<ParamType>{} } },
    { "var.load",
      Instruction{ Family::VAR, 0x0002, std::vector<ParamType>{} } },
    { "var.clear",
      Instruction{ Family::VAR, 0x0003, std::vector<ParamType>{} } },

    // CALL family (0x000B)

    // SEGMENT family (0x000C)
    { "segment.create_unspecified",
      Instruction{ Family::SEGMENT, 0x0001, std::vector<ParamType>{} } },
    { "segment.create_tagged",
      Instruction{ Family::SEGMENT, 0x0002, std::vector<ParamType>{} } },

};
} // namespace

std::optional<Instruction> getInstruction(const std::string& mnemonic)
{
    auto it = INSTRUCTIONS.find(mnemonic);
    if (it == INSTRUCTIONS.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace opcodes
} // namespace sddl2
} // namespace openzl
