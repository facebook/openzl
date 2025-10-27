// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/shared/a1cbor.h"
#include "openzl/shared/string_view.h"
#include "openzl/zl_errors.h"

#include <math.h>
#include <string.h>
#include <memory>
#include "tools/json.hpp"

#include <gtest/gtest.h>

bool operator==(const A1C_Item& a, const A1C_Item& b);
bool operator==(const A1C_Item& a, const A1C_Item& b)
{
    return A1C_Item_eq(&a, &b);
}

bool operator!=(const A1C_Item& a, const A1C_Item& b);
bool operator!=(const A1C_Item& a, const A1C_Item& b)
{
    return !(a == b);
}

namespace {
using namespace ::testing;
using json = nlohmann::json;

using Ptrs = std::vector<std::unique_ptr<uint8_t[]>>;

void* testCalloc(void* opaque, size_t bytes) noexcept
{
    auto ptrs = static_cast<Ptrs*>(opaque);
    if (bytes == 0) {
        return nullptr;
    }
    auto ptr = std::make_unique<uint8_t[]>(bytes);
    if (ptr == nullptr) {
        return nullptr;
    }
    memset(ptr.get(), 0, bytes);
    ptrs->push_back(std::move(ptr));
    return ptrs->back().get();
}

size_t appendToString(void* opaque, const uint8_t* data, size_t size) noexcept
{
    auto str = static_cast<std::string*>(opaque);
    if (size == 0) {
        return 0;
    }
    str->append(reinterpret_cast<const char*>(data), size);
    return size;
}
} // namespace

class A1CBorTest : public Test {
   protected:
    void SetUp() override
    {
        arena.calloc = testCalloc;
        arena.opaque = &ptrs;
    }

    std::string printError(std::string msg, A1C_Error error)
    {
        msg += ": ";
        msg += "type=";
        msg += A1C_ErrorType_getString(error.type);
        msg += ", srcPos=" + std::to_string(error.srcPos);
        msg += ", depth=" + std::to_string(error.depth);
        msg += ", file=";
        msg += error.file;
        msg += ", line=" + std::to_string(error.line);
        return msg;
    }

    std::string encode(const A1C_Item* item)
    {
        std::string str;
        A1C_Encoder encoder;
        A1C_Encoder_init(&encoder, appendToString, &str);
        if (!A1C_Encoder_encode(&encoder, item)) {
            throw std::runtime_error{ printError(
                    "Encoding failed", encoder.error) };
        }
        EXPECT_EQ(str.size(), A1C_Item_encodedSize(item));
        std::string string2;
        string2.resize(str.size());
        EXPECT_EQ(
                A1C_Item_encode(
                        item,
                        reinterpret_cast<uint8_t*>(&string2[0]),
                        string2.size(),
                        nullptr),
                string2.size());
        EXPECT_EQ(str, string2);
        return str;
    }

    std::string encodeJson(const A1C_Item* item)
    {
        std::string str;
        A1C_Encoder encoder;
        A1C_Encoder_init(&encoder, appendToString, &str);
        if (!A1C_Encoder_json(&encoder, item)) {
            throw std::runtime_error{ printError(
                    "JSON Encoding failed", encoder.error) };
        }
        EXPECT_EQ(str.size(), A1C_Item_jsonSize(item));
        std::string string2;
        string2.resize(str.size());
        EXPECT_EQ(
                A1C_Item_json(
                        item,
                        reinterpret_cast<uint8_t*>(&string2[0]),
                        string2.size(),
                        nullptr),
                string2.size());
        EXPECT_EQ(str, string2);
        return str;
    }

    const A1C_Item* decode(
            const std::string& data,
            size_t limit         = 0,
            bool referenceSource = false)
    {
        A1C_Decoder decoder;
        A1C_Decoder_init(
                &decoder,
                arena,
                { .limitBytes = limit, .referenceSource = referenceSource });
        const A1C_Item* item = A1C_Decoder_decode(
                &decoder,
                reinterpret_cast<const uint8_t*>(data.data()),
                data.size());
        if (item == nullptr) {
            throw std::runtime_error{ printError(
                    "Decoding failed", decoder.error) };
        }
        return item;
    }

    const A1C_Item* decode(
            const std::vector<uint8_t>& data,
            size_t limit         = 0,
            bool referenceSource = false)
    {
        A1C_Decoder decoder;
        A1C_Decoder_init(
                &decoder,
                arena,
                { .limitBytes = limit, .referenceSource = referenceSource });
        const A1C_Item* item =
                A1C_Decoder_decode(&decoder, data.data(), data.size());
        if (item == nullptr) {
            throw std::runtime_error{ printError(
                    "Decoding failed", decoder.error) };
        }
        return item;
    }

    A1C_Item* deepcopy(const A1C_Item* item)
    {
        A1C_Item* copy = A1C_Item_deepcopy(item, &arena);
        if (copy == nullptr) {
            throw std::runtime_error{ "Deepcopy failed" };
        }
        return copy;
    }

    void TearDown() override
    {
        ptrs.clear();
    }

    A1C_Arena arena;
    Ptrs ptrs;
};

TEST_F(A1CBorTest, Int64)
{
    auto testValue = [this](int64_t value) {
        auto item = A1C_Item_root(&arena);
        ASSERT_NE(item, nullptr);
        A1C_Item_int64(item, value);
        ASSERT_EQ(item->type, A1C_ItemType_int64);
        ASSERT_EQ(item->int64, value);
        ASSERT_EQ(item->parent, nullptr);
        auto encoded = encode(item);
        auto decoded = decode(encoded);
        ASSERT_EQ(decoded->parent, nullptr);
        ASSERT_EQ(decoded->type, A1C_ItemType_int64);
        ASSERT_EQ(item->int64, value);
        ASSERT_EQ(*item, *decoded);
        ASSERT_TRUE(A1C_Item_eq(item, decoded));
        ASSERT_EQ(*item, *deepcopy(item));
    };

    testValue(0);
    testValue(42);
    testValue(UINT8_MAX);
    testValue(UINT16_MAX);
    testValue(UINT32_MAX);
    testValue(INT64_MAX);

    testValue(-1);
    testValue(-UINT8_MAX);
    testValue(-UINT8_MAX - 1);
    testValue(-UINT16_MAX);
    testValue(-UINT16_MAX - 1);
    testValue(-UINT32_MAX);
    testValue(-UINT32_MAX - 1);
    testValue(INT64_MIN);

    {
        auto item1 = A1C_Item_root(&arena);
        ASSERT_NE(item1, nullptr);
        A1C_Item_int64(item1, -1);
        auto item2 = A1C_Item_root(&arena);
        ASSERT_NE(item2, nullptr);
        A1C_Item_int64(item2, -2);
        ASSERT_NE(*item1, *item2);
    }
}

TEST_F(A1CBorTest, Float32)
{
    auto testValue = [this](float value) {
        auto item = A1C_Item_root(&arena);
        ASSERT_NE(item, nullptr);
        A1C_Item_float32(item, value);
        ASSERT_EQ(item->type, A1C_ItemType_float32);
        if (!isnan(value)) {
            ASSERT_EQ(memcmp(&item->float32, &value, sizeof(value)), 0);
        } else {
            ASSERT_TRUE(isnan(item->float32));
        }
        ASSERT_EQ(item->parent, nullptr);

        auto encoded = encode(item);
        auto decoded = decode(encoded);
        ASSERT_EQ(*item, *decoded);
        ASSERT_EQ(decoded->parent, nullptr);
        ASSERT_EQ(*item, *deepcopy(item));
    };

    testValue(0.0);
    testValue(1e10);
    testValue(-1e10);
    testValue(std::numeric_limits<float>::quiet_NaN());
    testValue(std::numeric_limits<float>::signaling_NaN());
    testValue(std::numeric_limits<float>::infinity());

    {
        auto item1 = A1C_Item_root(&arena);
        ASSERT_NE(item1, nullptr);
        A1C_Item_float32(item1, 1.0);
        auto item2 = A1C_Item_root(&arena);
        ASSERT_NE(item2, nullptr);
        A1C_Item_float32(item2, 2.0);
        ASSERT_NE(*item1, *item2);
    }
}

TEST_F(A1CBorTest, Float64)
{
    auto testValue = [this](double value) {
        auto item = A1C_Item_root(&arena);
        ASSERT_NE(item, nullptr);
        A1C_Item_float64(item, value);
        ASSERT_EQ(item->type, A1C_ItemType_float64);
        if (!isnan(value)) {
            ASSERT_EQ(memcmp(&item->float64, &value, sizeof(value)), 0);
        } else {
            ASSERT_TRUE(isnan(item->float64));
        }
        ASSERT_EQ(item->parent, nullptr);

        auto encoded = encode(item);
        auto decoded = decode(encoded);
        ASSERT_EQ(*item, *decoded);
        ASSERT_EQ(decoded->parent, nullptr);
        ASSERT_EQ(*item, *deepcopy(item));
    };

    testValue(0.0);
    testValue(1e10);
    testValue(-1e10);
    testValue(std::numeric_limits<float>::quiet_NaN());
    testValue(std::numeric_limits<float>::signaling_NaN());
    testValue(std::numeric_limits<float>::infinity());

    {
        auto item1 = A1C_Item_root(&arena);
        ASSERT_NE(item1, nullptr);
        A1C_Item_float64(item1, 1.0);
        auto item2 = A1C_Item_root(&arena);
        ASSERT_NE(item2, nullptr);
        A1C_Item_float64(item2, 2.0);
        ASSERT_NE(*item1, *item2);
    }
}

TEST_F(A1CBorTest, Boolean)
{
    auto testValue = [this](bool value) {
        auto item = A1C_Item_root(&arena);
        ASSERT_NE(item, nullptr);
        A1C_Item_boolean(item, value);
        ASSERT_EQ(item->type, A1C_ItemType_boolean);
        ASSERT_EQ(item->boolean, value);
        ASSERT_EQ(item->parent, nullptr);

        auto encoded = encode(item);
        auto decoded = decode(encoded);
        ASSERT_EQ(*item, *decoded);
        ASSERT_EQ(decoded->parent, nullptr);
        ASSERT_EQ(*item, *deepcopy(item));
    };

    testValue(false);
    testValue(true);
    testValue(100);

    {
        auto item1 = A1C_Item_root(&arena);
        ASSERT_NE(item1, nullptr);
        A1C_Item_boolean(item1, true);
        auto item2 = A1C_Item_root(&arena);
        ASSERT_NE(item2, nullptr);
        A1C_Item_boolean(item2, false);
        ASSERT_NE(*item1, *item2);
    }
}

TEST_F(A1CBorTest, Undefined)
{
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);
    A1C_Item_undefined(item);
    ASSERT_EQ(item->type, A1C_ItemType_undefined);
    ASSERT_EQ(item->parent, nullptr);

    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);
    ASSERT_EQ(*item, *deepcopy(item));
}

TEST_F(A1CBorTest, Null)
{
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);
    A1C_Item_null(item);
    ASSERT_EQ(item->type, A1C_ItemType_null);
    ASSERT_EQ(item->parent, nullptr);

    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);
    ASSERT_EQ(*item, *deepcopy(item));
}

TEST_F(A1CBorTest, Tag)
{
    auto testValue = [this](uint64_t value) {
        auto item = A1C_Item_root(&arena);
        ASSERT_NE(item, nullptr);

        auto child = A1C_Item_tag(item, value, &arena);
        ASSERT_NE(child, nullptr);
        ASSERT_EQ(item->type, A1C_ItemType_tag);
        ASSERT_EQ(item->tag.tag, value);
        ASSERT_EQ(item->tag.item, child);
        ASSERT_EQ(child->parent, item);
        ASSERT_EQ(item->parent, nullptr);

        A1C_Item_null(child);

        auto encoded = encode(item);
        auto decoded = decode(encoded);
        ASSERT_EQ(decoded->type, A1C_ItemType_tag);
        ASSERT_EQ(decoded->tag.item->type, A1C_ItemType_null);
        ASSERT_EQ(*item, *decoded);
        ASSERT_EQ(decoded->parent, nullptr);
        ASSERT_EQ(decoded->tag.item->parent, decoded);
        ASSERT_EQ(*item, *deepcopy(item));

        {
            auto item1 = A1C_Item_root(&arena);
            ASSERT_NE(item1, nullptr);
            auto child1 = A1C_Item_tag(item1, 1, &arena);
            ASSERT_NE(child1, nullptr);
            A1C_Item_null(child1);
            auto item2 = A1C_Item_root(&arena);
            ASSERT_NE(item2, nullptr);
            auto child2 = A1C_Item_tag(item2, 2, &arena);
            ASSERT_NE(child2, nullptr);
            A1C_Item_null(child2);
            ASSERT_NE(*item1, *item2);
        }
        {
            auto item1 = A1C_Item_root(&arena);
            ASSERT_NE(item1, nullptr);
            auto child1 = A1C_Item_tag(item1, 1, &arena);
            ASSERT_NE(child1, nullptr);
            A1C_Item_null(child1);
            auto item2 = A1C_Item_root(&arena);
            ASSERT_NE(item2, nullptr);
            auto child2 = A1C_Item_tag(item2, 1, &arena);
            ASSERT_NE(child2, nullptr);
            A1C_Item_undefined(child2);
            ASSERT_NE(*item1, *item2);
        }
    };

    testValue(0);
    testValue(100);
    testValue(55799);
    testValue(UINT8_MAX);
    testValue(UINT16_MAX);
    testValue(UINT32_MAX);
    testValue(UINT64_MAX);
}

TEST_F(A1CBorTest, Bytes)
{
    auto testValue = [this](const std::string& value) {
        auto item = A1C_Item_root(&arena);
        ASSERT_NE(item, nullptr);

        const uint8_t* data = reinterpret_cast<const uint8_t*>(value.data());
        size_t size         = value.size();
        A1C_Item_bytes_ref(item, data, size);
        ASSERT_EQ(item->type, A1C_ItemType_bytes);
        ASSERT_EQ(memcmp(item->bytes.data, data, size), 0);
        ASSERT_EQ(item->bytes.size, size);
        ASSERT_EQ(item->parent, nullptr);
        ASSERT_EQ(item->bytes.data, data);
        ASSERT_EQ(*item, *deepcopy(item));

        auto encoded = encode(item);
        auto decoded = decode(encoded);
        ASSERT_EQ(*item, *decoded);
        ASSERT_EQ(decoded->parent, nullptr);

        ASSERT_TRUE(A1C_Item_bytes_copy(item, data, size, &arena));
        ASSERT_EQ(item->type, A1C_ItemType_bytes);
        ASSERT_EQ(memcmp(item->bytes.data, data, size), 0);
        ASSERT_EQ(item->bytes.size, size);
        ASSERT_EQ(item->parent, nullptr);
        ASSERT_NE(item->bytes.data, data);
        ASSERT_EQ(*item, *deepcopy(item));
    };

    testValue("");
    testValue("hello");
    testValue("world");
    testValue("this is a longer string that doesn't fit in one character");
    testValue(std::string(1000, 'a'));
    testValue(std::string(100000, 'a'));

    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);

    A1C_Item_bytes_ref(item, nullptr, 0);
    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);

    {
        auto item1 = A1C_Item_root(&arena);
        ASSERT_NE(item1, nullptr);
        A1C_Item_bytes_ref(item1, nullptr, 0);
        auto item2 = A1C_Item_root(&arena);
        ASSERT_NE(item2, nullptr);
        uint8_t data = 5;
        A1C_Item_bytes_ref(item2, &data, 1);
        ASSERT_NE(*item1, *item2);
    }
    {
        auto item1 = A1C_Item_root(&arena);
        ASSERT_NE(item1, nullptr);
        uint8_t data1 = 4;
        A1C_Item_bytes_ref(item1, &data1, 1);
        auto item2 = A1C_Item_root(&arena);
        ASSERT_NE(item2, nullptr);
        uint8_t data2 = 5;
        A1C_Item_bytes_ref(item2, &data2, 1);
        ASSERT_NE(*item1, *item2);
    }
}

TEST_F(A1CBorTest, String)
{
    auto testValue = [this](const std::string& value) {
        auto item = A1C_Item_root(&arena);
        ASSERT_NE(item, nullptr);

        const char* data = value.data();
        size_t size      = value.size();
        A1C_Item_string_ref(item, data, size);
        ASSERT_EQ(item->type, A1C_ItemType_string);
        ASSERT_EQ(memcmp(item->string.data, data, size), 0);
        ASSERT_EQ(item->string.size, size);
        ASSERT_EQ(item->parent, nullptr);
        ASSERT_EQ(item->string.data, data);
        ASSERT_EQ(*item, *deepcopy(item));

        auto encoded = encode(item);
        auto decoded = decode(encoded);
        ASSERT_EQ(*item, *decoded);
        ASSERT_EQ(decoded->parent, nullptr);

        ASSERT_TRUE(A1C_Item_string_copy(item, data, size, &arena));
        ASSERT_EQ(item->type, A1C_ItemType_string);
        ASSERT_EQ(memcmp(item->string.data, data, size), 0);
        ASSERT_EQ(item->string.size, size);
        ASSERT_EQ(item->parent, nullptr);
        ASSERT_NE(item->string.data, data);
        ASSERT_EQ(*item, *deepcopy(item));
    };

    testValue("");
    testValue("hello");
    testValue("world");
    testValue("this is a longer string that doesn't fit in one character");
    testValue(std::string(1000, 'a'));
    testValue(std::string(100000, 'a'));

    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);

    A1C_Item_string_ref(item, nullptr, 0);
    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);

    {
        auto item1 = A1C_Item_root(&arena);
        ASSERT_NE(item1, nullptr);
        A1C_Item_string_refCStr(item1, "x");
        auto item2 = A1C_Item_root(&arena);
        ASSERT_NE(item2, nullptr);
        A1C_Item_string_refCStr(item2, "y");
        ASSERT_NE(*item1, *item2);
    }
    {
        auto item1 = A1C_Item_root(&arena);
        ASSERT_NE(item1, nullptr);
        A1C_Item_string_refCStr(item1, "short");
        auto item2 = A1C_Item_root(&arena);
        ASSERT_NE(item2, nullptr);
        A1C_Item_string_refCStr(item2, "longer string");
        ASSERT_NE(*item1, *item2);
    }
}

TEST_F(A1CBorTest, Map)
{
    auto testMap = [this](const A1C_Item* map) {
        ASSERT_EQ(map->parent, nullptr);
        ASSERT_EQ(map->type, A1C_ItemType_map);
        const A1C_Map& m = map->map;

        for (size_t i = 0; i < m.size; ++i) {
            auto item = A1C_Map_get(&m, &m.items[i].key);
            ASSERT_NE(item, nullptr);
            ASSERT_EQ(item, &m.items[i].val);

            ASSERT_EQ(m.items[i].key.parent, map);
            ASSERT_EQ(m.items[i].val.parent, map);
        }

        auto encoded = encode(map);
        auto decoded = decode(encoded);
        ASSERT_EQ(*map, *decoded);
        ASSERT_EQ(decoded->parent, nullptr);

        for (size_t i = 0; i < m.size; ++i) {
            ASSERT_EQ(decoded->map.items[i].key.parent, decoded);
            ASSERT_EQ(decoded->map.items[i].val.parent, decoded);
        }
        ASSERT_EQ(*map, *deepcopy(map));
    };

    {
        auto map = A1C_Item_root(&arena);
        auto m   = A1C_Item_map(map, 0, &arena);
        ASSERT_NE(m, nullptr);
        testMap(map);
    }

    A1C_Item* map1;
    {
        auto map = A1C_Item_root(&arena);
        auto m   = A1C_Item_map(map, 1, &arena);
        ASSERT_NE(map, nullptr);
        A1C_Item_int64(&m[0].key, 42);
        A1C_Item_int64(&m[0].val, 42);
        testMap(map);
        EXPECT_NE(A1C_Map_get_int(&map->map, 42), nullptr);
        EXPECT_EQ(A1C_Map_get_cstr(&map->map, "key1"), nullptr);
        EXPECT_EQ(A1C_Map_get_int(&map->map, 0), nullptr);
        EXPECT_EQ(A1C_Map_get_int(&map->map, -5), nullptr);
        map1 = map;
    }
    A1C_Item* map2;
    {
        auto map = A1C_Item_root(&arena);
        auto m   = A1C_Item_map(map, 2, &arena);
        ASSERT_NE(map, nullptr);
        A1C_Item_string_refCStr(&m[0].key, "key1");
        A1C_Item_string_refCStr(&m[0].val, "value1");
        A1C_Item_string_refCStr(&m[1].key, "key2");
        A1C_Item_string_refCStr(&m[1].val, "value2");
        testMap(map);
        auto item = A1C_Map_get_cstr(&map->map, "key1");
        ASSERT_NE(item, nullptr);
        A1C_Map_get_cstr(&map->map, "key1");
        EXPECT_EQ(A1C_Map_get_int(&map->map, 42), nullptr);
        map2 = map;
    }
    ASSERT_NE(*map1, *map2);
    {
        auto map = A1C_Item_root(&arena);
        auto m   = A1C_Item_map(map, 4, &arena);
        ASSERT_NE(map, nullptr);
        A1C_Item_null(&m[0].key);
        A1C_Item_null(&m[0].val);
        A1C_Item_boolean(&m[1].key, true);
        A1C_Item_boolean(&m[1].val, true);
        A1C_Item_boolean(&m[2].key, false);
        A1C_Item_boolean(&m[2].val, false);
        A1C_Item_undefined(&m[3].key);
        A1C_Item_undefined(&m[3].val);
        testMap(map);
    }
    {
        // Builder API
        auto map = A1C_Item_root(&arena);
        auto b   = A1C_Item_map_builder(map, 2, &arena);
        ASSERT_EQ(map->type, A1C_ItemType_map);
        EXPECT_EQ(map->map.size, 0);
        {
            auto p = A1C_MapBuilder_add(b);
            EXPECT_NE(p, nullptr);
            EXPECT_EQ(p, &map->map.items[0]);
            EXPECT_EQ(map->map.size, 1);
            A1C_Item_string_refCStr(&p->key, "key1");
            A1C_Item_string_refCStr(&p->val, "val1");
            EXPECT_EQ(A1C_Map_get_cstr(&map->map, "key1"), &p->val);
        }
        {
            auto p = A1C_MapBuilder_add(b);
            EXPECT_NE(p, nullptr);
            EXPECT_EQ(p, &map->map.items[1]);
            EXPECT_EQ(map->map.size, 2);
            A1C_Item_string_refCStr(&p->key, "key2");
            A1C_Item_string_refCStr(&p->val, "val2");
            EXPECT_EQ(A1C_Map_get_cstr(&map->map, "key2"), &p->val);
        }
        {
            auto p = A1C_MapBuilder_add(b);
            EXPECT_EQ(p, nullptr);
            EXPECT_EQ(map->map.size, 2);
        }
        {
            auto p = A1C_MapBuilder_add(b);
            EXPECT_EQ(p, nullptr);
            EXPECT_EQ(map->map.size, 2);
        }
        testMap(map);
    }
}

TEST_F(A1CBorTest, Array)
{
    auto testArray = [this](const A1C_Item* array) {
        ASSERT_EQ(array->parent, nullptr);
        ASSERT_EQ(array->type, A1C_ItemType_array);
        const A1C_Array& a = array->array;

        for (size_t i = 0; i < a.size; ++i) {
            auto item = A1C_Array_get(&a, i);
            ASSERT_NE(item, nullptr);
            ASSERT_EQ(item, &a.items[i]);

            ASSERT_EQ(a.items[i].parent, array);
        }
        ASSERT_EQ(A1C_Array_get(&a, a.size), nullptr);

        auto encoded = encode(array);
        auto decoded = decode(encoded);
        ASSERT_EQ(*array, *decoded);
        ASSERT_EQ(decoded->parent, nullptr);

        for (size_t i = 0; i < a.size; ++i) {
            ASSERT_EQ(decoded->array.items[i].parent, decoded);
        }
        ASSERT_EQ(*array, *deepcopy(array));
    };

    {
        auto array = A1C_Item_root(&arena);
        ASSERT_NE(array, nullptr);
        auto a = A1C_Item_array(array, 0, &arena);
        ASSERT_NE(a, nullptr);
        testArray(array);
    }
    A1C_Item* array1;
    {
        auto array = A1C_Item_root(&arena);
        ASSERT_NE(array, nullptr);
        auto a = A1C_Item_array(array, 1, &arena);
        ASSERT_NE(a, nullptr);

        // Fill the array with a single int64 value
        A1C_Item_int64(a + 0, 42);
        testArray(array);

        EXPECT_EQ(A1C_Array_get(&array->array, 1), nullptr); // out of bounds
        array1 = array;
    }
    A1C_Item* array2;
    {
        auto array = A1C_Item_root(&arena);
        ASSERT_NE(array, nullptr);
        auto a = A1C_Item_array(array, 5, &arena);
        ASSERT_NE(a, nullptr);

        A1C_Item_null(a + 0);
        A1C_Item_boolean(a + 1, true);
        A1C_Item_undefined(a + 2);
        A1C_Item_int64(a + 3, 100);
        auto m = A1C_Item_map(a + 4, 1, &arena);
        ASSERT_NE(m, nullptr);
        A1C_Item_null(&m[0].key);
        A1C_Item_null(&m[0].val);

        testArray(array);
        array2 = array;
    }
    ASSERT_NE(*array1, *array2);
    {
        // Builder API
        auto array = A1C_Item_root(&arena);
        auto b     = A1C_Item_array_builder(array, 2, &arena);
        ASSERT_EQ(array->type, A1C_ItemType_array);
        EXPECT_EQ(array->array.size, 0);
        {
            auto p = A1C_ArrayBuilder_add(b);
            EXPECT_NE(p, nullptr);
            EXPECT_EQ(p, &array->array.items[0]);
            EXPECT_EQ(array->array.size, 1);
            EXPECT_EQ(A1C_Array_get(&array->array, 0), p);
            A1C_Item_int64(p, 1);
        }
        {
            auto p = A1C_ArrayBuilder_add(b);
            EXPECT_NE(p, nullptr);
            EXPECT_EQ(p, &array->array.items[1]);
            EXPECT_EQ(array->array.size, 2);
            EXPECT_EQ(A1C_Array_get(&array->array, 1), p);
            A1C_Item_int64(p, 2);
        }
        {
            auto p = A1C_ArrayBuilder_add(b);
            EXPECT_EQ(p, nullptr);
            EXPECT_EQ(array->array.size, 2);
        }
        {
            auto p = A1C_ArrayBuilder_add(b);
            EXPECT_EQ(p, nullptr);
            EXPECT_EQ(array->array.size, 2);
        }
        testArray(array);
    }
}

TEST_F(A1CBorTest, LargeArray)
{
    size_t size = 1000;
    auto array  = A1C_Item_root(&arena);
    ASSERT_NE(array, nullptr);
    auto a = A1C_Item_array(array, size, &arena);
    ASSERT_NE(a, nullptr);

    for (size_t i = 0; i < size; ++i) {
        A1C_Item_int64(a + i, i);
    }

    auto encoded = encode(array);
    auto decoded = decode(encoded);
    ASSERT_EQ(*array, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);

    for (size_t i = 0; i < size; ++i) {
        auto item = A1C_Array_get(&decoded->array, i);
        ASSERT_NE(item, nullptr);
        ASSERT_EQ(item->type, A1C_ItemType_int64);
        ASSERT_EQ(item->int64, i);
    }
    ASSERT_EQ(*array, *deepcopy(array));
}

TEST_F(A1CBorTest, DeeplyNested)
{
    size_t depth = A1C_MAX_DEPTH_DEFAULT;
    auto item    = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);

    A1C_Item* current = item;
    for (size_t i = 0; i < depth - 1; ++i) {
        auto tag = A1C_Item_tag(current, i, &arena);
        ASSERT_NE(tag, nullptr);
        current = tag;
    }
    A1C_Item_null(current);

    auto encoded = encode(item);
    auto decoded = decode(encoded);
    ASSERT_EQ(*item, *decoded);
    ASSERT_EQ(decoded->parent, nullptr);

    auto tag = A1C_Item_tag(current, 100, &arena);
    ASSERT_NE(tag, nullptr);
    A1C_Item_null(tag);

    encoded = encode(item);

    A1C_Decoder decoder;
    A1C_Decoder_init(&decoder, arena, {});
    ASSERT_EQ(
            A1C_Decoder_decode(
                    &decoder,
                    reinterpret_cast<const uint8_t*>(encoded.data()),
                    encoded.size()),
            nullptr);
    ASSERT_EQ(decoder.error.type, A1C_ErrorType_maxDepthExceeded);
}

static constexpr char kExpectedJSON[] = R"({
  "key": "value",
  42: [
    -1,
    -3.14,
    3.14,
    true,
    false,
    null,
    undefined,
    "aGVsbG8gd29ybGQxAA==",
    "this is a longer string",
    [
    ],
    {
    },
    {
      "type": "tag",
      "tag": 100,
      "value": true
    },
    {
      "type": "simple",
      "value": 42
    },
    [
      "",
      "aA==",
      "aGU=",
      "aGVs",
      "aGVsbA=="
    ]
  ]
})";

TEST_F(A1CBorTest, Json)
{
    auto item = A1C_Item_root(&arena);
    ASSERT_NE(item, nullptr);
    auto map = A1C_Item_map(item, 2, &arena);
    ASSERT_NE(map, nullptr);
    A1C_Item_string_refCStr(&map[0].key, "key");
    A1C_Item_string_refCStr(&map[0].val, "value");
    A1C_Item_int64(&map[1].key, 42);
    auto array = A1C_Item_array(&map[1].val, 14, &arena);
    ASSERT_NE(array, nullptr);
    A1C_Item_int64(array + 0, -1);
    A1C_Item_float32(array + 1, -3.14);
    A1C_Item_float64(array + 2, 3.14);
    A1C_Item_boolean(array + 3, true);
    A1C_Item_boolean(array + 4, false);
    A1C_Item_null(array + 5);
    A1C_Item_undefined(array + 6);
    uint8_t shortData[] = "hello world1";
    A1C_Item_bytes_ref(array + 7, shortData, sizeof(shortData));
    A1C_Item_string_refCStr(array + 8, "this is a longer string");
    ASSERT_NE(A1C_Item_array(array + 9, 0, &arena), nullptr);
    ASSERT_NE(A1C_Item_map(array + 10, 0, &arena), nullptr);
    auto tag = A1C_Item_tag(array + 11, 100, &arena);
    ASSERT_NE(tag, nullptr);
    A1C_Item_boolean(tag, true);
    array[12].type   = A1C_ItemType_simple;
    array[12].simple = 42;
    array            = A1C_Item_array(array + 13, 5, &arena);
    ASSERT_NE(array, nullptr);
    A1C_Item_bytes_ref(array + 0, shortData, 0);
    A1C_Item_bytes_ref(array + 1, shortData, 1);
    A1C_Item_bytes_ref(array + 2, shortData, 2);
    A1C_Item_bytes_ref(array + 3, shortData, 3);
    A1C_Item_bytes_ref(array + 4, shortData, 4);

    auto encoded = encodeJson(item);
    EXPECT_EQ(encoded, kExpectedJSON) << encoded;
    ASSERT_EQ(*item, *deepcopy(item));
}

TEST_F(A1CBorTest, JsonRoundTrip)
{
    json data;
    data["key"]   = "value";
    data["null"]  = nullptr;
    data["array"] = json::array();
    data["array"].push_back(-1);
    data["array"].push_back(-3.14);
    data["array"].push_back(3.14);
    data["array"].push_back(true);
    data["array"].push_back(false);
    data["array"].push_back(nullptr);
    data["array"].push_back("hello world1");
    data["array"].push_back(
            json::object(
                    {
                            { "type", "tag" },
                            { "tag", 100 },
                            { "value", json::array({ 0, 1, 2 }) },
                    }));
    data["false"]  = false;
    data["true"]   = true;
    data["map"]    = json::object();
    data["nested"] = json::object(
            { { "map", json::object() }, { "array", json::array() } });

    auto encoded = json::to_cbor(data);
    auto item    = decode(encoded);
    auto jsonStr = encodeJson(item);
    auto decoded = json::parse(jsonStr);
    ASSERT_EQ(data, decoded);

    auto reencoded = encode(item);
    ASSERT_EQ(encoded.size(), reencoded.size());
    ASSERT_EQ(memcmp(encoded.data(), reencoded.data(), encoded.size()), 0);
    ASSERT_EQ(*item, *deepcopy(item));

    // Test A1C_convert_cbor_to_json
    struct StringView cbor = StringView_init(
            reinterpret_cast<const char*>(encoded.data()), encoded.size());
    Arena* const zl_arena = ALLOC_HeapArena_create();
    ASSERT_TRUE(zl_arena);
    void* dst      = nullptr;
    size_t dstSize = 0;
    ASSERT_FALSE(ZL_RES_isError(
            A1C_convert_cbor_to_json(NULL, zl_arena, &dst, &dstSize, cbor)));
    ASSERT_NE(dst, nullptr);
    ASSERT_NE(dstSize, 0);
    ASSERT_EQ(reinterpret_cast<const char*>(dst)[dstSize], '\0');
    std::string json_str(static_cast<const char*>(dst), dstSize);
    ALLOC_Arena_freeArena(zl_arena);
    auto parsed_json = json::parse(json_str);
    ASSERT_EQ(data, parsed_json);
}
