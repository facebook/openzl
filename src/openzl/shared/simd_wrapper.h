// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_MATCH_FINDER_SIMD_WRAPPER_H
#define ZSTRONG_COMPRESS_MATCH_FINDER_SIMD_WRAPPER_H

#include <string.h>

#include "openzl/common/assertion.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"

#if ZL_HAS_NEON
#    include <arm_neon.h>
#endif
#if ZL_ARCH_X86_64
#    include <emmintrin.h>
#endif
#ifdef __AVX2__
#    include <immintrin.h>
#endif

ZL_BEGIN_C_DECLS

typedef uint32_t ZL_VecMask;

typedef struct {
    uint8_t data[16];
} ZL_Vec128Fallback;

ZL_INLINE ZL_Vec128Fallback ZL_Vec128Fallback_read(void const* ptr)
{
    ZL_Vec128Fallback out;
    memcpy(&out.data, ptr, sizeof(out.data));
    return out;
}

ZL_INLINE void ZL_Vec128Fallback_write(void* ptr, ZL_Vec128Fallback v)
{
    memcpy(ptr, &v.data, sizeof(v.data));
}

ZL_INLINE ZL_Vec128Fallback ZL_Vec128Fallback_set8(uint8_t val)
{
    ZL_Vec128Fallback out;
    memset(&out.data, (char)val, sizeof(out.data));
    return out;
}

ZL_INLINE ZL_Vec128Fallback
ZL_Vec128Fallback_cmp8(ZL_Vec128Fallback x, ZL_Vec128Fallback y)
{
    ZL_Vec128Fallback out;
    for (size_t i = 0; i < ZL_ARRAY_SIZE(x.data); ++i) {
        out.data[i] = x.data[i] == y.data[i] ? 0xff : 0;
    }
    return out;
}

ZL_INLINE ZL_Vec128Fallback
ZL_Vec128Fallback_and(ZL_Vec128Fallback x, ZL_Vec128Fallback y)
{
    ZL_Vec128Fallback out;
    for (size_t i = 0; i < ZL_ARRAY_SIZE(x.data); ++i) {
        out.data[i] = x.data[i] & y.data[i];
    }
    return out;
}

ZL_INLINE ZL_VecMask ZL_Vec128Fallback_mask8(ZL_Vec128Fallback v)
{
    ZL_VecMask out = 0;
    for (size_t i = 0; i < ZL_ARRAY_SIZE(v.data); ++i) {
        ZL_VecMask const bit = v.data[i] >> 7;
        out |= bit << i;
    }
    return out;
}

#if ZL_ARCH_X86_64

typedef __m128i ZL_Vec128;

ZL_INLINE ZL_Vec128 ZL_Vec128_read(void const* ptr)
{
    return _mm_loadu_si128((ZL_Vec128 const*)ptr);
}

ZL_INLINE void ZL_Vec128_write(void* ptr, ZL_Vec128 v)
{
    _mm_storeu_si128((ZL_Vec128*)ptr, v);
}

ZL_INLINE ZL_Vec128 ZL_Vec128_set8(uint8_t val)
{
    return _mm_set1_epi8((char)val);
}

ZL_INLINE ZL_Vec128 ZL_Vec128_cmp8(ZL_Vec128 x, ZL_Vec128 y)
{
    return _mm_cmpeq_epi8(x, y);
}

ZL_INLINE ZL_Vec128 ZL_Vec128_and(ZL_Vec128 x, ZL_Vec128 y)
{
    return _mm_and_si128(x, y);
}

ZL_INLINE ZL_VecMask ZL_Vec128_mask8(ZL_Vec128 v)
{
    return (ZL_VecMask)_mm_movemask_epi8(v);
}

#elif ZL_HAS_NEON

typedef uint8x16_t ZL_Vec128;

ZL_INLINE ZL_Vec128 ZL_Vec128_read(void const* ptr)
{
    return vld1q_u8((uint8_t const*)ptr);
}

ZL_INLINE void ZL_Vec128_write(void* ptr, ZL_Vec128 v)
{
    vst1q_u8((uint8_t*)ptr, v);
}

ZL_INLINE ZL_Vec128 ZL_Vec128_set8(uint8_t val)
{
    return vdupq_n_u8(val);
}

ZL_INLINE ZL_Vec128 ZL_Vec128_cmp8(ZL_Vec128 x, ZL_Vec128 y)
{
    return vceqq_u8(x, y);
}

ZL_INLINE ZL_Vec128 ZL_Vec128_and(ZL_Vec128 x, ZL_Vec128 y)
{
    return vandq_u8(x, y);
}

/*
 * Convert a byte comparison mask to a 16-bit bitmask.
 *
 * The input must contain only 0x00 or 0xFF bytes (e.g. the output of
 * ZL_Vec128_cmp8()). The implementation relies on this representation.
 */
ZL_INLINE ZL_VecMask ZL_Vec128_mask8(ZL_Vec128 v)
{
#    if ZL_HAS_NEON_DOTPROD
    int8x16_t const weights_s8 =
            vreinterpretq_s8_u64(vdupq_n_u64(0x80C0E0F0F8FCFEFFull));
    int8x16_t const v_s8 = vreinterpretq_s8_u8(v);
#        if ZL_HAS_NEON_I8MM
    int32x4_t const acc_s32 = vmmlaq_s32(vdupq_n_s32(0), v_s8, weights_s8);
#        else
    int32x4_t acc_s32 = vdotq_s32(vdupq_n_s32(0), v_s8, weights_s8);
    acc_s32           = vreinterpretq_s32_s64(vpaddlq_s32(acc_s32));
#        endif
    uint8x16_t acc_u8 = vreinterpretq_u8_s32(acc_s32);
    acc_u8            = vcopyq_laneq_u8(acc_u8, 1, acc_u8, 8);
    return vgetq_lane_u32(vreinterpretq_u32_u8(acc_u8), 0);
#    else
    uint8x16_t const weights =
            vreinterpretq_u8_u64(vdupq_n_u64(0x8040201008040201ull));
    uint8x16_t const weighted = vandq_u8(v, weights);
    uint8_t const loAcc       = vaddv_u8(vget_low_u8(weighted));
    uint8_t const hiAcc       = vaddv_u8(vget_high_u8(weighted));
    return (ZL_VecMask)loAcc | ((ZL_VecMask)hiAcc << 8);
#    endif
}

#else

typedef ZL_Vec128Fallback ZL_Vec128;

ZL_INLINE ZL_Vec128 ZL_Vec128_read(void const* ptr)
{
    return ZL_Vec128Fallback_read(ptr);
}

ZL_INLINE void ZL_Vec128_write(void* ptr, ZL_Vec128 v)
{
    ZL_Vec128Fallback_write(ptr, v);
}

ZL_INLINE ZL_Vec128 ZL_Vec128_set8(uint8_t val)
{
    return ZL_Vec128Fallback_set8(val);
}

ZL_INLINE ZL_Vec128 ZL_Vec128_cmp8(ZL_Vec128 x, ZL_Vec128 y)
{
    return ZL_Vec128Fallback_cmp8(x, y);
}

ZL_INLINE ZL_Vec128 ZL_Vec128_and(ZL_Vec128 x, ZL_Vec128 y)
{
    return ZL_Vec128Fallback_and(x, y);
}

ZL_INLINE ZL_VecMask ZL_Vec128_mask8(ZL_Vec128 v)
{
    return ZL_Vec128Fallback_mask8(v);
}

#endif

typedef struct {
    ZL_Vec128 fst;
    ZL_Vec128 snd;
} ZL_Vec256Fallback;

ZL_INLINE ZL_Vec256Fallback ZL_Vec256Fallback_read(void const* ptr)
{
    ZL_Vec256Fallback v;
    v.fst = ZL_Vec128_read(ptr);
    v.snd = ZL_Vec128_read((ZL_Vec128 const*)ptr + 1);
    return v;
}

ZL_INLINE void ZL_Vec256Fallback_write(void* ptr, ZL_Vec256Fallback v)
{
    ZL_Vec128_write(ptr, v.fst);
    ZL_Vec128_write((ZL_Vec128*)ptr + 1, v.snd);
}

ZL_INLINE ZL_Vec256Fallback ZL_Vec256Fallback_set8(uint8_t val)
{
    ZL_Vec256Fallback v;
    v.fst = ZL_Vec128_set8(val);
    v.snd = ZL_Vec128_set8(val);
    return v;
}

ZL_INLINE ZL_Vec256Fallback
ZL_Vec256Fallback_cmp8(ZL_Vec256Fallback x, ZL_Vec256Fallback y)
{
    ZL_Vec256Fallback v;
    v.fst = ZL_Vec128_cmp8(x.fst, y.fst);
    v.snd = ZL_Vec128_cmp8(x.snd, y.snd);
    return v;
}

ZL_INLINE ZL_Vec256Fallback
ZL_Vec256Fallback_and(ZL_Vec256Fallback x, ZL_Vec256Fallback y)
{
    ZL_Vec256Fallback v;
    v.fst = ZL_Vec128_and(x.fst, y.fst);
    v.snd = ZL_Vec128_and(x.snd, y.snd);
    return v;
}

ZL_INLINE ZL_VecMask ZL_Vec256Fallback_mask8(ZL_Vec256Fallback v)
{
    return ZL_Vec128_mask8(v.fst) | (ZL_Vec128_mask8(v.snd) << 16);
}

#ifdef __AVX2__

typedef __m256i ZL_Vec256;

ZL_INLINE ZL_Vec256 ZL_Vec256_read(void const* ptr)
{
    return _mm256_loadu_si256((ZL_Vec256 const*)ptr);
}

ZL_INLINE void ZL_Vec256_write(void* ptr, ZL_Vec256 v)
{
    _mm256_storeu_si256((ZL_Vec256*)ptr, v);
}

ZL_INLINE ZL_Vec256 ZL_Vec256_set8(uint8_t val)
{
    return _mm256_set1_epi8((char)val);
}

ZL_INLINE ZL_Vec256 ZL_Vec256_cmp8(ZL_Vec256 x, ZL_Vec256 y)
{
    return _mm256_cmpeq_epi8(x, y);
}

ZL_INLINE ZL_Vec256 ZL_Vec256_and(ZL_Vec256 x, ZL_Vec256 y)
{
    return _mm256_and_si256(x, y);
}

ZL_INLINE ZL_VecMask ZL_Vec256_mask8(ZL_Vec256 v)
{
    return (ZL_VecMask)_mm256_movemask_epi8(v);
}

#elif ZL_HAS_NEON

typedef uint8x16x2_t ZL_Vec256;

ZL_INLINE ZL_Vec256 ZL_Vec256_read(void const* ptr)
{
    return vld1q_u8_x2((uint8_t const*)ptr);
}

ZL_INLINE void ZL_Vec256_write(void* ptr, ZL_Vec256 v)
{
    vst1q_u8_x2((uint8_t*)ptr, v);
}

ZL_INLINE ZL_Vec256 ZL_Vec256_set8(uint8_t val)
{
    ZL_Vec256 v;
    v.val[0] = v.val[1] = vdupq_n_u8(val);
    return v;
}

ZL_INLINE ZL_Vec256 ZL_Vec256_cmp8(ZL_Vec256 x, ZL_Vec256 y)
{
    x.val[0] = vceqq_u8(x.val[0], y.val[0]);
    x.val[1] = vceqq_u8(x.val[1], y.val[1]);
    return x;
}

ZL_INLINE ZL_Vec256 ZL_Vec256_and(ZL_Vec256 x, ZL_Vec256 y)
{
    x.val[0] = vandq_u8(x.val[0], y.val[0]);
    x.val[1] = vandq_u8(x.val[1], y.val[1]);
    return x;
}

/*
 * Convert a byte comparison mask to a 32-bit bitmask.
 *
 * The input must contain only 0x00 or 0xFF bytes (e.g. the output of
 * ZL_Vec256_cmp8()). The implementation relies on this representation.
 */
ZL_INLINE ZL_VecMask ZL_Vec256_mask8(ZL_Vec256 v)
{
#    if ZL_HAS_NEON_DOTPROD
    int8x16_t const weights_s8 =
            vreinterpretq_s8_u64(vdupq_n_u64(0x80C0E0F0F8FCFEFFull));
    int8x16_t const lo_s8 = vreinterpretq_s8_u8(v.val[0]);
    int8x16_t const hi_s8 = vreinterpretq_s8_u8(v.val[1]);
#        if ZL_HAS_NEON_I8MM
    int32x4_t const loAcc_s32 = vmmlaq_s32(vdupq_n_s32(0), lo_s8, weights_s8);
    int32x4_t const hiAcc_s32 = vmmlaq_s32(vdupq_n_s32(0), hi_s8, weights_s8);
    v.val[0]                  = vreinterpretq_u8_s32(loAcc_s32);
    v.val[1]                  = vreinterpretq_u8_s32(hiAcc_s32);
    uint8x8_t const acc_u8    = vqtbl2_u8(v, vcreate_u8(0x18100800));
#        else
    int32x4_t const loAcc_s32 = vdotq_s32(vdupq_n_s32(0), lo_s8, weights_s8);
    int32x4_t const hiAcc_s32 = vdotq_s32(vdupq_n_s32(0), hi_s8, weights_s8);
    int32x4_t const acc_s32   = vpaddq_s32(loAcc_s32, hiAcc_s32);
    uint8x8_t const tbl_u8    = vcreate_u8(0x0C080400);
    uint8x8_t const acc_u8 = vqtbl1_u8(vreinterpretq_u8_s32(acc_s32), tbl_u8);
#        endif
    return vget_lane_u32(vreinterpret_u32_u8(acc_u8), 0);
#    else
    return ZL_Vec128_mask8(v.val[0]) | (ZL_Vec128_mask8(v.val[1]) << 16);
#    endif
}

#else

typedef ZL_Vec256Fallback ZL_Vec256;

ZL_INLINE ZL_Vec256 ZL_Vec256_read(void const* ptr)
{
    return ZL_Vec256Fallback_read(ptr);
}

ZL_INLINE void ZL_Vec256_write(void* ptr, ZL_Vec256 v)
{
    ZL_Vec256Fallback_write(ptr, v);
}

ZL_INLINE ZL_Vec256 ZL_Vec256_set8(uint8_t val)
{
    return ZL_Vec256Fallback_set8(val);
}

ZL_INLINE ZL_Vec256 ZL_Vec256_cmp8(ZL_Vec256 x, ZL_Vec256 y)
{
    return ZL_Vec256Fallback_cmp8(x, y);
}

ZL_INLINE ZL_Vec256 ZL_Vec256_and(ZL_Vec256 x, ZL_Vec256 y)
{
    return ZL_Vec256Fallback_and(x, y);
}

ZL_INLINE ZL_VecMask ZL_Vec256_mask8(ZL_Vec256 v)
{
    return ZL_Vec256Fallback_mask8(v);
}

#endif

/**
 * while (m) {
 *   int const bit = ZL_VecMask_next(m);
 *   m &= m - 1;
 * }
 */
ZL_INLINE uint32_t ZL_VecMask_next(ZL_VecMask m)
{
    return (uint32_t)ZL_ctz32((uint32_t)m);
}

ZL_FORCE_INLINE ZL_VecMask
ZL_VecMask_rotateRight(ZL_VecMask mask, uint32_t rotation, uint32_t totalBits)
{
    ZL_ASSERT_LT(rotation, totalBits);
    if (rotation == 0)
        return mask;
    switch (totalBits) {
        case 8:
            return (mask >> rotation) | (uint8_t)(mask << (8 - rotation));
        case 16:
            return (mask >> rotation) | (uint16_t)(mask << (16 - rotation));
        case 32:
            return (mask >> rotation) | (uint32_t)(mask << (32 - rotation));
        default:
            return (mask >> rotation)
                    | ((mask << (totalBits - rotation))
                       & ((1u << totalBits) - 1));
    }
}

ZL_END_C_DECLS

#endif
