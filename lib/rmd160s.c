// Copyright (c) vladkens
// https://github.com/vladkens/ecloop
// Licensed under the MIT License.

#pragma once
#include <stdalign.h>
#include <stdint.h>

// https://homes.esat.kuleuven.be/~bosselae/ripemd160/pdf/AB-9601/AB-9601.pdf
// https://homes.esat.kuleuven.be/~bosselae/ripemd160/ps/AB-9601/rmd160.h
// https://homes.esat.kuleuven.be/~bosselae/ripemd160/ps/AB-9601/rmd160.c

#define RMD_K1 0x67452301
#define RMD_K2 0xEFCDAB89
#define RMD_K3 0x98BADCFE
#define RMD_K4 0x10325476
#define RMD_K5 0xC3D2E1F0

#if defined(__aarch64__) && defined(__ARM_NEON) && !defined(NO_SIMD)
  #include <arm_neon.h>

  #define RMD_LEN 4
  #define RMD_VEC uint32x4_t
  #define RMD_LD_NUM(x) vdupq_n_u32(x)

  #define RMD_SWAP(x) vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(x)))
  #define RMD_LOAD(x, i) vld1q_u32(((uint32_t[4]){x[0][i], x[1][i], x[2][i], x[3][i]}))
  #define RMD_DUMP(r, s, i)                                                                        \
    do {                                                                                           \
      alignas(16) uint32_t tmp[4];                                                                 \
      vst1q_u32(tmp, s[i]);                                                                        \
      for (int j = 0; j < 4; ++j) r[j][i] = tmp[j];                                                \
    } while (0);

  #define RMD_F1(x, y, z) veorq_u32(veorq_u32(x, y), z)
  #define RMD_F2(x, y, z) vbslq_u32(x, y, z)
  #define RMD_F3(x, y, z) veorq_u32(vorrq_u32(x, vmvnq_u32(y)), z)
  #define RMD_F4(x, y, z) vbslq_u32(z, x, y)
  #define RMD_F5(x, y, z) veorq_u32(x, vorrq_u32(y, vmvnq_u32(z)))

  #define RMD_ROTL(x, n) vsriq_n_u32(vshlq_n_u32(x, n), x, 32 - (n))
  #define RMD_ADD2(a, b) vaddq_u32(a, b)
  #define RMD_ADD3(a, b, c) vaddq_u32(vaddq_u32(a, b), c)
  #define RMD_ADD4(a, b, c, d) vaddq_u32(vaddq_u32(vaddq_u32(a, b), c), d)

#elif defined(__x86_64__) && defined(__AVX2__) && !defined(NO_SIMD)
  #include <immintrin.h>

  #define RMD_LEN 8
  #define RMD_VEC __m256i
  #define RMD_LD_NUM(x) _mm256_set1_epi32(x)

  #define RMD_SWAP(x)                                                                              \
    _mm256_shuffle_epi8((x), _mm256_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13,    \
                                              12, 19, 18, 17, 16, 23, 22, 21, 20, 27, 26, 25, 24,  \
                                              31, 30, 29, 28))

  #define RMD_LOAD(x, i)                                                                           \
    _mm256_set_epi32(x[7][i], x[6][i], x[5][i], x[4][i], x[3][i], x[2][i], x[1][i], x[0][i])

  #define RMD_DUMP(r, s, i)                                                                        \
    do {                                                                                           \
      alignas(32) int32_t tmp[8];                                                                  \
      _mm256_store_si256((__m256i *)tmp, s[i]);                                                    \
      for (int j = 0; j < 8; ++j) r[j][i] = tmp[j];                                                \
    } while (0);

  #define _mm256_not_si256(x) _mm256_xor_si256((x), _mm256_set1_epi32(0xffffffff))
  #define RMD_F1(x, y, z) _mm256_xor_si256(x, _mm256_xor_si256(y, z))
  #define RMD_F2(x, y, z) _mm256_or_si256(_mm256_and_si256(x, y), _mm256_andnot_si256(x, z))
  #define RMD_F3(x, y, z) _mm256_xor_si256(_mm256_or_si256(x, _mm256_not_si256(y)), z)
  #define RMD_F4(x, y, z) _mm256_or_si256(_mm256_and_si256(x, z), _mm256_andnot_si256(z, y))
  #define RMD_F5(x, y, z) _mm256_xor_si256(x, _mm256_or_si256(y, _mm256_not_si256(z)))

  #define RMD_ROTL(x, n) _mm256_or_si256(_mm256_slli_epi32(x, n), _mm256_srli_epi32(x, 32 - (n)))
  #define RMD_ADD2(a, b) _mm256_add_epi32(a, b)
  #define RMD_ADD3(a, b, c) _mm256_add_epi32(_mm256_add_epi32(a, b), c)
  #define RMD_ADD4(a, b, c, d) _mm256_add_epi32(_mm256_add_epi32(a, b), _mm256_add_epi32(c, d))

#else
  #warning "Fallback RIPEMD-160 implementation used. AVX2 or NEON required for SIMD"

  #define RMD_LEN 1
  #define RMD_VEC uint32_t
  #define RMD_LD_NUM(x) x

  #define RMD_SWAP(x) __builtin_bswap32(x)
  #define RMD_LOAD(x, i) x[0][i]
  #define RMD_DUMP(r, s, i) r[0][i] = s[i]

  #define RMD_F1(x, y, z) ((x) ^ (y) ^ (z))
  #define RMD_F2(x, y, z) (((x) & (y)) | (~(x) & (z)))
  #define RMD_F3(x, y, z) (((x) | ~(y)) ^ (z))
  #define RMD_F4(x, y, z) (((x) & (z)) | ((y) & ~(z)))
  #define RMD_F5(x, y, z) ((x) ^ ((y) | ~(z)))

  #define RMD_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
  #define RMD_ADD2(a, b) (a + b)
  #define RMD_ADD3(a, b, c) (a + b + c)
  #define RMD_ADD4(a, b, c, d) (a + b + c + d)

#endif

#define RMD_RN(a, b, c, d, e, f, x, k, r)                                                          \
  u = RMD_ADD4(a, f, x, RMD_LD_NUM(k));                                                            \
  a = RMD_ADD2(RMD_ROTL(u, r), e);                                                                 \
  c = RMD_ROTL(c, 10);

#define RMD_L1(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F1(b, c, d), x, 0, r)
#define RMD_L2(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F2(b, c, d), x, 0x5A827999ul, r)
#define RMD_L3(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F3(b, c, d), x, 0x6ED9EBA1ul, r)
#define RMD_L4(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F4(b, c, d), x, 0x8F1BBCDCul, r)
#define RMD_L5(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F5(b, c, d), x, 0xA953FD4Eul, r)
#define RMD_R1(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F5(b, c, d), x, 0x50A28BE6ul, r)
#define RMD_R2(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F4(b, c, d), x, 0x5C4DD124ul, r)
#define RMD_R3(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F3(b, c, d), x, 0x6D703EF3ul, r)
#define RMD_R4(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F2(b, c, d), x, 0x7A6D76E9ul, r)
#define RMD_R5(a, b, c, d, e, x, r) RMD_RN(a, b, c, d, e, RMD_F1(b, c, d), x, 0, r)

#define RMD_LOAD_SWAP(x, i) RMD_SWAP(RMD_LOAD(x, i))

void rmd160_block(RMD_VEC *s, const uint32_t x[RMD_LEN][16]) {
  RMD_VEC a1, b1, c1, d1, e1, a2, b2, c2, d2, e2, u;
  a1 = a2 = RMD_LD_NUM(RMD_K1);
  b1 = b2 = RMD_LD_NUM(RMD_K2);
  c1 = c2 = RMD_LD_NUM(RMD_K3);
  d1 = d2 = RMD_LD_NUM(RMD_K4);
  e1 = e2 = RMD_LD_NUM(RMD_K5);

  RMD_VEC w[16];
  // for (int i = 0; i < 16; i++) w[i] = RMD_LOAD(x, i);

  // SHA256 is big-endian, but RIPEMD-160 is little-endian, so swap bytes here
  // keep unrolled let ILP decide how to schedule
  w[0] = RMD_LOAD_SWAP(x, 0);
  w[1] = RMD_LOAD_SWAP(x, 1);
  w[2] = RMD_LOAD_SWAP(x, 2);
  w[3] = RMD_LOAD_SWAP(x, 3);
  w[4] = RMD_LOAD_SWAP(x, 4);
  w[5] = RMD_LOAD_SWAP(x, 5);
  w[6] = RMD_LOAD_SWAP(x, 6);
  w[7] = RMD_LOAD_SWAP(x, 7);
  w[8] = RMD_LOAD_SWAP(x, 8);
  w[9] = RMD_LOAD_SWAP(x, 9);
  w[10] = RMD_LOAD_SWAP(x, 10);
  w[11] = RMD_LOAD_SWAP(x, 11);
  w[12] = RMD_LOAD_SWAP(x, 12);
  w[13] = RMD_LOAD_SWAP(x, 13);
  w[14] = RMD_LOAD_SWAP(x, 14);
  w[15] = RMD_LOAD_SWAP(x, 15);

  RMD_L1(a1, b1, c1, d1, e1, w[0], 11);
  RMD_R1(a2, b2, c2, d2, e2, w[5], 8);
  RMD_L1(e1, a1, b1, c1, d1, w[1], 14);
  RMD_R1(e2, a2, b2, c2, d2, w[14], 9);
  RMD_L1(d1, e1, a1, b1, c1, w[2], 15);
  RMD_R1(d2, e2, a2, b2, c2, w[7], 9);
  RMD_L1(c1, d1, e1, a1, b1, w[3], 12);
  RMD_R1(c2, d2, e2, a2, b2, w[0], 11);
  RMD_L1(b1, c1, d1, e1, a1, w[4], 5);
  RMD_R1(b2, c2, d2, e2, a2, w[9], 13);
  RMD_L1(a1, b1, c1, d1, e1, w[5], 8);
  RMD_R1(a2, b2, c2, d2, e2, w[2], 15);
  RMD_L1(e1, a1, b1, c1, d1, w[6], 7);
  RMD_R1(e2, a2, b2, c2, d2, w[11], 15);
  RMD_L1(d1, e1, a1, b1, c1, w[7], 9);
  RMD_R1(d2, e2, a2, b2, c2, w[4], 5);
  RMD_L1(c1, d1, e1, a1, b1, w[8], 11);
  RMD_R1(c2, d2, e2, a2, b2, w[13], 7);
  RMD_L1(b1, c1, d1, e1, a1, w[9], 13);
  RMD_R1(b2, c2, d2, e2, a2, w[6], 7);
  RMD_L1(a1, b1, c1, d1, e1, w[10], 14);
  RMD_R1(a2, b2, c2, d2, e2, w[15], 8);
  RMD_L1(e1, a1, b1, c1, d1, w[11], 15);
  RMD_R1(e2, a2, b2, c2, d2, w[8], 11);
  RMD_L1(d1, e1, a1, b1, c1, w[12], 6);
  RMD_R1(d2, e2, a2, b2, c2, w[1], 14);
  RMD_L1(c1, d1, e1, a1, b1, w[13], 7);
  RMD_R1(c2, d2, e2, a2, b2, w[10], 14);
  RMD_L1(b1, c1, d1, e1, a1, w[14], 9);
  RMD_R1(b2, c2, d2, e2, a2, w[3], 12);
  RMD_L1(a1, b1, c1, d1, e1, w[15], 8);
  RMD_R1(a2, b2, c2, d2, e2, w[12], 6);

  RMD_L2(e1, a1, b1, c1, d1, w[7], 7);
  RMD_R2(e2, a2, b2, c2, d2, w[6], 9);
  RMD_L2(d1, e1, a1, b1, c1, w[4], 6);
  RMD_R2(d2, e2, a2, b2, c2, w[11], 13);
  RMD_L2(c1, d1, e1, a1, b1, w[13], 8);
  RMD_R2(c2, d2, e2, a2, b2, w[3], 15);
  RMD_L2(b1, c1, d1, e1, a1, w[1], 13);
  RMD_R2(b2, c2, d2, e2, a2, w[7], 7);
  RMD_L2(a1, b1, c1, d1, e1, w[10], 11);
  RMD_R2(a2, b2, c2, d2, e2, w[0], 12);
  RMD_L2(e1, a1, b1, c1, d1, w[6], 9);
  RMD_R2(e2, a2, b2, c2, d2, w[13], 8);
  RMD_L2(d1, e1, a1, b1, c1, w[15], 7);
  RMD_R2(d2, e2, a2, b2, c2, w[5], 9);
  RMD_L2(c1, d1, e1, a1, b1, w[3], 15);
  RMD_R2(c2, d2, e2, a2, b2, w[10], 11);
  RMD_L2(b1, c1, d1, e1, a1, w[12], 7);
  RMD_R2(b2, c2, d2, e2, a2, w[14], 7);
  RMD_L2(a1, b1, c1, d1, e1, w[0], 12);
  RMD_R2(a2, b2, c2, d2, e2, w[15], 7);
  RMD_L2(e1, a1, b1, c1, d1, w[9], 15);
  RMD_R2(e2, a2, b2, c2, d2, w[8], 12);
  RMD_L2(d1, e1, a1, b1, c1, w[5], 9);
  RMD_R2(d2, e2, a2, b2, c2, w[12], 7);
  RMD_L2(c1, d1, e1, a1, b1, w[2], 11);
  RMD_R2(c2, d2, e2, a2, b2, w[4], 6);
  RMD_L2(b1, c1, d1, e1, a1, w[14], 7);
  RMD_R2(b2, c2, d2, e2, a2, w[9], 15);
  RMD_L2(a1, b1, c1, d1, e1, w[11], 13);
  RMD_R2(a2, b2, c2, d2, e2, w[1], 13);
  RMD_L2(e1, a1, b1, c1, d1, w[8], 12);
  RMD_R2(e2, a2, b2, c2, d2, w[2], 11);

  RMD_L3(d1, e1, a1, b1, c1, w[3], 11);
  RMD_R3(d2, e2, a2, b2, c2, w[15], 9);
  RMD_L3(c1, d1, e1, a1, b1, w[10], 13);
  RMD_R3(c2, d2, e2, a2, b2, w[5], 7);
  RMD_L3(b1, c1, d1, e1, a1, w[14], 6);
  RMD_R3(b2, c2, d2, e2, a2, w[1], 15);
  RMD_L3(a1, b1, c1, d1, e1, w[4], 7);
  RMD_R3(a2, b2, c2, d2, e2, w[3], 11);
  RMD_L3(e1, a1, b1, c1, d1, w[9], 14);
  RMD_R3(e2, a2, b2, c2, d2, w[7], 8);
  RMD_L3(d1, e1, a1, b1, c1, w[15], 9);
  RMD_R3(d2, e2, a2, b2, c2, w[14], 6);
  RMD_L3(c1, d1, e1, a1, b1, w[8], 13);
  RMD_R3(c2, d2, e2, a2, b2, w[6], 6);
  RMD_L3(b1, c1, d1, e1, a1, w[1], 15);
  RMD_R3(b2, c2, d2, e2, a2, w[9], 14);
  RMD_L3(a1, b1, c1, d1, e1, w[2], 14);
  RMD_R3(a2, b2, c2, d2, e2, w[11], 12);
  RMD_L3(e1, a1, b1, c1, d1, w[7], 8);
  RMD_R3(e2, a2, b2, c2, d2, w[8], 13);
  RMD_L3(d1, e1, a1, b1, c1, w[0], 13);
  RMD_R3(d2, e2, a2, b2, c2, w[12], 5);
  RMD_L3(c1, d1, e1, a1, b1, w[6], 6);
  RMD_R3(c2, d2, e2, a2, b2, w[2], 14);
  RMD_L3(b1, c1, d1, e1, a1, w[13], 5);
  RMD_R3(b2, c2, d2, e2, a2, w[10], 13);
  RMD_L3(a1, b1, c1, d1, e1, w[11], 12);
  RMD_R3(a2, b2, c2, d2, e2, w[0], 13);
  RMD_L3(e1, a1, b1, c1, d1, w[5], 7);
  RMD_R3(e2, a2, b2, c2, d2, w[4], 7);
  RMD_L3(d1, e1, a1, b1, c1, w[12], 5);
  RMD_R3(d2, e2, a2, b2, c2, w[13], 5);

  RMD_L4(c1, d1, e1, a1, b1, w[1], 11);
  RMD_R4(c2, d2, e2, a2, b2, w[8], 15);
  RMD_L4(b1, c1, d1, e1, a1, w[9], 12);
  RMD_R4(b2, c2, d2, e2, a2, w[6], 5);
  RMD_L4(a1, b1, c1, d1, e1, w[11], 14);
  RMD_R4(a2, b2, c2, d2, e2, w[4], 8);
  RMD_L4(e1, a1, b1, c1, d1, w[10], 15);
  RMD_R4(e2, a2, b2, c2, d2, w[1], 11);
  RMD_L4(d1, e1, a1, b1, c1, w[0], 14);
  RMD_R4(d2, e2, a2, b2, c2, w[3], 14);
  RMD_L4(c1, d1, e1, a1, b1, w[8], 15);
  RMD_R4(c2, d2, e2, a2, b2, w[11], 14);
  RMD_L4(b1, c1, d1, e1, a1, w[12], 9);
  RMD_R4(b2, c2, d2, e2, a2, w[15], 6);
  RMD_L4(a1, b1, c1, d1, e1, w[4], 8);
  RMD_R4(a2, b2, c2, d2, e2, w[0], 14);
  RMD_L4(e1, a1, b1, c1, d1, w[13], 9);
  RMD_R4(e2, a2, b2, c2, d2, w[5], 6);
  RMD_L4(d1, e1, a1, b1, c1, w[3], 14);
  RMD_R4(d2, e2, a2, b2, c2, w[12], 9);
  RMD_L4(c1, d1, e1, a1, b1, w[7], 5);
  RMD_R4(c2, d2, e2, a2, b2, w[2], 12);
  RMD_L4(b1, c1, d1, e1, a1, w[15], 6);
  RMD_R4(b2, c2, d2, e2, a2, w[13], 9);
  RMD_L4(a1, b1, c1, d1, e1, w[14], 8);
  RMD_R4(a2, b2, c2, d2, e2, w[9], 12);
  RMD_L4(e1, a1, b1, c1, d1, w[5], 6);
  RMD_R4(e2, a2, b2, c2, d2, w[7], 5);
  RMD_L4(d1, e1, a1, b1, c1, w[6], 5);
  RMD_R4(d2, e2, a2, b2, c2, w[10], 15);
  RMD_L4(c1, d1, e1, a1, b1, w[2], 12);
  RMD_R4(c2, d2, e2, a2, b2, w[14], 8);

  RMD_L5(b1, c1, d1, e1, a1, w[4], 9);
  RMD_R5(b2, c2, d2, e2, a2, w[12], 8);
  RMD_L5(a1, b1, c1, d1, e1, w[0], 15);
  RMD_R5(a2, b2, c2, d2, e2, w[15], 5);
  RMD_L5(e1, a1, b1, c1, d1, w[5], 5);
  RMD_R5(e2, a2, b2, c2, d2, w[10], 12);
  RMD_L5(d1, e1, a1, b1, c1, w[9], 11);
  RMD_R5(d2, e2, a2, b2, c2, w[4], 9);
  RMD_L5(c1, d1, e1, a1, b1, w[7], 6);
  RMD_R5(c2, d2, e2, a2, b2, w[1], 12);
  RMD_L5(b1, c1, d1, e1, a1, w[12], 8);
  RMD_R5(b2, c2, d2, e2, a2, w[5], 5);
  RMD_L5(a1, b1, c1, d1, e1, w[2], 13);
  RMD_R5(a2, b2, c2, d2, e2, w[8], 14);
  RMD_L5(e1, a1, b1, c1, d1, w[10], 12);
  RMD_R5(e2, a2, b2, c2, d2, w[7], 6);
  RMD_L5(d1, e1, a1, b1, c1, w[14], 5);
  RMD_R5(d2, e2, a2, b2, c2, w[6], 8);
  RMD_L5(c1, d1, e1, a1, b1, w[1], 12);
  RMD_R5(c2, d2, e2, a2, b2, w[2], 13);
  RMD_L5(b1, c1, d1, e1, a1, w[3], 13);
  RMD_R5(b2, c2, d2, e2, a2, w[13], 6);
  RMD_L5(a1, b1, c1, d1, e1, w[8], 14);
  RMD_R5(a2, b2, c2, d2, e2, w[14], 5);
  RMD_L5(e1, a1, b1, c1, d1, w[11], 11);
  RMD_R5(e2, a2, b2, c2, d2, w[0], 15);
  RMD_L5(d1, e1, a1, b1, c1, w[6], 8);
  RMD_R5(d2, e2, a2, b2, c2, w[3], 13);
  RMD_L5(c1, d1, e1, a1, b1, w[15], 5);
  RMD_R5(c2, d2, e2, a2, b2, w[9], 11);
  RMD_L5(b1, c1, d1, e1, a1, w[13], 6);
  RMD_R5(b2, c2, d2, e2, a2, w[11], 11);

  RMD_VEC t = s[0];
  s[0] = RMD_ADD3(s[1], c1, d2);
  s[1] = RMD_ADD3(s[2], d1, e2);
  s[2] = RMD_ADD3(s[3], e1, a2);
  s[3] = RMD_ADD3(s[4], a1, b2);
  s[4] = RMD_ADD3(t, b1, c2);
}

void rmd160_batch(uint32_t r[RMD_LEN][5], const uint32_t x[RMD_LEN][16]) {
  RMD_VEC s[5] = {0}; // load initial state
  s[0] = RMD_LD_NUM(RMD_K1);
  s[1] = RMD_LD_NUM(RMD_K2);
  s[2] = RMD_LD_NUM(RMD_K3);
  s[3] = RMD_LD_NUM(RMD_K4);
  s[4] = RMD_LD_NUM(RMD_K5);

  rmd160_block((RMD_VEC *)s, x);                     // round
  for (int i = 0; i < 5; ++i) s[i] = RMD_SWAP(s[i]); // change endian
  for (int i = 0; i < 5; ++i) RMD_DUMP(r, s, i);     // dump data to array
}
