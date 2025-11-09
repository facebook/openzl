// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/Serializer.h"

#include <iomanip>

#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/detail/NonNullUniqueCPtr.hpp"

#include "openzl/common/a1cbor_helpers.h"

#include "tools/sddl/compiler/Exception.h"

using namespace openzl::detail;
using namespace openzl::sddl::detail;

namespace openzl::sddl {

namespace {

void log_json(std::ostream& log, const poly::string_view& ser)
{
    const auto json = Compressor::convertSerializedToJson(ser);
    auto sv         = poly::string_view{ json };

    log << "Serialized JSON:" << std::endl;

    while (!sv.empty()) {
        auto pos = sv.find('\n');
        if (pos == poly::string_view::npos) {
            log << "  " << sv;
            sv = poly::string_view{};
        } else {
            log << "  " << sv.substr(0, pos + 1);
            sv.remove_prefix(pos + 1);
        }
    }
    log << std::endl << std::endl;
}

void log_serialized(std::ostream& log, const poly::string_view& ser)
{
    const auto format_flags = log.flags();
    log << "Serialized:" << std::endl;
    log << "  std::string_view{" << std::endl;
    log << "    \"";
    for (size_t i = 0; i < ser.size(); i++) {
        if (i && (i % 16 == 0)) {
            log << "\"" << std::endl;
            log << "    \"";
        }
        log << "\\x" << std::setw(2) << std::setfill('0') << std::hex
            << (uint32_t)(uint8_t)ser[i];
    }
    log << "\"," << std::endl;
    log.flags(format_flags);
    log << "    " << ser.size() << std::endl;
    log << "  };" << std::endl << std::endl;
}

} // namespace

Serializer::Serializer(const Logger& logger, bool include_debug_info)
        : log_(logger), include_debug_info_(include_debug_info)
{
}

std::string Serializer::serialize(const ASTVec& ast, const Source& source) const
{
    const auto arena = NonNullUniqueCPtr<Arena>(
            ALLOC_HeapArena_create(), ALLOC_Arena_freeArena);
    auto a1c_arena = A1C_Arena_wrap(arena.get());

    SerializationOptions ser_opts = {
        .arena                    = &a1c_arena,
        .include_source_locations = include_debug_info_,
    };

    A1C_Item root;

    const auto root_map_builder = A1C_Item_map_builder(&root, 2, &a1c_arena);

    {
        auto* const items_pair = A1C_MapBuilder_add(root_map_builder);
        if (items_pair == nullptr) {
            throw SerializationError("Failed to add element to root map.");
        }
        A1C_Item_string_refCStr(&items_pair->key, "exprs");
        auto* const items =
                A1C_Item_array(&items_pair->val, ast.size(), &a1c_arena);
        if (items == nullptr) {
            throw SerializationError(
                    "Failed to serialize compiled tree due to failing to allocate root A1C_Item array.");
        }

        std::vector<const A1C_Item*> exprs;
        for (size_t i = 0; i < ast.size(); i++) {
            items[i] = ast[i]->serialize(ser_opts);
        }
    }

    if (include_debug_info_) {
        auto* const src_pair = A1C_MapBuilder_add(root_map_builder);
        if (src_pair == nullptr) {
            throw SerializationError("Failed to add element to root map.");
        }
        A1C_Item_string_refCStr(&src_pair->key, "src");
        A1C_Item_string_ref(
                &src_pair->val,
                source.contents().data(),
                source.contents().size());
    }

    const auto size = A1C_Item_encodedSize(&root);

    std::string ser(size, '\0');

    A1C_Error error;
    if (A1C_Item_encode(
                &root,
                reinterpret_cast<uint8_t*>(ser.data()),
                ser.size(),
                &error)
        != size) {
        throw SerializationError(
                std::string{
                        "Failed to serialize compiled tree with A1C error: " }
                + A1C_ErrorType_getString(error.type));
    }

    log_json(log_(2), ser);
    log_serialized(log_(1), ser);

    return ser;
}
} // namespace openzl::sddl
