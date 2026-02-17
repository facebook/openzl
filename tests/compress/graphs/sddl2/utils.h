#include <gtest/gtest.h>

#include "openzl/compress/graphs/sddl2/sddl2_vm.h"

namespace openzl {
namespace sddl2 {
namespace testing {
template <size_t StackCapacity>
class SDDL2StackTestCustomCapacity : public ::testing::Test {
   protected:
    void SetUp() override
    {
        stack_ = (SDDL2_Stack*)malloc(sizeof(SDDL2_Stack));
        assert(stack_ != NULL);

        stack_->items =
                (SDDL2_Value*)malloc(sizeof(SDDL2_Value) * StackCapacity);
        assert(stack_->items != NULL);

        stack_->capacity = StackCapacity;
        SDDL2_Stack_init(stack_);
    }
    void TearDown() override
    {
        free(stack_->items);
        free(stack_);
    }

    static void* alloc_fn(void* allocator_ctx, size_t size)
    {
        auto arena = (std::vector<std::string>*)allocator_ctx;
        arena->push_back(std::string(size, 'x'));
        return arena->back().data();
    }

    SDDL2_Stack* stack_;
    std::vector<std::string> arena_;
    void* alloc_ctx_ = &arena_;
};

using SDDL2StackTest = SDDL2StackTestCustomCapacity<100>;

static void popAndVerifyI64(SDDL2_Stack* stack, int64_t expected)
{
    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_I64);
    EXPECT_EQ(result.value.as_i64, expected);
}

} // namespace testing
} // namespace sddl2
} // namespace openzl
