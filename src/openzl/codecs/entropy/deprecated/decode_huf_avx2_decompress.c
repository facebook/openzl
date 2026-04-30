// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openzl/zl_errors.h"

#define HUF_STATIC_LINKING_ONLY

#include "openzl/codecs/entropy/decode_huffman_kernel.h"
#include "openzl/codecs/entropy/deprecated/common_huf_avx2.h"
#include "openzl/fse/hist.h"
#include "openzl/fse/huf.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"

#define ZS_HUF_RET_IF_NOT(cond)                         \
    do {                                                \
        if (!(cond)) {                                  \
            ZL_DLOG(ERROR, "HufAvx2 error: %s", #cond); \
            return 0;                                   \
        }                                               \
    } while (0)

#define kMaxHufLog 12
#define kMaxHuf16Log 13
#define kNumStates 32

#if ZL_HAS_AVX2

#    include <immintrin.h>
#    if !defined(_MSC_VER)
#        include <popcntintrin.h>
#        include <x86intrin.h>
#    endif
#    include <xmmintrin.h>

#    define _ 9
// clang-format off
static ZL_ALIGNED(32) int32_t permute0[16][8] = {
    {0, 0, 0, 0, 8, 8, 8, 8},
    {7, 0, 0, 0, 7, 7, 7, 7},
    {0, 7, 0, 0, 7, 7, 7, 7},
    {7, 6, 0, 0, 6, 6, 6, 6},
    {0, 0, 7, 0, 7, 7, 7, 7},
    {7, 0, 6, 0, 6, 6, 6, 6},
    {0, 7, 6, 0, 6, 6, 6, 6},
    {7, 6, 5, 0, 5, 5, 5, 5},
    {0, 0, 0, 7, 7, 7, 7, 7},
    {7, 0, 0, 6, 6, 6, 6, 6},
    {0, 7, 0, 6, 6, 6, 6, 6},
    {7, 6, 0, 5, 5, 5, 5, 5},
    {0, 0, 7, 6, 6, 6, 6, 6},
    {7, 0, 6, 5, 5, 5, 5, 5},
    {0, 7, 6, 5, 5, 5, 5, 5},
    {7, 6, 5, 4, 4, 4, 4, 4},
};
static ZL_ALIGNED(32) int32_t permute1[16][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, -1, 0, 0, 0},
    {0, 0, 0, 0, 0, -1, 0, 0},
    {0, 0, 0, 0, -1, -2, 0, 0},
    {0, 0, 0, 0, 0, 0, -1, 0},
    {0, 0, 0, 0, -1, 0, -2, 0},
    {0, 0, 0, 0, 0, -1, -2, 0},
    {0, 0, 0, 0, -1, -2, -3, 0},
    {0, 0, 0, 0, 0, 0, 0, -1},
    {0, 0, 0, 0, -1, 0, 0, -2},
    {0, 0, 0, 0, 0, -1, 0, -2},
    {0, 0, 0, 0, -1, -2, 0, -3},
    {0, 0, 0, 0, 0, 0, -1, -2},
    {0, 0, 0, 0, -1, 0, -2, -3},
    {0, 0, 0, 0, 0, -1, -2, -3},
    {0, 0, 0, 0, -1, -2, -3, -4},
};
static ZL_ALIGNED(32) uint32_t permute[256][8] = { // reverse binary bit order
{_, _, _, _, _, _, _, _},
{7, _, _, _, _, _, _, _},
{_, 7, _, _, _, _, _, _},
{7, 6, _, _, _, _, _, _},
{_, _, 7, _, _, _, _, _},
{7, _, 6, _, _, _, _, _},
{_, 7, 6, _, _, _, _, _},
{7, 6, 5, _, _, _, _, _},
{_, _, _, 7, _, _, _, _},
{7, _, _, 6, _, _, _, _},
{_, 7, _, 6, _, _, _, _},
{7, 6, _, 5, _, _, _, _},
{_, _, 7, 6, _, _, _, _},
{7, _, 6, 5, _, _, _, _},
{_, 7, 6, 5, _, _, _, _},
{7, 6, 5, 4, _, _, _, _},
{_, _, _, _, 7, _, _, _},
{7, _, _, _, 6, _, _, _},
{_, 7, _, _, 6, _, _, _},
{7, 6, _, _, 5, _, _, _},
{_, _, 7, _, 6, _, _, _},
{7, _, 6, _, 5, _, _, _},
{_, 7, 6, _, 5, _, _, _},
{7, 6, 5, _, 4, _, _, _},
{_, _, _, 7, 6, _, _, _},
{7, _, _, 6, 5, _, _, _},
{_, 7, _, 6, 5, _, _, _},
{7, 6, _, 5, 4, _, _, _},
{_, _, 7, 6, 5, _, _, _},
{7, _, 6, 5, 4, _, _, _},
{_, 7, 6, 5, 4, _, _, _},
{7, 6, 5, 4, 3, _, _, _},
{_, _, _, _, _, 7, _, _},
{7, _, _, _, _, 6, _, _},
{_, 7, _, _, _, 6, _, _},
{7, 6, _, _, _, 5, _, _},
{_, _, 7, _, _, 6, _, _},
{7, _, 6, _, _, 5, _, _},
{_, 7, 6, _, _, 5, _, _},
{7, 6, 5, _, _, 4, _, _},
{_, _, _, 7, _, 6, _, _},
{7, _, _, 6, _, 5, _, _},
{_, 7, _, 6, _, 5, _, _},
{7, 6, _, 5, _, 4, _, _},
{_, _, 7, 6, _, 5, _, _},
{7, _, 6, 5, _, 4, _, _},
{_, 7, 6, 5, _, 4, _, _},
{7, 6, 5, 4, _, 3, _, _},
{_, _, _, _, 7, 6, _, _},
{7, _, _, _, 6, 5, _, _},
{_, 7, _, _, 6, 5, _, _},
{7, 6, _, _, 5, 4, _, _},
{_, _, 7, _, 6, 5, _, _},
{7, _, 6, _, 5, 4, _, _},
{_, 7, 6, _, 5, 4, _, _},
{7, 6, 5, _, 4, 3, _, _},
{_, _, _, 7, 6, 5, _, _},
{7, _, _, 6, 5, 4, _, _},
{_, 7, _, 6, 5, 4, _, _},
{7, 6, _, 5, 4, 3, _, _},
{_, _, 7, 6, 5, 4, _, _},
{7, _, 6, 5, 4, 3, _, _},
{_, 7, 6, 5, 4, 3, _, _},
{7, 6, 5, 4, 3, 2, _, _},
{_, _, _, _, _, _, 7, _},
{7, _, _, _, _, _, 6, _},
{_, 7, _, _, _, _, 6, _},
{7, 6, _, _, _, _, 5, _},
{_, _, 7, _, _, _, 6, _},
{7, _, 6, _, _, _, 5, _},
{_, 7, 6, _, _, _, 5, _},
{7, 6, 5, _, _, _, 4, _},
{_, _, _, 7, _, _, 6, _},
{7, _, _, 6, _, _, 5, _},
{_, 7, _, 6, _, _, 5, _},
{7, 6, _, 5, _, _, 4, _},
{_, _, 7, 6, _, _, 5, _},
{7, _, 6, 5, _, _, 4, _},
{_, 7, 6, 5, _, _, 4, _},
{7, 6, 5, 4, _, _, 3, _},
{_, _, _, _, 7, _, 6, _},
{7, _, _, _, 6, _, 5, _},
{_, 7, _, _, 6, _, 5, _},
{7, 6, _, _, 5, _, 4, _},
{_, _, 7, _, 6, _, 5, _},
{7, _, 6, _, 5, _, 4, _},
{_, 7, 6, _, 5, _, 4, _},
{7, 6, 5, _, 4, _, 3, _},
{_, _, _, 7, 6, _, 5, _},
{7, _, _, 6, 5, _, 4, _},
{_, 7, _, 6, 5, _, 4, _},
{7, 6, _, 5, 4, _, 3, _},
{_, _, 7, 6, 5, _, 4, _},
{7, _, 6, 5, 4, _, 3, _},
{_, 7, 6, 5, 4, _, 3, _},
{7, 6, 5, 4, 3, _, 2, _},
{_, _, _, _, _, 7, 6, _},
{7, _, _, _, _, 6, 5, _},
{_, 7, _, _, _, 6, 5, _},
{7, 6, _, _, _, 5, 4, _},
{_, _, 7, _, _, 6, 5, _},
{7, _, 6, _, _, 5, 4, _},
{_, 7, 6, _, _, 5, 4, _},
{7, 6, 5, _, _, 4, 3, _},
{_, _, _, 7, _, 6, 5, _},
{7, _, _, 6, _, 5, 4, _},
{_, 7, _, 6, _, 5, 4, _},
{7, 6, _, 5, _, 4, 3, _},
{_, _, 7, 6, _, 5, 4, _},
{7, _, 6, 5, _, 4, 3, _},
{_, 7, 6, 5, _, 4, 3, _},
{7, 6, 5, 4, _, 3, 2, _},
{_, _, _, _, 7, 6, 5, _},
{7, _, _, _, 6, 5, 4, _},
{_, 7, _, _, 6, 5, 4, _},
{7, 6, _, _, 5, 4, 3, _},
{_, _, 7, _, 6, 5, 4, _},
{7, _, 6, _, 5, 4, 3, _},
{_, 7, 6, _, 5, 4, 3, _},
{7, 6, 5, _, 4, 3, 2, _},
{_, _, _, 7, 6, 5, 4, _},
{7, _, _, 6, 5, 4, 3, _},
{_, 7, _, 6, 5, 4, 3, _},
{7, 6, _, 5, 4, 3, 2, _},
{_, _, 7, 6, 5, 4, 3, _},
{7, _, 6, 5, 4, 3, 2, _},
{_, 7, 6, 5, 4, 3, 2, _},
{7, 6, 5, 4, 3, 2, 1, _},
{_, _, _, _, _, _, _, 7},
{7, _, _, _, _, _, _, 6},
{_, 7, _, _, _, _, _, 6},
{7, 6, _, _, _, _, _, 5},
{_, _, 7, _, _, _, _, 6},
{7, _, 6, _, _, _, _, 5},
{_, 7, 6, _, _, _, _, 5},
{7, 6, 5, _, _, _, _, 4},
{_, _, _, 7, _, _, _, 6},
{7, _, _, 6, _, _, _, 5},
{_, 7, _, 6, _, _, _, 5},
{7, 6, _, 5, _, _, _, 4},
{_, _, 7, 6, _, _, _, 5},
{7, _, 6, 5, _, _, _, 4},
{_, 7, 6, 5, _, _, _, 4},
{7, 6, 5, 4, _, _, _, 3},
{_, _, _, _, 7, _, _, 6},
{7, _, _, _, 6, _, _, 5},
{_, 7, _, _, 6, _, _, 5},
{7, 6, _, _, 5, _, _, 4},
{_, _, 7, _, 6, _, _, 5},
{7, _, 6, _, 5, _, _, 4},
{_, 7, 6, _, 5, _, _, 4},
{7, 6, 5, _, 4, _, _, 3},
{_, _, _, 7, 6, _, _, 5},
{7, _, _, 6, 5, _, _, 4},
{_, 7, _, 6, 5, _, _, 4},
{7, 6, _, 5, 4, _, _, 3},
{_, _, 7, 6, 5, _, _, 4},
{7, _, 6, 5, 4, _, _, 3},
{_, 7, 6, 5, 4, _, _, 3},
{7, 6, 5, 4, 3, _, _, 2},
{_, _, _, _, _, 7, _, 6},
{7, _, _, _, _, 6, _, 5},
{_, 7, _, _, _, 6, _, 5},
{7, 6, _, _, _, 5, _, 4},
{_, _, 7, _, _, 6, _, 5},
{7, _, 6, _, _, 5, _, 4},
{_, 7, 6, _, _, 5, _, 4},
{7, 6, 5, _, _, 4, _, 3},
{_, _, _, 7, _, 6, _, 5},
{7, _, _, 6, _, 5, _, 4},
{_, 7, _, 6, _, 5, _, 4},
{7, 6, _, 5, _, 4, _, 3},
{_, _, 7, 6, _, 5, _, 4},
{7, _, 6, 5, _, 4, _, 3},
{_, 7, 6, 5, _, 4, _, 3},
{7, 6, 5, 4, _, 3, _, 2},
{_, _, _, _, 7, 6, _, 5},
{7, _, _, _, 6, 5, _, 4},
{_, 7, _, _, 6, 5, _, 4},
{7, 6, _, _, 5, 4, _, 3},
{_, _, 7, _, 6, 5, _, 4},
{7, _, 6, _, 5, 4, _, 3},
{_, 7, 6, _, 5, 4, _, 3},
{7, 6, 5, _, 4, 3, _, 2},
{_, _, _, 7, 6, 5, _, 4},
{7, _, _, 6, 5, 4, _, 3},
{_, 7, _, 6, 5, 4, _, 3},
{7, 6, _, 5, 4, 3, _, 2},
{_, _, 7, 6, 5, 4, _, 3},
{7, _, 6, 5, 4, 3, _, 2},
{_, 7, 6, 5, 4, 3, _, 2},
{7, 6, 5, 4, 3, 2, _, 1},
{_, _, _, _, _, _, 7, 6},
{7, _, _, _, _, _, 6, 5},
{_, 7, _, _, _, _, 6, 5},
{7, 6, _, _, _, _, 5, 4},
{_, _, 7, _, _, _, 6, 5},
{7, _, 6, _, _, _, 5, 4},
{_, 7, 6, _, _, _, 5, 4},
{7, 6, 5, _, _, _, 4, 3},
{_, _, _, 7, _, _, 6, 5},
{7, _, _, 6, _, _, 5, 4},
{_, 7, _, 6, _, _, 5, 4},
{7, 6, _, 5, _, _, 4, 3},
{_, _, 7, 6, _, _, 5, 4},
{7, _, 6, 5, _, _, 4, 3},
{_, 7, 6, 5, _, _, 4, 3},
{7, 6, 5, 4, _, _, 3, 2},
{_, _, _, _, 7, _, 6, 5},
{7, _, _, _, 6, _, 5, 4},
{_, 7, _, _, 6, _, 5, 4},
{7, 6, _, _, 5, _, 4, 3},
{_, _, 7, _, 6, _, 5, 4},
{7, _, 6, _, 5, _, 4, 3},
{_, 7, 6, _, 5, _, 4, 3},
{7, 6, 5, _, 4, _, 3, 2},
{_, _, _, 7, 6, _, 5, 4},
{7, _, _, 6, 5, _, 4, 3},
{_, 7, _, 6, 5, _, 4, 3},
{7, 6, _, 5, 4, _, 3, 2},
{_, _, 7, 6, 5, _, 4, 3},
{7, _, 6, 5, 4, _, 3, 2},
{_, 7, 6, 5, 4, _, 3, 2},
{7, 6, 5, 4, 3, _, 2, 1},
{_, _, _, _, _, 7, 6, 5},
{7, _, _, _, _, 6, 5, 4},
{_, 7, _, _, _, 6, 5, 4},
{7, 6, _, _, _, 5, 4, 3},
{_, _, 7, _, _, 6, 5, 4},
{7, _, 6, _, _, 5, 4, 3},
{_, 7, 6, _, _, 5, 4, 3},
{7, 6, 5, _, _, 4, 3, 2},
{_, _, _, 7, _, 6, 5, 4},
{7, _, _, 6, _, 5, 4, 3},
{_, 7, _, 6, _, 5, 4, 3},
{7, 6, _, 5, _, 4, 3, 2},
{_, _, 7, 6, _, 5, 4, 3},
{7, _, 6, 5, _, 4, 3, 2},
{_, 7, 6, 5, _, 4, 3, 2},
{7, 6, 5, 4, _, 3, 2, 1},
{_, _, _, _, 7, 6, 5, 4},
{7, _, _, _, 6, 5, 4, 3},
{_, 7, _, _, 6, 5, 4, 3},
{7, 6, _, _, 5, 4, 3, 2},
{_, _, 7, _, 6, 5, 4, 3},
{7, _, 6, _, 5, 4, 3, 2},
{_, 7, 6, _, 5, 4, 3, 2},
{7, 6, 5, _, 4, 3, 2, 1},
{_, _, _, 7, 6, 5, 4, 3},
{7, _, _, 6, 5, 4, 3, 2},
{_, 7, _, 6, 5, 4, 3, 2},
{7, 6, _, 5, 4, 3, 2, 1},
{_, _, 7, 6, 5, 4, 3, 2},
{7, _, 6, 5, 4, 3, 2, 1},
{_, 7, 6, 5, 4, 3, 2, 1},
{7, 6, 5, 4, 3, 2, 1, 0},
//   { _,_,_,_,_,_,_,_,},
//   { 0,_,_,_,_,_,_,_,},
//   { _,0,_,_,_,_,_,_,},
//   { 0,1,_,_,_,_,_,_,},
//   { _,_,0,_,_,_,_,_,},
//   { 0,_,1,_,_,_,_,_,},
//   { _,0,1,_,_,_,_,_,},
//   { 0,1,2,_,_,_,_,_,},
//   { _,_,_,0,_,_,_,_,},
//   { 0,_,_,1,_,_,_,_,},
//   { _,0,_,1,_,_,_,_,},
//   { 0,1,_,2,_,_,_,_,},
//   { _,_,0,1,_,_,_,_,},
//   { 0,_,1,2,_,_,_,_,},
//   { _,0,1,2,_,_,_,_,},
//   { 0,1,2,3,_,_,_,_,},
//   { _,_,_,_,0,_,_,_,},
//   { 0,_,_,_,1,_,_,_,},
//   { _,0,_,_,1,_,_,_,},
//   { 0,1,_,_,2,_,_,_,},
//   { _,_,0,_,1,_,_,_,},
//   { 0,_,1,_,2,_,_,_,},
//   { _,0,1,_,2,_,_,_,},
//   { 0,1,2,_,3,_,_,_,},
//   { _,_,_,0,1,_,_,_,},
//   { 0,_,_,1,2,_,_,_,},
//   { _,0,_,1,2,_,_,_,},
//   { 0,1,_,2,3,_,_,_,},
//   { _,_,0,1,2,_,_,_,},
//   { 0,_,1,2,3,_,_,_,},
//   { _,0,1,2,3,_,_,_,},
//   { 0,1,2,3,4,_,_,_,},
//   { _,_,_,_,_,0,_,_,},
//   { 0,_,_,_,_,1,_,_,},
//   { _,0,_,_,_,1,_,_,},
//   { 0,1,_,_,_,2,_,_,},
//   { _,_,0,_,_,1,_,_,},
//   { 0,_,1,_,_,2,_,_,},
//   { _,0,1,_,_,2,_,_,},
//   { 0,1,2,_,_,3,_,_,},
//   { _,_,_,0,_,1,_,_,},
//   { 0,_,_,1,_,2,_,_,},
//   { _,0,_,1,_,2,_,_,},
//   { 0,1,_,2,_,3,_,_,},
//   { _,_,0,1,_,2,_,_,},
//   { 0,_,1,2,_,3,_,_,},
//   { _,0,1,2,_,3,_,_,},
//   { 0,1,2,3,_,4,_,_,},
//   { _,_,_,_,0,1,_,_,},
//   { 0,_,_,_,1,2,_,_,},
//   { _,0,_,_,1,2,_,_,},
//   { 0,1,_,_,2,3,_,_,},
//   { _,_,0,_,1,2,_,_,},
//   { 0,_,1,_,2,3,_,_,},
//   { _,0,1,_,2,3,_,_,},
//   { 0,1,2,_,3,4,_,_,},
//   { _,_,_,0,1,2,_,_,},
//   { 0,_,_,1,2,3,_,_,},
//   { _,0,_,1,2,3,_,_,},
//   { 0,1,_,2,3,4,_,_,},
//   { _,_,0,1,2,3,_,_,},
//   { 0,_,1,2,3,4,_,_,},
//   { _,0,1,2,3,4,_,_,},
//   { 0,1,2,3,4,5,_,_,},
//   { _,_,_,_,_,_,0,_,},
//   { 0,_,_,_,_,_,1,_,},
//   { _,0,_,_,_,_,1,_,},
//   { 0,1,_,_,_,_,2,_,},
//   { _,_,0,_,_,_,1,_,},
//   { 0,_,1,_,_,_,2,_,},
//   { _,0,1,_,_,_,2,_,},
//   { 0,1,2,_,_,_,3,_,},
//   { _,_,_,0,_,_,1,_,},
//   { 0,_,_,1,_,_,2,_,},
//   { _,0,_,1,_,_,2,_,},
//   { 0,1,_,2,_,_,3,_,},
//   { _,_,0,1,_,_,2,_,},
//   { 0,_,1,2,_,_,3,_,},
//   { _,0,1,2,_,_,3,_,},
//   { 0,1,2,3,_,_,4,_,},
//   { _,_,_,_,0,_,1,_,},
//   { 0,_,_,_,1,_,2,_,},
//   { _,0,_,_,1,_,2,_,},
//   { 0,1,_,_,2,_,3,_,},
//   { _,_,0,_,1,_,2,_,},
//   { 0,_,1,_,2,_,3,_,},
//   { _,0,1,_,2,_,3,_,},
//   { 0,1,2,_,3,_,4,_,},
//   { _,_,_,0,1,_,2,_,},
//   { 0,_,_,1,2,_,3,_,},
//   { _,0,_,1,2,_,3,_,},
//   { 0,1,_,2,3,_,4,_,},
//   { _,_,0,1,2,_,3,_,},
//   { 0,_,1,2,3,_,4,_,},
//   { _,0,1,2,3,_,4,_,},
//   { 0,1,2,3,4,_,5,_,},
//   { _,_,_,_,_,0,1,_,},
//   { 0,_,_,_,_,1,2,_,},
//   { _,0,_,_,_,1,2,_,},
//   { 0,1,_,_,_,2,3,_,},
//   { _,_,0,_,_,1,2,_,},
//   { 0,_,1,_,_,2,3,_,},
//   { _,0,1,_,_,2,3,_,},
//   { 0,1,2,_,_,3,4,_,},
//   { _,_,_,0,_,1,2,_,},
//   { 0,_,_,1,_,2,3,_,},
//   { _,0,_,1,_,2,3,_,},
//   { 0,1,_,2,_,3,4,_,},
//   { _,_,0,1,_,2,3,_,},
//   { 0,_,1,2,_,3,4,_,},
//   { _,0,1,2,_,3,4,_,},
//   { 0,1,2,3,_,4,5,_,},
//   { _,_,_,_,0,1,2,_,},
//   { 0,_,_,_,1,2,3,_,},
//   { _,0,_,_,1,2,3,_,},
//   { 0,1,_,_,2,3,4,_,},
//   { _,_,0,_,1,2,3,_,},
//   { 0,_,1,_,2,3,4,_,},
//   { _,0,1,_,2,3,4,_,},
//   { 0,1,2,_,3,4,5,_,},
//   { _,_,_,0,1,2,3,_,},
//   { 0,_,_,1,2,3,4,_,},
//   { _,0,_,1,2,3,4,_,},
//   { 0,1,_,2,3,4,5,_,},
//   { _,_,0,1,2,3,4,_,},
//   { 0,_,1,2,3,4,5,_,},
//   { _,0,1,2,3,4,5,_,},
//   { 0,1,2,3,4,5,6,_,},
//   { _,_,_,_,_,_,_,0,},
//   { 0,_,_,_,_,_,_,1,},
//   { _,0,_,_,_,_,_,1,},
//   { 0,1,_,_,_,_,_,2,},
//   { _,_,0,_,_,_,_,1,},
//   { 0,_,1,_,_,_,_,2,},
//   { _,0,1,_,_,_,_,2,},
//   { 0,1,2,_,_,_,_,3,},
//   { _,_,_,0,_,_,_,1,},
//   { 0,_,_,1,_,_,_,2,},
//   { _,0,_,1,_,_,_,2,},
//   { 0,1,_,2,_,_,_,3,},
//   { _,_,0,1,_,_,_,2,},
//   { 0,_,1,2,_,_,_,3,},
//   { _,0,1,2,_,_,_,3,},
//   { 0,1,2,3,_,_,_,4,},
//   { _,_,_,_,0,_,_,1,},
//   { 0,_,_,_,1,_,_,2,},
//   { _,0,_,_,1,_,_,2,},
//   { 0,1,_,_,2,_,_,3,},
//   { _,_,0,_,1,_,_,2,},
//   { 0,_,1,_,2,_,_,3,},
//   { _,0,1,_,2,_,_,3,},
//   { 0,1,2,_,3,_,_,4,},
//   { _,_,_,0,1,_,_,2,},
//   { 0,_,_,1,2,_,_,3,},
//   { _,0,_,1,2,_,_,3,},
//   { 0,1,_,2,3,_,_,4,},
//   { _,_,0,1,2,_,_,3,},
//   { 0,_,1,2,3,_,_,4,},
//   { _,0,1,2,3,_,_,4,},
//   { 0,1,2,3,4,_,_,5,},
//   { _,_,_,_,_,0,_,1,},
//   { 0,_,_,_,_,1,_,2,},
//   { _,0,_,_,_,1,_,2,},
//   { 0,1,_,_,_,2,_,3,},
//   { _,_,0,_,_,1,_,2,},
//   { 0,_,1,_,_,2,_,3,},
//   { _,0,1,_,_,2,_,3,},
//   { 0,1,2,_,_,3,_,4,},
//   { _,_,_,0,_,1,_,2,},
//   { 0,_,_,1,_,2,_,3,},
//   { _,0,_,1,_,2,_,3,},
//   { 0,1,_,2,_,3,_,4,},
//   { _,_,0,1,_,2,_,3,},
//   { 0,_,1,2,_,3,_,4,},
//   { _,0,1,2,_,3,_,4,},
//   { 0,1,2,3,_,4,_,5,},
//   { _,_,_,_,0,1,_,2,},
//   { 0,_,_,_,1,2,_,3,},
//   { _,0,_,_,1,2,_,3,},
//   { 0,1,_,_,2,3,_,4,},
//   { _,_,0,_,1,2,_,3,},
//   { 0,_,1,_,2,3,_,4,},
//   { _,0,1,_,2,3,_,4,},
//   { 0,1,2,_,3,4,_,5,},
//   { _,_,_,0,1,2,_,3,},
//   { 0,_,_,1,2,3,_,4,},
//   { _,0,_,1,2,3,_,4,},
//   { 0,1,_,2,3,4,_,5,},
//   { _,_,0,1,2,3,_,4,},
//   { 0,_,1,2,3,4,_,5,},
//   { _,0,1,2,3,4,_,5,},
//   { 0,1,2,3,4,5,_,6,},
//   { _,_,_,_,_,_,0,1,},
//   { 0,_,_,_,_,_,1,2,},
//   { _,0,_,_,_,_,1,2,},
//   { 0,1,_,_,_,_,2,3,},
//   { _,_,0,_,_,_,1,2,},
//   { 0,_,1,_,_,_,2,3,},
//   { _,0,1,_,_,_,2,3,},
//   { 0,1,2,_,_,_,3,4,},
//   { _,_,_,0,_,_,1,2,},
//   { 0,_,_,1,_,_,2,3,},
//   { _,0,_,1,_,_,2,3,},
//   { 0,1,_,2,_,_,3,4,},
//   { _,_,0,1,_,_,2,3,},
//   { 0,_,1,2,_,_,3,4,},
//   { _,0,1,2,_,_,3,4,},
//   { 0,1,2,3,_,_,4,5,},
//   { _,_,_,_,0,_,1,2,},
//   { 0,_,_,_,1,_,2,3,},
//   { _,0,_,_,1,_,2,3,},
//   { 0,1,_,_,2,_,3,4,},
//   { _,_,0,_,1,_,2,3,},
//   { 0,_,1,_,2,_,3,4,},
//   { _,0,1,_,2,_,3,4,},
//   { 0,1,2,_,3,_,4,5,},
//   { _,_,_,0,1,_,2,3,},
//   { 0,_,_,1,2,_,3,4,},
//   { _,0,_,1,2,_,3,4,},
//   { 0,1,_,2,3,_,4,5,},
//   { _,_,0,1,2,_,3,4,},
//   { 0,_,1,2,3,_,4,5,},
//   { _,0,1,2,3,_,4,5,},
//   { 0,1,2,3,4,_,5,6,},
//   { _,_,_,_,_,0,1,2,},
//   { 0,_,_,_,_,1,2,3,},
//   { _,0,_,_,_,1,2,3,},
//   { 0,1,_,_,_,2,3,4,},
//   { _,_,0,_,_,1,2,3,},
//   { 0,_,1,_,_,2,3,4,},
//   { _,0,1,_,_,2,3,4,},
//   { 0,1,2,_,_,3,4,5,},
//   { _,_,_,0,_,1,2,3,},
//   { 0,_,_,1,_,2,3,4,},
//   { _,0,_,1,_,2,3,4,},
//   { 0,1,_,2,_,3,4,5,},
//   { _,_,0,1,_,2,3,4,},
//   { 0,_,1,2,_,3,4,5,},
//   { _,0,1,2,_,3,4,5,},
//   { 0,1,2,3,_,4,5,6,},
//   { _,_,_,_,0,1,2,3,},
//   { 0,_,_,_,1,2,3,4,},
//   { _,0,_,_,1,2,3,4,},
//   { 0,1,_,_,2,3,4,5,},
//   { _,_,0,_,1,2,3,4,},
//   { 0,_,1,_,2,3,4,5,},
//   { _,0,1,_,2,3,4,5,},
//   { 0,1,2,_,3,4,5,6,},
//   { _,_,_,0,1,2,3,4,},
//   { 0,_,_,1,2,3,4,5,},
//   { _,0,_,1,2,3,4,5,},
//   { 0,1,_,2,3,4,5,6,},
//   { _,_,0,1,2,3,4,5,},
//   { 0,_,1,2,3,4,5,6,},
//   { _,0,1,2,3,4,5,6,},
//   { 0,1,2,3,4,5,6,7,},
};
// clang-format on

// Simulated gather.  This is sometimes faster as it can run on other ports.
static ZL_MAYBE_UNUSED_FUNCTION inline __m256i
_mm256_i32gather_epi32x(void const* bv, __m256i idx, int size)
{
    ZL_ALIGNED(32) int c[8];
    _mm256_store_si256((__m256i*)c, idx);
    if (size == 4) {
        int const* b = (int const*)bv;
        return _mm256_set_epi32(
                b[c[7]],
                b[c[6]],
                b[c[5]],
                b[c[4]],
                b[c[3]],
                b[c[2]],
                b[c[1]],
                b[c[0]]);
    } else if (size == 2) {
        int16_t const* b = (int16_t const*)bv;
        return _mm256_set_epi32(
                b[c[7]],
                b[c[6]],
                b[c[5]],
                b[c[4]],
                b[c[3]],
                b[c[2]],
                b[c[1]],
                b[c[0]]);
    } else if (size == 8) {
        int64_t const* b = (int64_t const*)bv;
        return _mm256_set_epi32(
                (int)b[c[7]],
                (int)b[c[6]],
                (int)b[c[5]],
                (int)b[c[4]],
                (int)b[c[3]],
                (int)b[c[2]],
                (int)b[c[1]],
                (int)b[c[0]]);
    } else {
        int8_t const* b = (int8_t const*)bv;
        return _mm256_set_epi32(
                b[c[7]],
                b[c[6]],
                b[c[5]],
                b[c[4]],
                b[c[3]],
                b[c[2]],
                b[c[1]],
                b[c[0]]);
    }
}
#    if 1
#        define LZ44_mm256_i32gather_epi32 _mm256_i32gather_epi32
#    else
#        define LZ44_mm256_i32gather_epi32 _mm256_i32gather_epi32x
#    endif

static ZL_MAYBE_UNUSED_FUNCTION void dpr(char const* name, __m256i const vec32)
{
    size_t n = 8;
    uint32_t data[8];
    _mm256_storeu_si256((__m256i*)(void*)data, vec32);
    fprintf(stderr, "%s = [", name);
    for (size_t i = 0; i < n; ++i) {
        fprintf(stderr, "%x", data[i]);
        if (i + 1 < n) {
            fprintf(stderr, ", ");
        }
    }
    fprintf(stderr, "]\n");
}

#    define _mm256_slli_si256x(x, shift)                                      \
        _mm256_alignr_epi8(                                                   \
                (x),                                                          \
                _mm256_permute2x128_si256((x), (x), _MM_SHUFFLE(0, 0, 3, 0)), \
                16 - (shift))

#    if 0
static inline __m256i prefixSum(__m256i x) {
    // dpr("reload", reloadV);
    // dpr("perms0", _mm256_slli_si256x(permV, 4));
    x = _mm256_add_epi32(x, _mm256_slli_si256x(x, 4));
    // dpr("perm 1", permV);
    // dpr("perms1", _mm256_slli_si256x(permV, 8));
    x = _mm256_add_epi32(x, _mm256_slli_si256x(x, 8));
    // dpr("perm 2", permV);
    x = _mm256_add_epi32(x, _mm256_slli_si256x(x, 16));
    return x;
    // dpr("perm 3", permV);
}
#    else
ZL_FORCE_INLINE __m256i prefixSum(__m256i x)
{
    x         = _mm256_add_epi32(x, _mm256_slli_si256(x, 4));
    x         = _mm256_add_epi32(x, _mm256_slli_si256(x, 8));
    __m256i y = _mm256_permute2x128_si256(x, x, 0x08);
    y         = _mm256_shuffle_epi32(y, 0xFF);
    x         = _mm256_add_epi32(x, y);
    return x;
}
#    endif

ZL_FORCE_INLINE __m256i getPermute(__m256i reloadV, int reloadM)
{
    (void)permute;
    (void)permute0;
    (void)permute1;
#    if 1
    (void)reloadV;
    // dpr("reload", reloadV);
    __m256i const permV =
            _mm256_load_si256((__m256i const*)(void*)permute[reloadM]);
    // dpr("perm  ", permV);
    return permV;
#    elif 1
    __m256i const permV0 =
            _mm256_load_si256((__m256i const*)permute0[reloadM & 0xF]);
    __m256i const permV1 =
            _mm256_load_si256((__m256i const*)permute1[reloadM >> 4]);
    return _mm256_add_epi32(permV0, permV1);
#    elif 0
    // Really close to tying the 2x LUT.
    (void)reloadM;
    __m256i permV = prefixSum(reloadV);
    permV         = _mm256_add_epi32(permV, _mm256_set1_epi32(8));
    // dpr("perm 4", permV);
    return permV;
    // permV = _mm256_and_si256(permV, reloadV);
    // dpr("perm 5", permV);
#    else
    uint64_t sum = _pdep_u64(reloadM, 0x0101010101010101ULL);
    sum += (sum << 8);
    sum += (sum << 16);
    sum += (sum << 32);
    sum = 0x0808080808080808ULL - sum;
    return _mm256_cvtepu8_epi32(_mm_set1_epi64x(sum));
#    endif
}

#endif // ZL_HAS_AVX2

typedef struct {
    uint8_t nbBits;
    uint8_t byte;
} HUF_DEltX1;

static size_t readDTable(HUF_DTable* dtable, void const* src, size_t srcSize)
{
    // HUF_CREATE_STATIC_DTABLEX1(tmp, kMaxHufLog);
    size_t const ret = HUF_readDTableX1(dtable, src, srcSize);
    ZS_HUF_RET_IF_NOT(!HUF_isError(ret));
    // memcpy(dtable, tmp, 4);
    // HUF_DEltX1 const* from = (HUF_DEltX1 const*)(tmp + 1);
    // HUF_DEltX1* to = (HUF_DEltX1*)(dtable + 1);
    // size_t const tableLog = ((uint16_t const*)from)[-1] & 0xFF;
    // size_t const tableSize = 1u << tableLog;
    // size_t const tableMask = tableSize - 1;

    // for (size_t i = 0; i < tableSize; ++i) {
    //     HUF_DEltX1 const elt = from[i];
    //     size_t d = ((i >> (tableLog - elt.nbBits)) | (i << elt.nbBits)) &
    //     tableMask; to[d] = elt;
    // }

    return ret;
}

size_t ZS_HufAvx2_decode(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize)
{
    uint8_t const* istart = (uint8_t const*)src;
    uint8_t const* ip     = istart;
    uint8_t const* iend   = istart + srcSize;
    uint32_t dstSize;
    ZS_HUF_RET_IF_NOT(srcSize >= 5);
    memcpy(&dstSize, ip, sizeof(dstSize));
    ip += sizeof(dstSize);
    ZS_HUF_RET_IF_NOT(dstCapacity >= dstSize);
    {
        uint8_t const hdr = *ip++;
        if (hdr == 0) {
            ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= dstSize);
            memcpy(dst, ip, dstSize);
            return dstSize;
        }
        if (hdr == 1) {
            ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= 1);
            memset(dst, *ip, dstSize);
            return dstSize;
        }
        ZS_HUF_RET_IF_NOT(hdr == 2);
    }
    HUF_CREATE_STATIC_DTABLEX1(dtable_, kMaxHufLog);
    size_t const dTableSize = readDTable(dtable_, ip, (size_t)(iend - ip));
    ZS_HUF_RET_IF_NOT(dTableSize != 0);
    ip += dTableSize;

    uint32_t csize;
    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= sizeof(csize));
    memcpy(&csize, ip, sizeof(csize));
    ip += sizeof(csize);

    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= (size_t)csize);

    uint8_t const* bend   = ip;
    uint8_t const* bstart = ip + csize;
    uint8_t const* bs     = bstart;
    ip                    = bstart;

    ZL_ALIGNED(32) uint32_t state[kNumStates];
    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= sizeof(state));
    memcpy(state, ip, sizeof(state));
    ip += sizeof(state);
    ZL_ALIGNED(32) uint32_t reload[kNumStates];
    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= sizeof(reload));
    memcpy(reload, ip, sizeof(reload));
    ip += sizeof(reload);
    ZL_ALIGNED(32) uint32_t bits[kNumStates];
    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= kNumStates);
    for (size_t i = 0; i < kNumStates; ++i) {
        // bits[i] is the number of bits consumed. It is invalid to consume 32
        // or more bits.
        bits[i] = (uint32_t)(32 - *ip++) & 31;
        state[i] <<= bits[i];
        // assert(bits[i] >= 16);
    }
    uint8_t const* blimit;
    {
        uint32_t maxReload = 0;
        for (size_t i = 0; i < kNumStates; ++i) {
            // reload[i] contains the earliest position where stream i is
            // reloaded from the bitstream. It must point into the bitstream.
            ZS_HUF_RET_IF_NOT(reload[i] <= csize);
            if (reload[i] > maxReload) {
                maxReload = reload[i];
            }
        }
        // fprintf(stderr, "max reload = %u\n", maxReload);
        blimit = bend + maxReload + 64;
    }

    uint8_t* op   = (uint8_t*)dst;
    uint8_t* oend = op + dstSize;
    // ZL_REQUIRE(dstSize + 32 <= dstCapacity); // Buffer room

    void const* const dtablev    = (void const*)(dtable_ + 1);
    uint16_t const* const dtable = (uint16_t const*)dtablev;
    size_t tableLog              = dtable[-1] & 0xFF;

    (void)blimit;
#if ZL_HAS_AVX2

    __m256i const tableShiftV = _mm256_set1_epi32(32 - (int)tableLog);
    __m256i const byteMaskV   = _mm256_set1_epi32(0xFF);
    __m256i const thresholdV  = _mm256_set1_epi32(16);

    // __m256i stateV1 = _mm256_cvtepu16_epi32(_mm_load_si128((__m128i
    // *)&state[0]));
    // __m256i stateV2 = _mm256_cvtepu16_epi32(_mm_load_si128((__m128i
    // *)&state[8]));
    // __m256i stateV3 = _mm256_cvtepu16_epi32(_mm_load_si128((__m128i
    // *)&state[16]));
    // __m256i stateV4 = _mm256_cvtepu16_epi32(_mm_load_si128((__m128i
    // *)&state[24]));
    __m256i stateV1 = _mm256_load_si256((__m256i const*)(void const*)&state[0]);
    __m256i stateV2 = _mm256_load_si256((__m256i const*)(void const*)&state[8]);
    __m256i entryV1 = LZ44_mm256_i32gather_epi32(
            (int const*)dtablev, _mm256_srlv_epi32(stateV1, tableShiftV), 2);
    __m256i entryV2 = LZ44_mm256_i32gather_epi32(
            (int const*)dtablev, _mm256_srlv_epi32(stateV2, tableShiftV), 2);
    __m256i bitsV1 = _mm256_load_si256((__m256i const*)(void const*)&bits[0]);
    __m256i bitsV2 = _mm256_load_si256((__m256i const*)(void const*)&bits[8]);
#    if kNumStates == 32
    __m256i stateV3 =
            _mm256_load_si256((__m256i const*)(void const*)&state[16]);
    __m256i stateV4 =
            _mm256_load_si256((__m256i const*)(void const*)&state[24]);
    __m256i entryV3 = LZ44_mm256_i32gather_epi32(
            (int const*)dtablev, _mm256_srlv_epi32(stateV3, tableShiftV), 2);
    __m256i entryV4 = LZ44_mm256_i32gather_epi32(
            (int const*)dtablev, _mm256_srlv_epi32(stateV4, tableShiftV), 2);
    __m256i bitsV3 = _mm256_load_si256((__m256i const*)(void const*)&bits[16]);
    __m256i bitsV4 = _mm256_load_si256((__m256i const*)(void const*)&bits[24]);
#    endif
// __m256i bitsV1 = _mm256_set1_epi32(32);
// __m256i bitsV2 = _mm256_set1_epi32(32);
// __m256i bitsV3 = _mm256_set1_epi32(32);
// __m256i bitsV4 = _mm256_set1_epi32(32);

// TODO: Could do 64-bit accumulators - guarantee 32-bits present at a time

// TODO: Buffer overflow input & output...
#    if 0
            uint32_t const shifted = state[i] >> (32 - tableLog);
            uint16_t const entry = dtable[shifted];
            uint8_t const byte = entry & 0xFF;
            uint8_t const nbits = entry >> 8;
            // fprintf(stderr, "%zu (%x) byte=%u nbits=%u\n", i, masked, (unsigned)byte, (unsigned)bit);
            *op++ = byte;
            state[i] <<= nbits;
            assert(nbits <= tableLog);
            // assert(bit <= bits[i]);
            bits[i] += nbits;
            if (bits[i] > 16 && bs - 1 > bend + reload[i]) {
                // fprintf(stderr, "%zu read %zu: %u -> %u\n", op - (uint8_t*)dst, i, bits[i], bits[i] + 16);
                bits[i] -= 16;
                bs -= 2;
                state[i] |= ((uint32_t)ZL_readLE16(bs)) << bits[i];
            }
#    endif
    bs -= 16;
#    ifdef LLVM_MCA
    __asm volatile("# LLVM-MCA-BEGIN huf");
#    endif
    for (; op < oend - (kNumStates - 1) && bs > blimit;) {
        __m256i dataV1 = _mm256_cvtepu16_epi32(
                _mm_loadu_si128((__m128i const*)(void const*)bs));
        // /// uint32_t shifted = state >> (32 - tableLog);
        // __m256i const shiftedV1 = _mm256_srlv_epi32(stateV1, tableShiftV);
        // __m256i const shiftedV2 = _mm256_srlv_epi32(stateV2, tableShiftV);

        // /// uint16_t entry = dtable[shifted];
        // __m256i const entryV1 = _mm256_i32gather_epi32(dtable, shiftedV1, 2);
        // __m256i const entryV2 = _mm256_i32gather_epi32(dtable, shiftedV2, 2);

        /// uint8_t byte = entry & 0xFF;
        __m256i const byteV1 =
                _mm256_and_si256(_mm256_srli_epi32(entryV1, 8), byteMaskV);
        __m256i const byteV2 =
                _mm256_and_si256(_mm256_srli_epi32(entryV2, 8), byteMaskV);

        // Pack bytes into the lowest 64-bits of each vector
        __m256i byteV = _mm256_packus_epi32(byteV1, byteV2);
        byteV         = _mm256_permute4x64_epi64(byteV, 0xd8);
        byteV         = _mm256_packus_epi16(byteV, byteV);
        ZL_writeLE64(op, (uint64_t)_mm256_extract_epi64(byteV, 0));
        op += 8;
        ZL_writeLE64(op, (uint64_t)_mm256_extract_epi64(byteV, 2));
        op += 8;

        /// uint8_t nbits = (entry >> 8) & 0xFF;
        __m256i const nbitsV1 = _mm256_and_si256(entryV1, byteMaskV);

        // state <<= nbits;
        stateV1                = _mm256_sllv_epi32(stateV1, nbitsV1);
        bitsV1                 = _mm256_add_epi32(bitsV1, nbitsV1);
        __m256i const reloadV1 = _mm256_cmpgt_epi32(bitsV1, thresholdV);
        int const reloadM1 = _mm256_movemask_ps(_mm256_castsi256_ps(reloadV1));
        __m256i const permV1 = getPermute(reloadV1, reloadM1);
        bitsV1               = _mm256_sub_epi32(
                bitsV1, _mm256_and_si256(thresholdV, reloadV1));

        /// if (bits > 16) state |= ZL_readLE16(bs) << bits
        dataV1 = _mm256_permutevar8x32_epi32(dataV1, permV1);
        __m256i const nextV1 =
                _mm256_or_si256(_mm256_sllv_epi32(dataV1, bitsV1), stateV1);
        stateV1 = _mm256_blendv_epi8(stateV1, nextV1, reloadV1);
        /// if (bits > 16) bs -= 2
        entryV1 = LZ44_mm256_i32gather_epi32(
                (int const*)dtablev,
                _mm256_srlv_epi32(stateV1, tableShiftV),
                2);
        bs -= 2 * _mm_popcnt_u32((unsigned)reloadM1);
        __m256i dataV2 = _mm256_cvtepu16_epi32(
                _mm_loadu_si128((__m128i const*)(void const*)bs));
        /// *op++ = byte

        __m256i const nbitsV2 = _mm256_and_si256(entryV2, byteMaskV);
        stateV2               = _mm256_sllv_epi32(stateV2, nbitsV2);

        // bits += nbits;
        bitsV2 = _mm256_add_epi32(bitsV2, nbitsV2);

        /// if (bits > 16)
        __m256i const reloadV2 = _mm256_cmpgt_epi32(bitsV2, thresholdV);

        /// if (bits > 16)
        int const reloadM2 = _mm256_movemask_ps(_mm256_castsi256_ps(reloadV2));
        __m256i const permV2 = getPermute(reloadV2, reloadM2);

        /// if (bits > 16) bits -= 16
        bitsV2 = _mm256_sub_epi32(
                bitsV2, _mm256_and_si256(thresholdV, reloadV2));

        /// if (bits > 16) state |= ZL_readLE16(bs) << bits
        dataV2 = _mm256_permutevar8x32_epi32(dataV2, permV2);
        __m256i const nextV2 =
                _mm256_or_si256(_mm256_sllv_epi32(dataV2, bitsV2), stateV2);
        stateV2 = _mm256_blendv_epi8(stateV2, nextV2, reloadV2);
        /// if (bits > 16) bs -= 2
        entryV2 = LZ44_mm256_i32gather_epi32(
                (int const*)dtablev,
                _mm256_srlv_epi32(stateV2, tableShiftV),
                2);
        bs -= 2 * _mm_popcnt_u32((unsigned)reloadM2);
        __m256i dataV3 = _mm256_cvtepu16_epi32(
                _mm_loadu_si128((__m128i const*)(void const*)bs));

#    if kNumStates == 32
        /// uint32_t shifted = state >> (32 - tableLog);
        // __m256i const shiftedV3 = _mm256_srlv_epi32(stateV3, tableShiftV);
        // __m256i const shiftedV4 = _mm256_srlv_epi32(stateV4, tableShiftV);

        /// uint16_t entry = dtable[shifted];
        // __m256i const entryV3 = _mm256_i32gather_epi32(dtable, shiftedV3, 2);

        /// uint16_t entry = dtable[shifted];
        // __m256i const entryV4 = _mm256_i32gather_epi32(dtable, shiftedV4, 2);

        /// uint8_t byte = (entry >> 8) & 0xFF;
        __m256i const byteV3 =
                _mm256_and_si256(_mm256_srli_epi32(entryV3, 8), byteMaskV);
        __m256i const byteV4 =
                _mm256_and_si256(_mm256_srli_epi32(entryV4, 8), byteMaskV);

        /// uint8_t nbits = entry & 0xFF;
        __m256i const nbitsV3 = _mm256_and_si256(entryV3, byteMaskV);
        __m256i const nbitsV4 = _mm256_and_si256(entryV4, byteMaskV);

        // Pack bytes into the lowest 64-bits of each vector
        byteV = _mm256_packus_epi32(byteV3, byteV4);
        byteV = _mm256_permute4x64_epi64(byteV, 0xd8);
        byteV = _mm256_packus_epi16(byteV, byteV);
        ZL_writeLE64(op, (uint64_t)_mm256_extract_epi64(byteV, 0));
        op += 8;
        ZL_writeLE64(op, (uint64_t)_mm256_extract_epi64(byteV, 2));
        op += 8;

        // state <<= nbits;
        stateV3 = _mm256_sllv_epi32(stateV3, nbitsV3);
        bitsV3  = _mm256_add_epi32(bitsV3, nbitsV3);

        __m256i const reloadV3 = _mm256_cmpgt_epi32(bitsV3, thresholdV);
        int const reloadM3 = _mm256_movemask_ps(_mm256_castsi256_ps(reloadV3));
        __m256i const permV3 = getPermute(reloadV3, reloadM3);
        bitsV3               = _mm256_sub_epi32(
                bitsV3, _mm256_and_si256(thresholdV, reloadV3));

        /// if (bits > 16) state |= ZL_readLE16(bs) << bits
        dataV3 = _mm256_permutevar8x32_epi32(dataV3, permV3);
        __m256i const nextV3 =
                _mm256_or_si256(_mm256_sllv_epi32(dataV3, bitsV3), stateV3);
        stateV3 = _mm256_blendv_epi8(stateV3, nextV3, reloadV3);
        /// if (bits > 16) bs -= 2
        entryV3 = LZ44_mm256_i32gather_epi32(
                (int const*)dtablev,
                _mm256_srlv_epi32(stateV3, tableShiftV),
                2);
        bs -= 2 * _mm_popcnt_u32((unsigned)reloadM3);
        __m256i dataV4 = _mm256_cvtepu16_epi32(
                _mm_loadu_si128((__m128i const*)(void const*)bs));

        stateV4 = _mm256_sllv_epi32(stateV4, nbitsV4);

        // bits += nbits;
        bitsV4 = _mm256_add_epi32(bitsV4, nbitsV4);

        /// if (bits > 16)
        __m256i const reloadV4 = _mm256_cmpgt_epi32(bitsV4, thresholdV);

        /// if (bits > 16)
        int const reloadM4 = _mm256_movemask_ps(_mm256_castsi256_ps(reloadV4));
        __m256i const permV4 = getPermute(reloadV4, reloadM4);

        /// if (bits > 16) bits -= 16
        bitsV4 = _mm256_sub_epi32(
                bitsV4, _mm256_and_si256(thresholdV, reloadV4));

        /// if (bits > 16) state |= ZL_readLE16(bs) << bits
        dataV4 = _mm256_permutevar8x32_epi32(dataV4, permV4);
        __m256i const nextV4 =
                _mm256_or_si256(_mm256_sllv_epi32(dataV4, bitsV4), stateV4);
        stateV4 = _mm256_blendv_epi8(stateV4, nextV4, reloadV4);
        /// if (bits > 16) bs -= 2
        entryV4 = LZ44_mm256_i32gather_epi32(
                (int const*)dtablev,
                _mm256_srlv_epi32(stateV4, tableShiftV),
                2);
        bs -= 2 * _mm_popcnt_u32((unsigned)reloadM4);

        /// *op++ = byte
#    endif
    }
    bs += 16;
#    ifdef LLVM_MCA
    __asm volatile("# LLVM-MCA-END huf");
#    endif

    _mm256_store_si256((__m256i*)(void*)&state[0], stateV1);
    _mm256_store_si256((__m256i*)(void*)&state[8], stateV2);
    _mm256_store_si256((__m256i*)(void*)&bits[0], bitsV1);
    _mm256_store_si256((__m256i*)(void*)&bits[8], bitsV2);
#    if kNumStates == 32
    _mm256_store_si256((__m256i*)(void*)&state[16], stateV3);
    _mm256_store_si256((__m256i*)(void*)&state[24], stateV4);
    _mm256_store_si256((__m256i*)(void*)&bits[16], bitsV3);
    _mm256_store_si256((__m256i*)(void*)&bits[24], bitsV4);
#    endif

#endif // ZL_HAS_AVX2

    // uint32_t bits[32] ZL_ALIGNED(32);
    // _mm256_store_si256((__m256i*)&bits[0], bitsV1);
    // _mm256_store_si256((__m256i*)&bits[8], bitsV2);
    // _mm256_store_si256((__m256i*)&bits[16], bitsV3);
    // _mm256_store_si256((__m256i*)&bits[24], bitsV4);
    // fprintf(stderr, "tlog = %zu\n", tableLog);
    assert(op <= oend);
    for (size_t i = 0; i < kNumStates; ++i) {
        assert(bs >= bend + reload[i]);
        assert(bits[i] <= 32);
        // fprintf(stderr, "d[%zu] = %x\n", i, state[i]);
    }
    // fprintf(stderr, "remaining = %u\n", (unsigned)(oend - op));
    // fprintf(stderr, "bs ?= bend %p %p\n", bs, bend);
    for (; op < oend;) {
        for (size_t i = 0; i < kNumStates && op < oend; ++i) {
            // fprintf(stderr, "%u\n", state[i]);
            uint32_t const shifted = state[i] >> (32 - tableLog);
            uint16_t const entry   = dtable[shifted];
            uint8_t const byte     = (uint8_t)(entry >> 8);
            uint8_t const nbits    = (uint8_t)(entry & 0xFF);
            // fprintf(stderr, "%zu (%x) byte=%u nbits=%u\n", i, masked,
            // (unsigned)byte, (unsigned)bit);
            *op++ = byte;
            state[i] <<= nbits;
            assert(nbits <= tableLog);
            // assert(bit <= bits[i]);
            bits[i] += nbits;
            if (bits[i] > 16 && bs - 1 > bend + reload[i]) {
                // fprintf(stderr, "%zu read %zu: %u -> %u\n", op -
                // (uint8_t*)dst, i, bits[i], bits[i] + 16);
                bits[i] -= 16;
                bs -= 2;
                state[i] |= ((uint32_t)ZL_readLE16(bs)) << bits[i];
            }
        }
    }
    // fprintf(stderr, "bs ?= bend %p %p\n", bs, bend);
    ZS_HUF_RET_IF_NOT(op == oend);
    ZS_HUF_RET_IF_NOT(bs == bend);
    ZS_HUF_RET_IF_NOT(ip == iend);
    for (size_t i = 0; i < kNumStates; ++i) {
        // fprintf(stderr, "bits[%zu] = %u\n", i, bits[i]);
        ZS_HUF_RET_IF_NOT(bits[i] == 32);
    }

    return dstSize;
}

typedef uint16_t HUF_DElt16;

typedef struct {
    int tableLog;
    HUF_DElt16 table[(size_t)1 << kMaxHuf16Log];
} HUF_DTable16;

size_t ZS_Huf16Avx2_decode(
        void* dst,
        size_t dstCapacity,
        void const* src,
        size_t srcSize)
{
    uint8_t const* istart = (uint8_t const*)src;
    uint8_t const* ip     = istart;
    uint8_t const* iend   = istart + srcSize;
    uint32_t dstSize;
    ZS_HUF_RET_IF_NOT(srcSize >= 5);
    memcpy(&dstSize, ip, sizeof(dstSize));
    ip += sizeof(dstSize);
    ZS_HUF_RET_IF_NOT(dstCapacity >= dstSize);
    {
        uint8_t const hdr = *ip++;
        if (hdr == 0) {
            ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= dstSize);
            memcpy(dst, ip, dstSize);
            return dstSize;
        }
        if (hdr == 1) {
            ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= 2);
            uint16_t val        = ZL_readLE16(ip);
            uint8_t* op         = (uint8_t*)dst;
            uint8_t* const oend = op + 2 * dstSize;
            for (; op < oend; op += 2) {
                ZL_write16(op, val);
            }
            return dstSize;
        }
        ZS_HUF_RET_IF_NOT(hdr == 2);
    }
    HUF_DTable16 dtable_;
    {
        ZL_RC rc     = ZL_RC_wrap(ip, (size_t)(iend - ip));
        int tableLog = kMaxHuf16Log;
        ZS_Huf16DElt* const dtable =
                ZS_largeHuffmanCreateDTable(&rc, &tableLog);
        ZS_HUF_RET_IF_NOT(dtable != NULL);
        if (tableLog > kMaxHuf16Log) {
            free(dtable);
            return 0;
        }
        size_t const tableSize = (size_t)1 << tableLog;
        dtable_.tableLog       = tableLog;
        for (size_t i = 0; i < tableSize; ++i) {
            ZS_Huf16DElt const in = dtable[i];
            if (in.symbol >= ((size_t)1 << 12)) {
                free(dtable);
                return 0;
            }
            uint16_t const out = (uint16_t)(in.symbol | (in.nbBits << 12));
            dtable_.table[i]   = out;
        }
        free(dtable);
        ip = ZL_RC_ptr(&rc);
    }

    uint32_t csize;
    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= sizeof(csize));
    memcpy(&csize, ip, sizeof(csize));
    ip += sizeof(csize);

    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= (size_t)csize);

    uint8_t const* bend   = ip;
    uint8_t const* bstart = ip + csize;
    uint8_t const* bs     = bstart;
    ip                    = bstart;

    ZL_ALIGNED(32) uint32_t state[kNumStates];
    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= sizeof(state));
    memcpy(state, ip, sizeof(state));
    ip += sizeof(state);
    ZL_ALIGNED(32) uint32_t reload[kNumStates];
    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= sizeof(reload));
    memcpy(reload, ip, sizeof(reload));
    ip += sizeof(reload);
    ZL_ALIGNED(32) uint32_t bits[kNumStates];
    ZS_HUF_RET_IF_NOT((size_t)(iend - ip) >= kNumStates);
    for (size_t i = 0; i < kNumStates; ++i) {
        bits[i] = (uint32_t)(32 - *ip++);
        state[i] <<= bits[i];
        assert(bits[i] <= 16);
    }
    uint8_t const* blimit;
    {
        uint32_t maxReload = 0;
        for (size_t i = 0; i < kNumStates; ++i) {
            if (reload[i] > maxReload) {
                maxReload = reload[i];
            }
        }
        // fprintf(stderr, "max reload = %u\n", maxReload);
        blimit = bend + maxReload + 64;
    }

    uint8_t* op   = (uint8_t*)dst;
    uint8_t* oend = op + 2 * dstSize;
    ZS_HUF_RET_IF_NOT(dstSize <= dstCapacity);
    // ZL_REQUIRE(dstSize + 32 <= dstCapacity); // Buffer room

    void const* const dtablev    = (void const*)dtable_.table;
    uint16_t const* const dtable = (uint16_t const*)dtablev;
    int tableLog                 = dtable_.tableLog;

    (void)blimit;
#if ZL_HAS_AVX2

    __m256i const tableShiftV = _mm256_set1_epi32(32 - tableLog);
    __m256i const valueMaskV  = _mm256_set1_epi32(0xFFF);
    __m256i const nbBitsMaskV = _mm256_set1_epi32(0xF);
    __m256i const thresholdV  = _mm256_set1_epi32(16);

    // __m256i stateV1 = _mm256_cvtepu16_epi32(_mm_load_si128((__m128i
    // *)&state[0]));
    // __m256i stateV2 = _mm256_cvtepu16_epi32(_mm_load_si128((__m128i
    // *)&state[8]));
    // __m256i stateV3 = _mm256_cvtepu16_epi32(_mm_load_si128((__m128i
    // *)&state[16]));
    // __m256i stateV4 = _mm256_cvtepu16_epi32(_mm_load_si128((__m128i
    // *)&state[24]));
    __m256i stateV1 = _mm256_load_si256((__m256i const*)(void const*)&state[0]);
    __m256i stateV2 = _mm256_load_si256((__m256i const*)(void const*)&state[8]);
    __m256i entryV1 = LZ44_mm256_i32gather_epi32(
            (int const*)dtablev, _mm256_srlv_epi32(stateV1, tableShiftV), 2);
    __m256i entryV2 = LZ44_mm256_i32gather_epi32(
            (int const*)dtablev, _mm256_srlv_epi32(stateV2, tableShiftV), 2);
    __m256i bitsV1 = _mm256_load_si256((__m256i const*)(void const*)&bits[0]);
    __m256i bitsV2 = _mm256_load_si256((__m256i const*)(void const*)&bits[8]);
#    if kNumStates == 32
    __m256i stateV3 =
            _mm256_load_si256((__m256i const*)(void const*)&state[16]);
    __m256i stateV4 =
            _mm256_load_si256((__m256i const*)(void const*)&state[24]);
    __m256i entryV3 = LZ44_mm256_i32gather_epi32(
            (int const*)dtablev, _mm256_srlv_epi32(stateV3, tableShiftV), 2);
    __m256i entryV4 = LZ44_mm256_i32gather_epi32(
            (int const*)dtablev, _mm256_srlv_epi32(stateV4, tableShiftV), 2);
    __m256i bitsV3 = _mm256_load_si256((__m256i const*)(void const*)&bits[16]);
    __m256i bitsV4 = _mm256_load_si256((__m256i const*)(void const*)&bits[24]);
#    endif
// __m256i bitsV1 = _mm256_set1_epi32(32);
// __m256i bitsV2 = _mm256_set1_epi32(32);
// __m256i bitsV3 = _mm256_set1_epi32(32);
// __m256i bitsV4 = _mm256_set1_epi32(32);

// TODO: Could do 64-bit accumulators - guarantee 32-bits present at a time

// TODO: Buffer overflow input & output...
#    if 0
            uint32_t const shifted = state[i] >> (32 - tableLog);
            uint16_t const entry = dtable[shifted];
            uint8_t const byte = entry & 0xFF;
            uint8_t const nbits = entry >> 8;
            // fprintf(stderr, "%zu (%x) byte=%u nbits=%u\n", i, masked, (unsigned)byte, (unsigned)bit);
            *op++ = byte;
            state[i] <<= nbits;
            assert(nbits <= tableLog);
            // assert(bit <= bits[i]);
            bits[i] += nbits;
            if (bits[i] > 16 && bs - 1 > bend + reload[i]) {
                // fprintf(stderr, "%zu read %zu: %u -> %u\n", op - (uint8_t*)dst, i, bits[i], bits[i] + 16);
                bits[i] -= 16;
                bs -= 2;
                state[i] |= ((uint32_t)ZL_readLE16(bs)) << bits[i];
            }
#    endif
    bs -= 16;
#    ifdef LLVM_MCA
    __asm volatile("# LLVM-MCA-BEGIN huf");
#    endif
    for (; op < oend - 2 * (kNumStates - 1) && bs > blimit;) {
        __m256i dataV1 = _mm256_cvtepu16_epi32(
                _mm_loadu_si128((__m128i const*)(void const*)bs));
        // /// uint32_t shifted = state >> (32 - tableLog);
        // __m256i const shiftedV1 = _mm256_srlv_epi32(stateV1,
        // tableShiftV);
        // __m256i const shiftedV2 = _mm256_srlv_epi32(stateV2,
        // tableShiftV);

        // /// uint16_t entry = dtable[shifted];
        // __m256i const entryV1 = _mm256_i32gather_epi32(dtable, shiftedV1,
        // 2);
        // __m256i const entryV2 = _mm256_i32gather_epi32(dtable, shiftedV2,
        // 2);

        /// uint16_t value = entry & 0xFFF;
        __m256i const valueV1 = _mm256_and_si256(entryV1, valueMaskV);
        __m256i const valueV2 = _mm256_and_si256(entryV2, valueMaskV);

        // Pack bytes into the lowest 64-bits of each vector
        __m256i valueV = _mm256_packus_epi32(valueV1, valueV2);
        valueV         = _mm256_permute4x64_epi64(valueV, 0xd8);
        _mm256_storeu_si256((__m256i*)(void*)op, valueV);
        op += 32;

        /// uint8_t nbits = (entry >> 8) & 0xFF;
        __m256i const nbitsV1 =
                _mm256_and_si256(_mm256_srli_epi32(entryV1, 12), nbBitsMaskV);

        // state <<= nbits;
        stateV1                = _mm256_sllv_epi32(stateV1, nbitsV1);
        bitsV1                 = _mm256_add_epi32(bitsV1, nbitsV1);
        __m256i const reloadV1 = _mm256_cmpgt_epi32(bitsV1, thresholdV);
        int const reloadM1 = _mm256_movemask_ps(_mm256_castsi256_ps(reloadV1));
        __m256i const permV1 = getPermute(reloadV1, reloadM1);
        bitsV1               = _mm256_sub_epi32(
                bitsV1, _mm256_and_si256(thresholdV, reloadV1));

        /// if (bits > 16) state |= ZL_readLE16(bs) << bits
        dataV1 = _mm256_permutevar8x32_epi32(dataV1, permV1);
        __m256i const nextV1 =
                _mm256_or_si256(_mm256_sllv_epi32(dataV1, bitsV1), stateV1);
        stateV1 = _mm256_blendv_epi8(stateV1, nextV1, reloadV1);
        /// if (bits > 16) bs -= 2
        entryV1 = LZ44_mm256_i32gather_epi32(
                (int const*)dtablev,
                _mm256_srlv_epi32(stateV1, tableShiftV),
                2);
        bs -= 2 * _mm_popcnt_u32((unsigned)reloadM1);
        __m256i dataV2 = _mm256_cvtepu16_epi32(
                _mm_loadu_si128((__m128i const*)(void const*)bs));
        /// *op++ = byte

        __m256i const nbitsV2 =
                _mm256_and_si256(_mm256_srli_epi32(entryV2, 12), nbBitsMaskV);
        stateV2 = _mm256_sllv_epi32(stateV2, nbitsV2);

        // bits += nbits;
        bitsV2 = _mm256_add_epi32(bitsV2, nbitsV2);

        /// if (bits > 16)
        __m256i const reloadV2 = _mm256_cmpgt_epi32(bitsV2, thresholdV);

        /// if (bits > 16)
        int const reloadM2 = _mm256_movemask_ps(_mm256_castsi256_ps(reloadV2));
        __m256i const permV2 = getPermute(reloadV2, reloadM2);

        /// if (bits > 16) bits -= 16
        bitsV2 = _mm256_sub_epi32(
                bitsV2, _mm256_and_si256(thresholdV, reloadV2));

        /// if (bits > 16) state |= ZL_readLE16(bs) << bits
        dataV2 = _mm256_permutevar8x32_epi32(dataV2, permV2);
        __m256i const nextV2 =
                _mm256_or_si256(_mm256_sllv_epi32(dataV2, bitsV2), stateV2);
        stateV2 = _mm256_blendv_epi8(stateV2, nextV2, reloadV2);
        /// if (bits > 16) bs -= 2
        entryV2 = LZ44_mm256_i32gather_epi32(
                (int const*)dtablev,
                _mm256_srlv_epi32(stateV2, tableShiftV),
                2);
        bs -= 2 * _mm_popcnt_u32((unsigned)reloadM2);
        __m256i dataV3 = _mm256_cvtepu16_epi32(
                _mm_loadu_si128((__m128i const*)(void const*)bs));

#    if kNumStates == 32
        /// uint32_t shifted = state >> (32 - tableLog);
        // __m256i const shiftedV3 = _mm256_srlv_epi32(stateV3,
        // tableShiftV);
        // __m256i const shiftedV4 = _mm256_srlv_epi32(stateV4,
        // tableShiftV);

        /// uint16_t entry = dtable[shifted];
        // __m256i const entryV3 = _mm256_i32gather_epi32(dtable, shiftedV3,
        // 2);

        /// uint16_t entry = dtable[shifted];
        // __m256i const entryV4 = _mm256_i32gather_epi32(dtable, shiftedV4,
        // 2);

        /// uint8_t byte = entry & 0xFF;
        __m256i const valueV3 = _mm256_and_si256(entryV3, valueMaskV);
        __m256i const valueV4 = _mm256_and_si256(entryV4, valueMaskV);

        /// uint8_t nbits = (entry >> 8) & 0xFF;
        __m256i const nbitsV3 =
                _mm256_and_si256(_mm256_srli_epi32(entryV3, 12), nbBitsMaskV);
        __m256i const nbitsV4 =
                _mm256_and_si256(_mm256_srli_epi32(entryV4, 12), nbBitsMaskV);

        // Pack bytes into the lowest 64-bits of each vector
        valueV = _mm256_packus_epi32(valueV3, valueV4);
        valueV = _mm256_permute4x64_epi64(valueV, 0xd8);
        _mm256_storeu_si256((__m256i*)(void*)op, valueV);
        op += 32;

        // state <<= nbits;
        stateV3 = _mm256_sllv_epi32(stateV3, nbitsV3);
        bitsV3  = _mm256_add_epi32(bitsV3, nbitsV3);

        __m256i const reloadV3 = _mm256_cmpgt_epi32(bitsV3, thresholdV);
        int const reloadM3 = _mm256_movemask_ps(_mm256_castsi256_ps(reloadV3));
        __m256i const permV3 = getPermute(reloadV3, reloadM3);
        bitsV3               = _mm256_sub_epi32(
                bitsV3, _mm256_and_si256(thresholdV, reloadV3));

        /// if (bits > 16) state |= ZL_readLE16(bs) << bits
        dataV3 = _mm256_permutevar8x32_epi32(dataV3, permV3);
        __m256i const nextV3 =
                _mm256_or_si256(_mm256_sllv_epi32(dataV3, bitsV3), stateV3);
        stateV3 = _mm256_blendv_epi8(stateV3, nextV3, reloadV3);
        /// if (bits > 16) bs -= 2
        entryV3 = LZ44_mm256_i32gather_epi32(
                (int const*)dtablev,
                _mm256_srlv_epi32(stateV3, tableShiftV),
                2);
        bs -= 2 * _mm_popcnt_u32((unsigned)reloadM3);
        __m256i dataV4 = _mm256_cvtepu16_epi32(
                _mm_loadu_si128((__m128i const*)(void const*)bs));

        stateV4 = _mm256_sllv_epi32(stateV4, nbitsV4);

        // bits += nbits;
        bitsV4 = _mm256_add_epi32(bitsV4, nbitsV4);

        /// if (bits > 16)
        __m256i const reloadV4 = _mm256_cmpgt_epi32(bitsV4, thresholdV);

        /// if (bits > 16)
        int const reloadM4 = _mm256_movemask_ps(_mm256_castsi256_ps(reloadV4));
        __m256i const permV4 = getPermute(reloadV4, reloadM4);

        /// if (bits > 16) bits -= 16
        bitsV4 = _mm256_sub_epi32(
                bitsV4, _mm256_and_si256(thresholdV, reloadV4));

        /// if (bits > 16) state |= ZL_readLE16(bs) << bits
        dataV4 = _mm256_permutevar8x32_epi32(dataV4, permV4);
        __m256i const nextV4 =
                _mm256_or_si256(_mm256_sllv_epi32(dataV4, bitsV4), stateV4);
        stateV4 = _mm256_blendv_epi8(stateV4, nextV4, reloadV4);
        /// if (bits > 16) bs -= 2
        entryV4 = LZ44_mm256_i32gather_epi32(
                (int const*)dtablev,
                _mm256_srlv_epi32(stateV4, tableShiftV),
                2);
        bs -= 2 * _mm_popcnt_u32((unsigned)reloadM4);

        /// *op++ = byte
#    endif
    }
    bs += 16;
#    ifdef LLVM_MCA
    __asm volatile("# LLVM-MCA-END huf");
#    endif

    _mm256_store_si256((__m256i*)(void*)&state[0], stateV1);
    _mm256_store_si256((__m256i*)(void*)&state[8], stateV2);
    _mm256_store_si256((__m256i*)(void*)&bits[0], bitsV1);
    _mm256_store_si256((__m256i*)(void*)&bits[8], bitsV2);
#    if kNumStates == 32
    _mm256_store_si256((__m256i*)(void*)&state[16], stateV3);
    _mm256_store_si256((__m256i*)(void*)&state[24], stateV4);
    _mm256_store_si256((__m256i*)(void*)&bits[16], bitsV3);
    _mm256_store_si256((__m256i*)(void*)&bits[24], bitsV4);
#    endif

#endif // ZL_HAS_AVX2

    // uint32_t bits[32] ZL_ALIGNED(32);
    // _mm256_store_si256((__m256i*)&bits[0], bitsV1);
    // _mm256_store_si256((__m256i*)&bits[8], bitsV2);
    // _mm256_store_si256((__m256i*)&bits[16], bitsV3);
    // _mm256_store_si256((__m256i*)&bits[24], bitsV4);
    // fprintf(stderr, "tlog = %zu\n", tableLog);
    assert(op <= oend);
    for (size_t i = 0; i < kNumStates; ++i) {
        assert(bs > bend + reload[i]);
        assert(bits[i] <= 32);
        // fprintf(stderr, "d[%zu] = %x\n", i, state[i]);
    }
    // fprintf(stderr, "remaining = %u\n", (unsigned)(oend - op));
    // fprintf(stderr, "bs ?= bend %p %p\n", bs, bend);
    for (; op < oend;) {
        for (size_t i = 0; i < kNumStates && op < oend; ++i) {
            // fprintf(stderr, "%u\n", state[i]);
            uint32_t const shifted = state[i] >> (32 - tableLog);
            uint16_t const entry   = dtable[shifted];
            uint16_t const value   = (uint16_t)(entry & 0xFFF);
            uint8_t const nbits    = (uint8_t)(entry >> 12);
            // fprintf(stderr, "%zu (%x) byte=%u nbits=%u\n", i, masked,
            // (unsigned)byte, (unsigned)bit);
            ZL_writeLE16(op, value);
            op += 2;
            state[i] <<= nbits;
            assert(nbits <= tableLog);
            // assert(bit <= bits[i]);
            bits[i] += nbits;
            if (bits[i] > 16 && bs - 1 > bend + reload[i]) {
                // fprintf(stderr, "%zu read %zu: %u -> %u\n", op -
                // (uint8_t*)dst, i, bits[i], bits[i] + 16);
                bits[i] -= 16;
                bs -= 2;
                state[i] |= ((uint32_t)ZL_readLE16(bs)) << bits[i];
            }
        }
    }
    // fprintf(stderr, "bs ?= bend %p %p\n", bs, bend);
    ZS_HUF_RET_IF_NOT(op == oend);
    ZS_HUF_RET_IF_NOT(bs == bend);
    ZS_HUF_RET_IF_NOT(ip == iend);
    for (size_t i = 0; i < kNumStates; ++i) {
        // fprintf(stderr, "bits[%zu] = %u\n", i, bits[i]);
        ZS_HUF_RET_IF_NOT(bits[i] == 32);
    }

    return dstSize;
}
