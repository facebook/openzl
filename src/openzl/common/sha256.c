// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/sha256.h"

#include <stdint.h>
#include <string.h>

// ============================================================================
// SHA256 Implementation
// ============================================================================
// TODO(csv): fast sha256 implementation

// macros
#define S(x, n)                                                \
    (((((uint32_t)(x) & 0xFFFFFFFFU) >> (uint32_t)((n) & 31))  \
      | ((uint32_t)(x) << (uint32_t)((32 - ((n) & 31)) & 31))) \
     & 0xFFFFFFFFU)
#define R(x, n) (((x) & 0xFFFFFFFFU) >> (n))
#define Gamma0(x) (S(x, 7) ^ S(x, 18) ^ R(x, 3))
#define Gamma1(x) (S(x, 17) ^ S(x, 19) ^ R(x, 10))
#define RND(a, b, c, d, e, f, g, h, i)                                    \
    t0 = h + (S(e, 6) ^ S(e, 11) ^ S(e, 25)) + (g ^ (e & (f ^ g))) + K[i] \
            + W[i];                                                       \
    t1 = (S(a, 2) ^ S(a, 13) ^ S(a, 22)) + (((a | b) & c) | (a & b));     \
    d += t0;                                                              \
    h = t0 + t1;
#define STORE32H(x, y)                           \
    (y)[0] = (unsigned char)(((x) >> 24) & 255); \
    (y)[1] = (unsigned char)(((x) >> 16) & 255); \
    (y)[2] = (unsigned char)(((x) >> 8) & 255);  \
    (y)[3] = (unsigned char)((x) & 255);
#define LOAD32H(x, y)                                                       \
    x = ((uint32_t)((y)[0] & 255) << 24) | ((uint32_t)((y)[1] & 255) << 16) \
            | ((uint32_t)((y)[2] & 255) << 8) | ((uint32_t)((y)[3] & 255));
#define STORE64H(x, y)                           \
    (y)[0] = (unsigned char)(((x) >> 56) & 255); \
    (y)[1] = (unsigned char)(((x) >> 48) & 255); \
    (y)[2] = (unsigned char)(((x) >> 40) & 255); \
    (y)[3] = (unsigned char)(((x) >> 32) & 255); \
    (y)[4] = (unsigned char)(((x) >> 24) & 255); \
    (y)[5] = (unsigned char)(((x) >> 16) & 255); \
    (y)[6] = (unsigned char)(((x) >> 8) & 255);  \
    (y)[7] = (unsigned char)((x) & 255);
#define SHA256_COMPRESS(buff)                                               \
    for (int i = 0; i < 8; i++)                                             \
        S[i] = sha256_state[i];                                             \
    for (int i = 0; i < 16; i++)                                            \
        LOAD32H(W[i], buff + (4 * i));                                      \
    for (int i = 16; i < 64; i++)                                           \
        W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) + W[i - 16]; \
    for (int i = 0; i < 64; i++) {                                          \
        RND(S[0], S[1], S[2], S[3], S[4], S[5], S[6], S[7], i);             \
        t    = S[7];                                                        \
        S[7] = S[6];                                                        \
        S[6] = S[5];                                                        \
        S[5] = S[4];                                                        \
        S[4] = S[3];                                                        \
        S[3] = S[2];                                                        \
        S[2] = S[1];                                                        \
        S[1] = S[0];                                                        \
        S[0] = t;                                                           \
    }                                                                       \
    for (int i = 0; i < 8; i++)                                             \
        sha256_state[i] = sha256_state[i] + S[i];

void ZL_sha256(const unsigned char* in, size_t len, unsigned char* out)
{
    const uint32_t K[64] = {
        0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
        0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
        0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
        0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
        0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
        0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
        0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
        0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
        0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
        0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
        0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
        0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
        0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
    };
    uint64_t sha256_length   = 0;
    uint32_t sha256_state[8] = { 0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL,
                                 0xA54FF53AUL, 0x510E527FUL, 0x9B05688CUL,
                                 0x1F83D9ABUL, 0x5BE0CD19UL },
             S[8], W[64], t0, t1, t;
    unsigned char sha256_buf[64];
    // process input in 64 byte chunks
    while (len >= 64) {
        SHA256_COMPRESS(in);
        sha256_length += 64 * 8;
        in += 64;
        len -= 64;
    }
    // copy remaining bytes into sha256_buf
    memcpy(sha256_buf, in, len);
    // finish up (len now number of bytes in sha256_buf)
    sha256_length += len * 8;
    sha256_buf[len++] = 0x80;
    // pad then compress if length is above 56 bytes
    if (len > 56) {
        while (len < 64)
            sha256_buf[len++] = 0;
        SHA256_COMPRESS(sha256_buf);
        len = 0;
    }
    // pad up to 56 bytes
    while (len < 56)
        sha256_buf[len++] = 0;
    // store length and compress
    STORE64H(sha256_length, sha256_buf + 56);
    SHA256_COMPRESS(sha256_buf);
    // copy output
    for (int i = 0; i < 8; i++) {
        STORE32H(sha256_state[i], out + 4 * i);
    }
}
