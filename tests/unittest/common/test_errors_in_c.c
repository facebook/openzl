// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/operation_context.h"
#include "openzl/zl_errors.h"

#include "tests/unittest/common/test_errors_in_c.h"

typedef enum { FOO, BAR } Foo;

#define DEFINE_ERROR_MESSAGE_CONTAINS(type_name, type)        \
    /* typename type type_name; */                            \
    static ZL_Report generate_error_message_##type_name(      \
            ZL_OperationContext* opCtx, type arg1, type arg2) \
    {                                                         \
        ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);                \
        ZL_ERR_IF_LT(arg1, arg2, GENERIC);                    \
        return ZL_returnSuccess();                            \
    }

#define EXPECT_ERROR_MESSAGE_CONTAINS(arg1, arg2, msg)                        \
    do {                                                                      \
        ZL_Report _report = generate_error_message_##arg1(opCtx, arg1, arg2); \
        ZL_ERR_IF_NOT(                                                        \
                ZL_RES_isError(_report),                                      \
                GENERIC,                                                      \
                "ZL_ERR_IF_LT"                                                \
                "(" #arg1 ", " #arg2 ") failed to fail.");                    \
        ZL_RES_error(_report) =                                               \
                ZL_E_ADDFRAME(ZL_RES_error(_report), ZL_EE_EMPTY, "");        \
        const char* _str = ZL_E_str(ZL_RES_error(_report));                   \
        ZL_ERR_IF_NULL(_str, GENERIC, "Error message is NULL!");              \
        const char* _found = strstr(_str, msg);                               \
        fprintf(stderr, "%s\n", _str);                                        \
        fprintf(stderr, "%s\n", _found);                                      \
        ZL_ERR_IF_NULL(                                                       \
                _found,                                                       \
                GENERIC,                                                      \
                "Message '%s' not found in error message '%s'",               \
                msg,                                                          \
                _str);                                                        \
    } while (0)

DEFINE_ERROR_MESSAGE_CONTAINS(c_1, char)
DEFINE_ERROR_MESSAGE_CONTAINS(h_1, short)
DEFINE_ERROR_MESSAGE_CONTAINS(i_1, int)
DEFINE_ERROR_MESSAGE_CONTAINS(l_1, long)
DEFINE_ERROR_MESSAGE_CONTAINS(ll_1, long long)

DEFINE_ERROR_MESSAGE_CONTAINS(sc_1, signed char)
DEFINE_ERROR_MESSAGE_CONTAINS(sh_1, signed short)
DEFINE_ERROR_MESSAGE_CONTAINS(si_1, signed int)
DEFINE_ERROR_MESSAGE_CONTAINS(sl_1, signed long)
DEFINE_ERROR_MESSAGE_CONTAINS(sll_1, signed long long)

DEFINE_ERROR_MESSAGE_CONTAINS(uc_1, unsigned char)
DEFINE_ERROR_MESSAGE_CONTAINS(uh_1, unsigned short)
DEFINE_ERROR_MESSAGE_CONTAINS(ui_1, unsigned int)
DEFINE_ERROR_MESSAGE_CONTAINS(ul_1, unsigned long)
DEFINE_ERROR_MESSAGE_CONTAINS(ull_1, unsigned long long)

DEFINE_ERROR_MESSAGE_CONTAINS(i8_1, int8_t)
DEFINE_ERROR_MESSAGE_CONTAINS(i16_1, int16_t)
DEFINE_ERROR_MESSAGE_CONTAINS(i32_1, int32_t)
DEFINE_ERROR_MESSAGE_CONTAINS(i64_1, int64_t)

DEFINE_ERROR_MESSAGE_CONTAINS(u8_1, uint8_t)
DEFINE_ERROR_MESSAGE_CONTAINS(u16_1, uint16_t)
DEFINE_ERROR_MESSAGE_CONTAINS(u32_1, uint32_t)
DEFINE_ERROR_MESSAGE_CONTAINS(u64_1, uint64_t)

DEFINE_ERROR_MESSAGE_CONTAINS(f_1, float)
DEFINE_ERROR_MESSAGE_CONTAINS(lf_1, double)
DEFINE_ERROR_MESSAGE_CONTAINS(llf_1, long double)

DEFINE_ERROR_MESSAGE_CONTAINS(pi_1, const int32_t*)

DEFINE_ERROR_MESSAGE_CONTAINS(e_1, Foo)

ZL_Report ZS2_test_errors_binary_arg_types_deduced_in_c_inner(
        ZL_OperationContext* opCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(opCtx);

    const char c_1       = (char)1;
    const char c_2       = c_1 + 1;
    const short h_1      = c_1;
    const short h_2      = h_1 + 1;
    const int i_1        = c_1;
    const int i_2        = i_1 + 1;
    const long l_1       = c_1;
    const long l_2       = l_1 + 1;
    const long long ll_1 = c_1;
    const long long ll_2 = ll_1 + 1;

    const signed char sc_1       = (signed char)1;
    const signed char sc_2       = sc_1 + 1;
    const signed short sh_1      = sc_1;
    const signed short sh_2      = sh_1 + 1;
    const signed int si_1        = sc_1;
    const signed int si_2        = si_1 + 1;
    const signed long sl_1       = sc_1;
    const signed long sl_2       = sl_1 + 1;
    const signed long long sll_1 = sc_1;
    const signed long long sll_2 = sll_1 + 1;

    const unsigned char uc_1       = (unsigned char)1;
    const unsigned char uc_2       = uc_1 + 1;
    const unsigned short uh_1      = uc_1;
    const unsigned short uh_2      = uh_1 + 1;
    const unsigned int ui_1        = uc_1;
    const unsigned int ui_2        = ui_1 + 1;
    const unsigned long ul_1       = uc_1;
    const unsigned long ul_2       = ul_1 + 1;
    const unsigned long long ull_1 = uc_1;
    const unsigned long long ull_2 = ull_1 + 1;

    const int8_t i8_1   = sc_1;
    const int8_t i8_2   = i8_1 + 1;
    const int16_t i16_1 = sc_1;
    const int16_t i16_2 = i16_1 + 1;
    const int32_t i32_1 = sc_1;
    const int32_t i32_2 = i32_1 + 1;
    const int64_t i64_1 = sc_1;
    const int64_t i64_2 = i64_1 + 1;

    const uint8_t u8_1   = uc_1;
    const uint8_t u8_2   = u8_1 + 1;
    const uint16_t u16_1 = uc_1;
    const uint16_t u16_2 = u16_1 + 1;
    const uint32_t u32_1 = uc_1;
    const uint32_t u32_2 = u32_1 + 1;
    const uint64_t u64_1 = uc_1;
    const uint64_t u64_2 = u64_1 + 1;

    const float f_1         = (float)123.4;
    const float f_2         = (float)123.5;
    const double lf_1       = f_1;
    const double lf_2       = lf_1 + 0.1;
    const long double llf_1 = f_1;
    const long double llf_2 = llf_1 + 0.1;

    const int32_t* const pi_1 = &i_1;
    const int32_t* const pi_2 = pi_1 + 1;

    const Foo e_1 = FOO;
    const Foo e_2 = BAR;

    ZL_ERR_IF_EQ(c_1, c_2, GENERIC);
    ZL_ERR_IF_EQ(h_1, h_2, GENERIC);
    ZL_ERR_IF_EQ(i_1, i_2, GENERIC);
    ZL_ERR_IF_EQ(l_1, l_2, GENERIC);
    ZL_ERR_IF_EQ(ll_1, ll_2, GENERIC);

    ZL_ERR_IF_EQ(sc_1, sc_2, GENERIC);
    ZL_ERR_IF_EQ(sh_1, sh_2, GENERIC);
    ZL_ERR_IF_EQ(si_1, si_2, GENERIC);
    ZL_ERR_IF_EQ(sl_1, sl_2, GENERIC);
    ZL_ERR_IF_EQ(sll_1, sll_2, GENERIC);

    ZL_ERR_IF_EQ(uc_1, uc_2, GENERIC);
    ZL_ERR_IF_EQ(uh_1, uh_2, GENERIC);
    ZL_ERR_IF_EQ(ui_1, ui_2, GENERIC);
    ZL_ERR_IF_EQ(ul_1, ul_2, GENERIC);
    ZL_ERR_IF_EQ(ull_1, ull_2, GENERIC);

    ZL_ERR_IF_EQ(i8_1, i8_2, GENERIC);
    ZL_ERR_IF_EQ(i16_1, i16_2, GENERIC);
    ZL_ERR_IF_EQ(i32_1, i32_2, GENERIC);
    ZL_ERR_IF_EQ(i64_1, i64_2, GENERIC);

    ZL_ERR_IF_EQ(u8_1, u8_2, GENERIC);
    ZL_ERR_IF_EQ(u16_1, u16_2, GENERIC);
    ZL_ERR_IF_EQ(u32_1, u32_2, GENERIC);
    ZL_ERR_IF_EQ(u64_1, u64_2, GENERIC);

    ZL_ERR_IF_GE(f_1, f_2, GENERIC);
    ZL_ERR_IF_GE(lf_1, lf_2, GENERIC);
    ZL_ERR_IF_GE(llf_1, llf_2, GENERIC);

    ZL_ERR_IF_EQ(pi_1, pi_2, GENERIC);

    EXPECT_ERROR_MESSAGE_CONTAINS(c_1, c_2, "(int)"); // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(h_1, h_2, "(int)"); // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(i_1, i_2, "(int)");
    EXPECT_ERROR_MESSAGE_CONTAINS(l_1, l_2, "(long)");
    EXPECT_ERROR_MESSAGE_CONTAINS(ll_1, ll_2, "(long long)");

    EXPECT_ERROR_MESSAGE_CONTAINS(sc_1, sc_2, "(int)"); // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(sh_1, sh_2, "(int)"); // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(si_1, si_2, "(int)");
    EXPECT_ERROR_MESSAGE_CONTAINS(sl_1, sl_2, "(long)");
    EXPECT_ERROR_MESSAGE_CONTAINS(sll_1, sll_2, "(long long)");

    EXPECT_ERROR_MESSAGE_CONTAINS(uc_1, uc_2, "(int)"); // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(uh_1, uh_2, "(int)"); // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(ui_1, ui_2, "(unsigned int)");
    EXPECT_ERROR_MESSAGE_CONTAINS(ul_1, ul_2, "(unsigned long)");
    EXPECT_ERROR_MESSAGE_CONTAINS(ull_1, ull_2, "(unsigned long long)");

    EXPECT_ERROR_MESSAGE_CONTAINS(i8_1, i8_2, "(int)");   // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(i16_1, i16_2, "(int)"); // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(i32_1, i32_2, "(int)");
    EXPECT_ERROR_MESSAGE_CONTAINS(
            i64_1, i64_2, "(long"); // could be long or long long

    EXPECT_ERROR_MESSAGE_CONTAINS(u8_1, u8_2, "(int)");   // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(u16_1, u16_2, "(int)"); // because promotion
    EXPECT_ERROR_MESSAGE_CONTAINS(u32_1, u32_2, "(unsigned int)");
    EXPECT_ERROR_MESSAGE_CONTAINS(
            u64_1,
            u64_2,
            "(unsigned long"); // could be unsigned long or unsigned long long

    EXPECT_ERROR_MESSAGE_CONTAINS(f_1, f_2, "(float)");
    EXPECT_ERROR_MESSAGE_CONTAINS(lf_1, lf_2, "(double)");
    EXPECT_ERROR_MESSAGE_CONTAINS(llf_1, llf_2, "(long double)");

    EXPECT_ERROR_MESSAGE_CONTAINS(pi_1, pi_2, "(pointer)");

    // Enum underlying type differs across platforms:
    // - Linux/GCC: "(unsigned int)"
    // - Windows/MSVC: "(int)"
    // - ARM64 may use either depending on ABI
    // Check for both possibilities explicitly
    {
        ZL_Report _report = generate_error_message_e_1(opCtx, e_1, e_2);
        ZL_ERR_IF_NOT(
                ZL_RES_isError(_report),
                GENERIC,
                "ZL_ERR_IF_LT(e_1, e_2) failed to fail.");
        ZL_RES_error(_report) =
                ZL_E_ADDFRAME(ZL_RES_error(_report), ZL_EE_EMPTY, "");
        const char* _str = ZL_E_str(ZL_RES_error(_report));
        ZL_ERR_IF_NULL(_str, GENERIC, "Error message is NULL!");
        const char* _found_int  = strstr(_str, "(int)");
        const char* _found_uint = strstr(_str, "(unsigned int)");
        if (!_found_int && !_found_uint) {
            ZL_ERR(GENERIC,
                   "Message '(int)' or '(unsigned int)' not found in error message '%s'",
                   _str);
        }
    }

    return ZL_returnSuccess();
}
