// Copyright (c) vladkens
// https://github.com/vladkens/ecloop
// Licensed under the MIT License.

#pragma once

typedef unsigned int u32;
typedef unsigned char u8;

#if __has_builtin(__builtin_rotateleft32)
  #define rotl32(x, n) __builtin_rotateleft32(x, n)
#else
  #define rotl32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#endif

#if __has_builtin(__builtin_bswap32)
  #define swap32(x) __builtin_bswap32(x)
#else
  #define swap32(x) ((x) << 24) | ((x) << 8 & 0x00ff0000) | ((x) >> 8 & 0x0000ff00) | ((x) >> 24)
#endif

static const u8 _n[80] = {
    0, 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, //
    7, 4,  13, 1,  10, 6,  15, 3,  12, 0, 9,  5,  2,  14, 11, 8,  //
    3, 10, 14, 4,  9,  15, 8,  1,  2,  7, 0,  6,  13, 11, 5,  12, //
    1, 9,  11, 10, 0,  8,  12, 4,  13, 3, 7,  15, 14, 5,  6,  2,  //
    4, 0,  5,  9,  7,  12, 2,  10, 14, 1, 3,  8,  11, 6,  15, 13, //
};

static const u8 _r[80] = {
    11, 14, 15, 12, 5,  8,  7,  9,  11, 13, 14, 15, 6,  7,  9,  8,  //
    7,  6,  8,  13, 11, 9,  7,  15, 7,  12, 15, 9,  11, 7,  13, 12, //
    11, 13, 6,  7,  14, 9,  13, 15, 14, 8,  13, 6,  5,  12, 7,  5,  //
    11, 12, 14, 15, 14, 15, 9,  8,  9,  14, 5,  6,  8,  6,  5,  12, //
    9,  15, 5,  11, 6,  8,  13, 12, 5,  12, 13, 14, 11, 8,  5,  6,  //
};

static const u8 n_[80] = {
    5,  14, 7,  0, 9, 2,  11, 4,  13, 6,  15, 8,  1,  10, 3,  12, //
    6,  11, 3,  7, 0, 13, 5,  10, 14, 15, 8,  12, 4,  9,  1,  2,  //
    15, 5,  1,  3, 7, 14, 6,  9,  11, 8,  12, 2,  10, 0,  4,  13, //
    8,  6,  4,  1, 3, 11, 15, 0,  5,  12, 2,  13, 9,  7,  10, 14, //
    12, 15, 10, 4, 1, 5,  8,  7,  6,  2,  13, 14, 0,  3,  9,  11, //
};

static const u8 r_[80] = {
    8,  9,  9,  11, 13, 15, 15, 5,  7,  7,  8,  11, 14, 14, 12, 6,  //
    9,  13, 15, 7,  12, 8,  9,  11, 7,  7,  12, 7,  6,  15, 13, 11, //
    9,  7,  15, 11, 8,  6,  6,  14, 12, 13, 5,  14, 13, 13, 7,  5,  //
    15, 5,  8,  11, 14, 14, 6,  14, 6,  9,  12, 9,  12, 5,  15, 8,  //
    8,  5,  12, 9,  12, 5,  14, 6,  8,  13, 6,  5,  15, 13, 11, 11, //
};

#define F1(x, y, z) ((x) ^ (y) ^ (z))
#define F2(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define F3(x, y, z) (((x) | ~(y)) ^ (z))
#define F4(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define F5(x, y, z) ((x) ^ ((y) | ~(z)))

void rmd160_final(u32 state[5], const u32 x[16]) {
  u32 a1, b1, c1, d1, e1, a2, b2, c2, d2, e2;
  state[0] = a1 = a2 = 0x67452301;
  state[1] = b1 = b2 = 0xefcdab89;
  state[2] = c1 = c2 = 0x98badcfe;
  state[3] = d1 = d2 = 0x10325476;
  state[4] = e1 = e2 = 0xc3d2e1f0;

  u32 alpha, beta;
  u32 i = 0;

  // round 1
  for (; i < 16; ++i) {
    alpha = a1 + F1(b1, c1, d1) + x[_n[i]];
    alpha = rotl32(alpha, _r[i]) + e1;
    beta = rotl32(c1, 10);
    a1 = e1, c1 = b1, e1 = d1, b1 = alpha, d1 = beta;

    alpha = a2 + F5(b2, c2, d2) + x[n_[i]] + 0x50a28be6;
    alpha = rotl32(alpha, r_[i]) + e2;
    beta = rotl32(c2, 10);
    a2 = e2, c2 = b2, e2 = d2, b2 = alpha, d2 = beta;
  }

  // round 2
  for (; i < 32; ++i) {
    alpha = a1 + F2(b1, c1, d1) + x[_n[i]] + 0x5a827999;
    alpha = rotl32(alpha, _r[i]) + e1;
    beta = rotl32(c1, 10);
    a1 = e1, c1 = b1, e1 = d1, b1 = alpha, d1 = beta;

    alpha = a2 + F4(b2, c2, d2) + x[n_[i]] + 0x5c4dd124;
    alpha = rotl32(alpha, r_[i]) + e2;
    beta = rotl32(c2, 10);
    a2 = e2, c2 = b2, e2 = d2, b2 = alpha, d2 = beta;
  }

  // round 3
  for (; i < 48; ++i) {
    alpha = a1 + F3(b1, c1, d1) + x[_n[i]] + 0x6ed9eba1;
    alpha = rotl32(alpha, _r[i]) + e1;
    beta = rotl32(c1, 10);
    a1 = e1, c1 = b1, e1 = d1, b1 = alpha, d1 = beta;

    alpha = a2 + F3(b2, c2, d2) + x[n_[i]] + 0x6d703ef3;
    alpha = rotl32(alpha, r_[i]) + e2;
    beta = rotl32(c2, 10);
    a2 = e2, c2 = b2, e2 = d2, b2 = alpha, d2 = beta;
  }

  // round 4
  for (; i < 64; ++i) {
    alpha = a1 + F4(b1, c1, d1) + x[_n[i]] + 0x8f1bbcdc;
    alpha = rotl32(alpha, _r[i]) + e1;
    beta = rotl32(c1, 10);
    a1 = e1, c1 = b1, e1 = d1, b1 = alpha, d1 = beta;

    alpha = a2 + F2(b2, c2, d2) + x[n_[i]] + 0x7a6d76e9;
    alpha = rotl32(alpha, r_[i]) + e2;
    beta = rotl32(c2, 10);
    a2 = e2, c2 = b2, e2 = d2, b2 = alpha, d2 = beta;
  }

  // round 5
  for (; i < 80; ++i) {
    alpha = a1 + F5(b1, c1, d1) + x[_n[i]] + 0xa953fd4e;
    alpha = rotl32(alpha, _r[i]) + e1;
    beta = rotl32(c1, 10);
    a1 = e1, c1 = b1, e1 = d1, b1 = alpha, d1 = beta;

    alpha = a2 + F1(b2, c2, d2) + x[n_[i]];
    alpha = rotl32(alpha, r_[i]) + e2;
    beta = rotl32(c2, 10);
    a2 = e2, c2 = b2, e2 = d2, b2 = alpha, d2 = beta;
  }

  d2 += c1 + state[1];
  state[1] = state[2] + d1 + e2;
  state[2] = state[3] + e1 + a2;
  state[3] = state[4] + a1 + b2;
  state[4] = state[0] + b1 + c2;
  state[0] = d2;

  for (int i = 0; i < 5; ++i) state[i] = swap32(state[i]);
}
