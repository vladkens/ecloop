// Copyright (c) vladkens
// https://github.com/vladkens/ecloop
// Licensed under the MIT License.

#pragma once
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.c"
#define GLOBAL static const

INLINE u64 umul128(const u64 a, const u64 b, u64 *hi) {
  // https://stackoverflow.com/a/50958815
  // https://botan.randombit.net/doxygen/mul128_8h_source.html
  u128 t = (u128)a * b;
  *hi = t >> 64;
  return t;
}

// MARK: Field Element
typedef u64 fe[4];    // 256bit as 4x64bit (a0 + a1*2^64 + a2*2^128 + a3*2^192)
typedef u64 fe320[5]; // 320bit as 5x64bit (a0 + a1*2^64 + a2*2^128 + a3*2^192 + a4*2^256)

GLOBAL fe FE_ZERO = {0, 0, 0, 0};

// Secp256k1 prime field (2^256 - 2^32 - 977) and order
GLOBAL fe FE_P = {0xfffffffefffffc2f, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff};
GLOBAL fe FE_N = {0xbfd25e8cd0364141, 0xbaaedce6af48a03b, 0xfffffffffffffffe, 0xffffffffffffffff};

// endomorphism constants (alpha, alpha^2, beta, beta^2)
GLOBAL fe A1 = {0xdf02967c1b23bd72, 0x122e22ea20816678, 0xa5261c028812645a, 0x5363ad4cc05c30e0};
GLOBAL fe A2 = {0xe0cfc810b51283ce, 0xa880b9fc8ec739c2, 0x5ad9e3fd77ed9ba4, 0xac9c52b33fa3cf1f};
GLOBAL fe B1 = {0xc1396c28719501ee, 0x9cf0497512f58995, 0x6e64479eac3434e9, 0x7ae96a2b657c0710};
GLOBAL fe B2 = {0x3ec693d68e6afa40, 0x630fb68aed0a766a, 0x919bb86153cbcb16, 0x851695d49a83f8ef};

INLINE void fe_print(const char *label, const fe a) {
  printf("%s: %016llx %016llx %016llx %016llx\n", label, a[3], a[2], a[1], a[0]);
}

INLINE bool fe_iszero(const fe r) { return r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0; }
INLINE void fe_clone(fe r, const fe a) { memcpy(r, a, sizeof(fe)); }
INLINE void fe_set64(fe r, const u64 a) {
  memset(r, 0, sizeof(fe));
  r[0] = a;
}

size_t fe_bitlen(const fe a) {
  for (int i = 3; i >= 0; --i) {
    if (a[i]) return 64 * i + (64 - __builtin_clzll(a[i]));
  }
  return 0;
}

void fe_add64(fe r, const u64 a) {
  u64 c = 0;
  r[0] = addc64(r[0], a, 0, &c);
  r[1] = addc64(r[1], 0, c, &c);
  r[2] = addc64(r[2], 0, c, &c);
  r[3] = addc64(r[3], 0, c, &c);
}

int fe_cmp64(const fe a, const u64 b) {
  if (a[3] != 0 || a[2] != 0 || a[1] != 0) return 1;
  if (a[0] != b) return a[0] > b ? 1 : -1;
  return 0;
}

int fe_cmp(const fe a, const fe b) {
  if (a[3] != b[3]) return a[3] > b[3] ? 1 : -1;
  if (a[2] != b[2]) return a[2] > b[2] ? 1 : -1;
  if (a[1] != b[1]) return a[1] > b[1] ? 1 : -1;
  if (a[0] != b[0]) return a[0] > b[0] ? 1 : -1;
  return 0;
}

void fe_from_hex(fe r, const char *hex) {
  // load dynamic length hex string into 256bit integer (from right to left)
  fe_set64(r, 0);

  int cnt = 0, len = strlen(hex);
  while (len-- > 0) {
    u64 v = tolower(hex[len]);
    if (v >= '0' && v <= '9') v = v - '0';
    else if (v >= 'a' && v <= 'f') v = v - 'a' + 10;
    else continue;

    r[cnt / 16] = (v << (cnt * 4 % 64)) | r[cnt / 16];
    cnt += 1;
  }
}

INLINE void fe_shiftl(fe r, const u8 n) {
  if (n == 0) return;

  u8 s = n / 64;
  u8 rem = n % 64;

  for (int i = 3; i >= 0; --i) r[i] = i >= s ? r[i - s] : 0;
  if (rem == 0) return;

  u128 carry = 0;
  for (int i = 0; i < 4; ++i) {
    // todo: use umul128
    u128 val = ((u128)r[i]) << rem;
    r[i] = (u64)(val | carry);
    carry = val >> 64;
  }
}

INLINE void fe_shiftr64(fe r, const u8 n) {
  assert(n < 64);
  r[0] = (r[0] >> n) | (r[1] << (64 - n));
  r[1] = (r[1] >> n) | (r[2] << (64 - n));
  r[2] = (r[2] >> n) | (r[3] << (64 - n));
  r[3] = (r[3] >> n);
}

// MARK: 320bit helpers

void fe_mul_scalar(fe320 r, const fe a, const u64 b) { // 256bit * 64bit -> 320bit
  u64 h1, h2, c = 0;
  r[0] = umul128(a[0], b, &h1);
  r[1] = addc64(umul128(a[1], b, &h2), h1, c, &c);
  r[2] = addc64(umul128(a[2], b, &h1), h2, c, &c);
  r[3] = addc64(umul128(a[3], b, &h2), h1, c, &c);
  r[4] = addc64(0, h2, c, &c);
}

u64 fe320_addc(fe320 r, const fe320 a, const fe320 b) {
  u64 c = 0;
  r[0] = addc64(a[0], b[0], c, &c);
  r[1] = addc64(a[1], b[1], c, &c);
  r[2] = addc64(a[2], b[2], c, &c);
  r[3] = addc64(a[3], b[3], c, &c);
  r[4] = addc64(a[4], b[4], c, &c);
  return c;
}

u64 fe320_subc(fe320 r, const fe320 a, const fe320 b) {
  u64 c = 0;
  r[0] = subc64(a[0], b[0], c, &c);
  r[1] = subc64(a[1], b[1], c, &c);
  r[2] = subc64(a[2], b[2], c, &c);
  r[3] = subc64(a[3], b[3], c, &c);
  r[4] = subc64(a[4], b[4], c, &c);
  return c;
}

void fe320_add_shift(fe320 r, const fe320 a, const fe320 b, u64 ch) {
  u64 c = 0;
  addc64(a[0], b[0], c, &c); // keep only carry
  r[0] = addc64(a[1], b[1], c, &c);
  r[1] = addc64(a[2], b[2], c, &c);
  r[2] = addc64(a[3], b[3], c, &c);
  r[3] = addc64(a[4], b[4], c, &c);
  r[4] = c + ch;
}

// MARK: Modulo N arithmetic

void fe_modn_neg(fe r, const fe a) { // r = -a (mod N)
  u64 c = 0;
  r[0] = subc64(FE_N[0], a[0], c, &c);
  r[1] = subc64(FE_N[1], a[1], c, &c);
  r[2] = subc64(FE_N[2], a[2], c, &c);
  r[3] = subc64(FE_N[3], a[3], c, &c);
}

void fe_modn_add(fe r, const fe a, const fe b) { // r = a + b (mod N)
  u64 c = 0;
  r[0] = addc64(a[0], b[0], c, &c);
  r[1] = addc64(a[1], b[1], c, &c);
  r[2] = addc64(a[2], b[2], c, &c);
  r[3] = addc64(a[3], b[3], c, &c);

  if (c) {
    r[0] = subc64(r[0], FE_N[0], 0, &c);
    r[1] = subc64(r[1], FE_N[1], c, &c);
    r[2] = subc64(r[2], FE_N[2], c, &c);
    r[3] = subc64(r[3], FE_N[3], c, &c);
  }
}

void fe_modn_sub(fe r, const fe a, const fe b) { // r = a - b (mod N)
  u64 c = 0;
  r[0] = subc64(a[0], b[0], c, &c);
  r[1] = subc64(a[1], b[1], c, &c);
  r[2] = subc64(a[2], b[2], c, &c);
  r[3] = subc64(a[3], b[3], c, &c);

  if (c) {
    r[0] = addc64(r[0], FE_N[0], 0, &c);
    r[1] = addc64(r[1], FE_N[1], c, &c);
    r[2] = addc64(r[2], FE_N[2], c, &c);
    r[3] = addc64(r[3], FE_N[3], c, &c);
  }
}

// clang-format off
GLOBAL fe320 _NN = {0xbfd25e8cd0364141, 0xbaaedce6af48a03b, 0xfffffffffffffffe, 0xffffffffffffffff, 0x0};
GLOBAL fe320 _R2 = {0x896cf21467d7d140, 0x741496c20e7cf878, 0xe697f5e45bcd07c6, 0x9d671cd581c69bc5, 0x0};
GLOBAL u64 _MM64o = 0x4b0dff665588b13f; // 64bits lsb negative inverse of secp256k1 order
// clang-format on

// https://github.com/albertobsd/keyhunt/blob/main/secp256k1/IntMod.cpp#L1109
void fe_modn_mul(fe r, const fe a, const fe b) {
  fe320 t = {0}, pr = {0}, p = {0}, rr = {0};
  u64 ml, c;

  fe_mul_scalar(pr, a, b[0]); // pr = a * b[0]
  ml = pr[0] * _MM64o;
  fe_mul_scalar(p, _NN, ml); // p = N * ML
  c = fe320_addc(pr, pr, p); // r = pr + p
  memcpy(t, pr + 1, 32);
  t[4] = c;

  for (int i = 1; i < 4; ++i) {
    fe_mul_scalar(pr, a, b[i]); // pr = a * b[i]
    ml = (pr[0] + t[0]) * _MM64o;
    fe_mul_scalar(p, _NN, ml); // p = N * ML
    c = fe320_addc(pr, pr, p); // r = pr + p
    fe320_add_shift(t, t, pr, c);
  }

  fe320_subc(p, t, _NN); // p = t - N
  (int64_t)p[4] >= 0 ? memcpy(rr, p, 40) : memcpy(rr, t, 40);

  // normalize
  fe_mul_scalar(pr, _R2, rr[0]); // pr = rr * R2
  ml = pr[0] * _MM64o;
  fe_mul_scalar(p, _NN, ml); // p = N * ML
  c = fe320_addc(pr, pr, p); // r = pr + p
  memcpy(t, pr + 1, 32);
  t[4] = c;

  for (int i = 1; i < 4; ++i) {
    fe_mul_scalar(pr, _R2, rr[i]); // pr = rr * R2
    ml = (pr[0] + t[0]) * _MM64o;
    fe_mul_scalar(p, _NN, ml); // p = N * ML
    c = fe320_addc(pr, pr, p); // r = pr + p
    fe320_add_shift(t, t, pr, c);
  }

  fe320_subc(p, t, _NN); // p = t - N
  (int64_t)p[4] >= 0 ? memcpy(rr, p, 40) : memcpy(rr, t, 40);

  fe_clone(r, rr);
}

void fe_modn_add_stride(fe r, const fe base, const fe stride, const u64 offset) {
  fe t; // in case r and base are same pointer
  fe_set64(t, offset);
  fe_modn_mul(t, t, stride); // r = offset * stride
  fe_modn_add(r, t, base);   // r = offset * stride + base
}

void fe_modn_from_hex(fe r, const char *hex) {
  fe_from_hex(r, hex);
  if (fe_cmp(r, FE_N) >= 0) fe_modn_sub(r, r, FE_N);
}

// MARK: Modulo P arithmetic

void fe_modp_neg(fe r, const fe a) { // r = -a (mod P)
  u64 c = 0;
  r[0] = subc64(FE_P[0], a[0], c, &c);
  r[1] = subc64(FE_P[1], a[1], c, &c);
  r[2] = subc64(FE_P[2], a[2], c, &c);
  r[3] = subc64(FE_P[3], a[3], c, &c);
}

void fe_modp_sub(fe r, const fe a, const fe b) { // r = a - b (mod P)
  u64 c = 0;
  r[0] = subc64(a[0], b[0], c, &c);
  r[1] = subc64(a[1], b[1], c, &c);
  r[2] = subc64(a[2], b[2], c, &c);
  r[3] = subc64(a[3], b[3], c, &c);

  if (c) {
    r[0] = addc64(r[0], FE_P[0], 0, &c);
    r[1] = addc64(r[1], FE_P[1], c, &c);
    r[2] = addc64(r[2], FE_P[2], c, &c);
    r[3] = addc64(r[3], FE_P[3], c, &c);
  }
}

void fe_modp_add(fe r, const fe a, const fe b) { // r = a + b (mod P)
  u64 c = 0;
  r[0] = addc64(a[0], b[0], c, &c);
  r[1] = addc64(a[1], b[1], c, &c);
  r[2] = addc64(a[2], b[2], c, &c);
  r[3] = addc64(a[3], b[3], c, &c);

  if (c) {
    r[0] = subc64(r[0], FE_P[0], 0, &c);
    r[1] = subc64(r[1], FE_P[1], c, &c);
    r[2] = subc64(r[2], FE_P[2], c, &c);
    r[3] = subc64(r[3], FE_P[3], c, &c);
  }
}

void fe_modp_mul(fe r, const fe a, const fe b) {
  u64 rr[8] = {0}, tt[5] = {0}, c = 0;

  // 256bit * 256bit -> 512bit
  fe_mul_scalar(rr, a, b[0]);
  fe_mul_scalar(tt, a, b[1]);
  rr[1] = addc64(rr[1], tt[0], c, &c);
  rr[2] = addc64(rr[2], tt[1], c, &c);
  rr[3] = addc64(rr[3], tt[2], c, &c);
  rr[4] = addc64(rr[4], tt[3], c, &c);
  rr[5] = addc64(rr[5], tt[4], c, &c);
  fe_mul_scalar(tt, a, b[2]);
  rr[2] = addc64(rr[2], tt[0], c, &c);
  rr[3] = addc64(rr[3], tt[1], c, &c);
  rr[4] = addc64(rr[4], tt[2], c, &c);
  rr[5] = addc64(rr[5], tt[3], c, &c);
  rr[6] = addc64(rr[6], tt[4], c, &c);
  fe_mul_scalar(tt, a, b[3]);
  rr[3] = addc64(rr[3], tt[0], c, &c);
  rr[4] = addc64(rr[4], tt[1], c, &c);
  rr[5] = addc64(rr[5], tt[2], c, &c);
  rr[6] = addc64(rr[6], tt[3], c, &c);
  rr[7] = addc64(rr[7], tt[4], c, &c);

  // reduce 512bit -> 320bit
  fe_mul_scalar(tt, rr + 4, 0x1000003D1ULL);
  rr[0] = addc64(rr[0], tt[0], 0, &c);
  rr[1] = addc64(rr[1], tt[1], c, &c);
  rr[2] = addc64(rr[2], tt[2], c, &c);
  rr[3] = addc64(rr[3], tt[3], c, &c);

  // reduce 320bit -> 256bit
  u64 hi, lo;
  lo = umul128(tt[4] + c, 0x1000003D1ULL, &hi);
  r[0] = addc64(rr[0], lo, 0, &c);
  r[1] = addc64(rr[1], hi, c, &c);
  r[2] = addc64(rr[2], 0, c, &c);
  r[3] = addc64(rr[3], 0, c, &c);

  if (fe_cmp(r, FE_P) >= 0) fe_modp_sub(r, r, FE_P);
}

void fe_modp_sqr(fe r, const fe a) {
  // from: https://github.com/JeanLucPons/VanitySearch/blob/1.19/IntMod.cpp#L1034
  // ~8% faster than fe_modmul(r, a, a)
  // return fe_modmul(r, a, a);

  // k=0
  u64 rr[8] = {0}, tt[5] = {0}, c = 0, t1, t2, lo, hi;
  rr[0] = umul128(a[0], a[0], &tt[1]);

  // k=1
  tt[3] = umul128(a[0], a[1], &tt[4]);
  tt[3] = addc64(tt[3], tt[3], 0, &c);
  tt[4] = addc64(tt[4], tt[4], c, &c);
  t1 = c;
  tt[3] = addc64(tt[1], tt[3], 0, &c);
  tt[4] = addc64(tt[4], 0, c, &c);
  t1 += c;
  rr[1] = tt[3];

  // k=2
  tt[0] = umul128(a[0], a[2], &tt[1]);
  tt[0] = addc64(tt[0], tt[0], 0, &c);
  tt[1] = addc64(tt[1], tt[1], c, &c);
  t2 = c;
  lo = umul128(a[1], a[1], &hi);
  tt[0] = addc64(tt[0], lo, 0, &c);
  tt[1] = addc64(tt[1], hi, c, &c);
  t2 += c;
  tt[0] = addc64(tt[0], tt[4], 0, &c);
  tt[1] = addc64(tt[1], t1, c, &c);
  t2 += c;
  rr[2] = tt[0];

  // k=3
  tt[3] = umul128(a[0], a[3], &tt[4]);
  lo = umul128(a[1], a[2], &hi);
  tt[3] = addc64(tt[3], lo, 0, &c);
  tt[4] = addc64(tt[4], hi, c, &c);
  t1 = c + c;
  tt[3] = addc64(tt[3], tt[3], 0, &c);
  tt[4] = addc64(tt[4], tt[4], c, &c);
  t1 += c;
  tt[3] = addc64(tt[1], tt[3], 0, &c);
  tt[4] = addc64(tt[4], t2, c, &c);
  t1 += c;
  rr[3] = tt[3];

  // k=4
  tt[0] = umul128(a[1], a[3], &tt[1]);
  tt[0] = addc64(tt[0], tt[0], 0, &c);
  tt[1] = addc64(tt[1], tt[1], c, &c);
  t2 = c;
  lo = umul128(a[2], a[2], &hi);
  tt[0] = addc64(tt[0], lo, 0, &c);
  tt[1] = addc64(tt[1], hi, c, &c);
  t2 += c;
  tt[0] = addc64(tt[0], tt[4], 0, &c);
  tt[1] = addc64(tt[1], t1, c, &c);
  t2 += c;
  rr[4] = tt[0];

  // k=5
  tt[3] = umul128(a[2], a[3], &tt[4]);
  tt[3] = addc64(tt[3], tt[3], 0, &c);
  tt[4] = addc64(tt[4], tt[4], c, &c);
  t1 = c;
  tt[3] = addc64(tt[3], tt[1], 0, &c);
  tt[4] = addc64(tt[4], t2, c, &c);
  t1 += c;
  rr[5] = tt[3];

  // k=6
  tt[0] = umul128(a[3], a[3], &tt[1]);
  tt[0] = addc64(tt[0], tt[4], 0, &c);
  tt[1] = addc64(tt[1], t1, c, &c);
  rr[6] = tt[0];

  // k=7
  rr[7] = tt[1];

  // reduce 512bit -> 320bit
  fe_mul_scalar(tt, rr + 4, 0x1000003D1ULL);
  rr[0] = addc64(rr[0], tt[0], 0, &c);
  rr[1] = addc64(rr[1], tt[1], c, &c);
  rr[2] = addc64(rr[2], tt[2], c, &c);
  rr[3] = addc64(rr[3], tt[3], c, &c);

  // reduce 320bit -> 256bit
  lo = umul128(tt[4] + c, 0x1000003D1ULL, &hi);
  r[0] = addc64(rr[0], lo, 0, &c);
  r[1] = addc64(rr[1], hi, c, &c);
  r[2] = addc64(rr[2], 0, c, &c);
  r[3] = addc64(rr[3], 0, c, &c);

  if (fe_cmp(r, FE_P) >= 0) fe_modp_sub(r, r, FE_P);
}

void _fe_modp_inv_binpow(fe r, const fe a) {
  // a^(P-2) = a^-1 (mod P)
  // https://e-maxx.ru/algo/reverse_element https://e-maxx.ru/algo/binary_pow
  fe q = {1}, p, t;
  fe_clone(p, FE_P);
  fe_clone(t, a);
  p[0] -= 2;

  while (p[0] || p[1] || p[2] || p[3]) {       // p > 0
    if ((a[0] & 1) != 0) fe_modp_mul(q, q, t); // when odd
    fe_modp_sqr(t, t);
    fe_shiftr64(p, 1);
  }

  fe_clone(r, q);
}

void _fe_modp_inv_addchn(fe r, const fe a) {
  // https://briansmith.org/ecc-inversion-addition-chains-01#secp256k1_field_inversion
  fe x2, x3, x6, x9, x11, x22, x44, x88, x176, x220, x223, t1;
  fe_modp_sqr(x2, a);
  fe_modp_mul(x2, x2, a);

  fe_clone(x3, x2);
  fe_modp_sqr(x3, x2);
  fe_modp_mul(x3, x3, a);

  fe_clone(x6, x3);
  for (int j = 0; j < 3; j++) fe_modp_sqr(x6, x6);
  fe_modp_mul(x6, x6, x3);

  fe_clone(x9, x6);
  for (int j = 0; j < 3; j++) fe_modp_sqr(x9, x9);
  fe_modp_mul(x9, x9, x3);

  fe_clone(x11, x9);
  for (int j = 0; j < 2; j++) fe_modp_sqr(x11, x11);
  fe_modp_mul(x11, x11, x2);

  fe_clone(x22, x11);
  for (int j = 0; j < 11; j++) fe_modp_sqr(x22, x22);
  fe_modp_mul(x22, x22, x11);

  fe_clone(x44, x22);
  for (int j = 0; j < 22; j++) fe_modp_sqr(x44, x44);
  fe_modp_mul(x44, x44, x22);

  fe_clone(x88, x44);
  for (int j = 0; j < 44; j++) fe_modp_sqr(x88, x88);
  fe_modp_mul(x88, x88, x44);

  fe_clone(x176, x88);
  for (int j = 0; j < 88; j++) fe_modp_sqr(x176, x176);
  fe_modp_mul(x176, x176, x88);

  fe_clone(x220, x176);
  for (int j = 0; j < 44; j++) fe_modp_sqr(x220, x220);
  fe_modp_mul(x220, x220, x44);

  fe_clone(x223, x220);
  for (int j = 0; j < 3; j++) fe_modp_sqr(x223, x223);
  fe_modp_mul(x223, x223, x3);

  fe_clone(t1, x223);
  for (int j = 0; j < 23; j++) fe_modp_sqr(t1, t1);
  fe_modp_mul(t1, t1, x22);
  for (int j = 0; j < 5; j++) fe_modp_sqr(t1, t1);
  fe_modp_mul(t1, t1, a);
  for (int j = 0; j < 3; j++) fe_modp_sqr(t1, t1);
  fe_modp_mul(t1, t1, x2);
  for (int j = 0; j < 2; j++) fe_modp_sqr(t1, t1);
  fe_modp_mul(r, t1, a);
}

INLINE void fe_modp_inv(fe r, const fe a) { return _fe_modp_inv_addchn(r, a); }

void fe_modp_grpinv(fe r[], const u32 n) {
  fe *zs = (fe *)malloc(n * sizeof(fe));

  fe_clone(*zs, r[0]);
  for (u32 i = 1; i < n; ++i) fe_modp_mul(*(zs + i), *(zs + (i - 1)), r[i]);

  fe t1, t2;
  fe_clone(t1, *(zs + (n - 1)));
  fe_modp_inv(t1, t1);

  for (u32 i = n - 1; i > 0; --i) {
    fe_modp_mul(t2, t1, *(zs + (i - 1)));
    fe_modp_mul(t1, r[i], t1);
    fe_clone(r[i], t2);
  }

  fe_clone(r[0], t1);
  free(zs);
}

// MARK: EC Point
// https://eprint.iacr.org/2015/1060.pdf
// https://hyperelliptic.org/EFD/g1p/auto-shortw.html

typedef struct pe {
  fe x, y, z;
} pe;

GLOBAL pe G1 = {
    .x = {0x59f2815b16f81798, 0x029bfcdb2dce28d9, 0x55a06295ce870b07, 0x79be667ef9dcbbac},
    .y = {0x9c47d08ffb10d4b8, 0xfd17b448a6855419, 0x5da4fbfc0e1108a8, 0x483ada7726a3c465},
    .z = {0x1, 0x0, 0x0, 0x0},
};

GLOBAL pe G2 = {
    .x = {0xabac09b95c709ee5, 0x5c778e4b8cef3ca7, 0x3045406e95c07cd8, 0xc6047f9441ed7d6d},
    .y = {0x236431a950cfe52a, 0xf7f632653266d0e1, 0xa3c58419466ceaee, 0x1ae168fea63dc339},
    .z = {0x1, 0x0, 0x0, 0x0},
};

INLINE void pe_clone(pe *r, const pe *a) {
  memcpy(r, a, sizeof(pe));
  // fe_clone(r->x, a->x);
  // fe_clone(r->y, a->y);
  // fe_clone(r->z, a->z);
}

// https://en.wikibooks.org/wiki/Cryptography/Prime_Curve/Affine_Coordinates

void ec_affine_dbl(pe *r, const pe *p) {
  // λ = (3 * x^2) / (2 * y)
  // x = λ^2 - 2 * x
  // y = λ * (x1 - x) - y1
  fe t1, t2, t3;
  fe_modp_sqr(t1, p->x);       // x^2
  fe_modp_add(t2, t1, t1);     // 2 * x^2
  fe_modp_add(t2, t2, t1);     // 3 * x^2
  fe_modp_add(t1, p->y, p->y); // 2 * y
  fe_modp_inv(t1, t1);         //
  fe_modp_mul(t1, t2, t1);     // λ = (3 * x^2) / (2 * y)
  fe_modp_sqr(t3, t1);         // λ^2
  fe_modp_sub(t3, t3, p->x);   // λ^2 - x1
  fe_modp_sub(t3, t3, p->x);   // λ^2 - x1 - x1
  fe_modp_sub(t2, p->x, t3);   // x1 - x
  fe_modp_mul(t2, t1, t2);     // λ * (x1 - x)
  fe_modp_sub(r->y, t2, p->y); // λ * (x1 - x) - y1
  fe_clone(r->x, t3);
}

void ec_affine_add(pe *r, const pe *p, const pe *q) {
  // λ = (y1 - y2) / (x1 - x2)
  // x = λ^2 - x1 - x2
  // y = λ * (x1 - x) - y1
  fe t1, t2, t3, t4;
  fe_modp_sub(t1, p->y, q->y); // y1 - y2
  fe_modp_sub(t2, p->x, q->x); // x1 - x2
  fe_modp_inv(t2, t2);         //
  fe_modp_mul(t1, t1, t2);     // λ = (y1 - y2) / (x1 - x2)
  fe_modp_sqr(t3, t1);         // λ^2
  fe_modp_sub(t3, t3, p->x);   // λ^2 - x1
  fe_modp_sub(t3, t3, q->x);   // λ^2 - x1 - x2
  fe_modp_sub(t4, p->x, t3);   // x1 - x
  fe_modp_mul(t4, t1, t4);     // λ * (x1 - x)
  fe_modp_sub(r->y, t4, p->y); // λ * (x1 - x) - y1
  fe_clone(r->x, t3);
}

// https://en.wikibooks.org/wiki/Cryptography/Prime_Curve/Standard_Projective_Coordinates

void _ec_jacobi_dbl1(pe *r, const pe *p) {
  // W = a*Z^2 + 3*X^2
  // S = Y*Z
  // B = X*Y*S
  // H = W^2 - 8*B
  // X' = 2*H*S
  // Y' = W*(4*B - H) - 8*Y^2*S^2
  // Z' = 8*S^3
  fe w, s, b, h, t;
  fe_modp_sqr(t, p->x);          // X^2
  fe_modp_add(w, t, t);          // 2*X^2
  fe_modp_add(w, w, t);          // 3*X^2
  fe_modp_mul(s, p->y, p->z);    // Y*Z
  fe_modp_mul(b, p->x, p->y);    // X*Y
  fe_modp_mul(b, b, s);          // X*Y*S
  fe_modp_add(b, b, b);          // 2*B
  fe_modp_add(b, b, b);          // 4*B
  fe_modp_add(t, b, b);          // 8*B
  fe_modp_sqr(h, w);             // W^2
  fe_modp_sub(h, h, t);          // W^2 - 8*B
  fe_modp_mul(r->x, h, s);       // H*S
  fe_modp_add(r->x, r->x, r->x); // 2*H*S
  fe_modp_sub(t, b, h);          // 4*B - H
  fe_modp_mul(t, w, t);          // W*(4*B - H)
  fe_modp_sqr(r->y, p->y);       // Y^2
  fe_modp_sqr(h, s);             // S^2
  fe_modp_mul(r->y, r->y, h);    // Y^2*S^2
  fe_modp_add(r->y, r->y, r->y); // 2*Y^2*S^2
  fe_modp_add(r->y, r->y, r->y); // 4*Y^2*S^2
  fe_modp_add(r->y, r->y, r->y); // 8*Y^2*S^2
  fe_modp_sub(r->y, t, r->y);    // W*(4*B - H) - 8*Y^2*S^2
  fe_modp_mul(r->z, h, s);       // S^3
  fe_modp_add(r->z, r->z, r->z); // 2*S^3
  fe_modp_add(r->z, r->z, r->z); // 4*S^3
  fe_modp_add(r->z, r->z, r->z); // 8*S^3
}

void _ec_jacobi_add1(pe *r, const pe *p, const pe *q) {
  // u1 = qy * pz
  // u2 = py * qz
  // v1 = qx * pz
  // v2 = px * qz
  // if (v1 == v2) return u1 != u2 ? POINT_AT_INFINITY : POINT_DOUBLE(px, py, pz)
  // u = u1 - u2
  // v = v1 - v2
  // w = pz * qz
  // a = u^2 * w - v^3 - 2 * v^2 * v2
  // x3 = v * a
  // y3 = u * (v^2 * v2 - a) - v^3 * u2
  // z3 = v^3 * w
  fe u2, v2, u, v, w, a, vs, vc;
  fe_modp_mul(u2, p->y, q->z); // u2 = py * qz
  fe_modp_mul(v2, p->x, q->z); // v2 = px * qz
  fe_modp_mul(u, q->y, p->z);  // u1 = qy * pz
  fe_modp_mul(v, q->x, p->z);  // v1 = qx * pz
  assert(fe_cmp(v, v2) != 0);  // if (v1 == v2) return
  fe_modp_mul(w, p->z, q->z);  // w = pz * qz
  fe_modp_sub(u, u, u2);       // u = u1 - u2
  fe_modp_sub(v, v, v2);       // v = v1 - v2
  fe_modp_sqr(vs, v);          // v^2
  fe_modp_mul(vc, vs, v);      // v^3
  fe_modp_mul(vs, vs, v2);     // v^2 * v2
  fe_modp_mul(r->z, vc, w);    // z3 = v^3 * w
  fe_modp_sqr(a, u);           // u^2
  fe_modp_mul(a, a, w);        // u^2 * w
  fe_modp_add(w, vs, vs);      // 2 * v^2 * v2                        [w reused]
  fe_modp_sub(a, a, vc);       // u^2 * w - v^3
  fe_modp_sub(a, a, w);        // u^2 * w - v^3 - 2 * v^2 * v2
  fe_modp_mul(r->x, v, a);     // x3 = v * a
  fe_modp_sub(a, vs, a);       // v^2 * v2 - a                        [a reused]
  fe_modp_mul(a, a, u);        // u * (v^2 * v2 - a)                  [a reused]
  fe_modp_mul(u, vc, u2);      // v^3 * u2                            [u reused]
  fe_modp_sub(r->y, a, u);     // y3 = u * (v^2 * v2 - a) - v^3 * u2
}

void _ec_jacobi_rdc1(pe *r, const pe *a) {
  // reduce Standard Projective to Affine
  fe_clone(r->z, a->z);
  fe_modp_inv(r->z, r->z);
  fe_modp_mul(r->x, a->x, r->z);
  fe_modp_mul(r->y, a->y, r->z);
  fe_set64(r->z, 0x1);
}

void _ec_jacobi_grprdc1(pe r[], u64 n) {
  fe *zz = (fe *)malloc(n * sizeof(fe));
  for (u64 i = 0; i < n; ++i) fe_clone(zz[i], r[i].z);
  fe_modp_grpinv(zz, n);

  for (u64 i = 0; i < n; ++i) {
    fe_modp_mul(r[i].x, r[i].x, zz[i]);
    fe_modp_mul(r[i].y, r[i].y, zz[i]);
    fe_set64(r[i].z, 0x1);
  }

  free(zz);
}

// https://en.wikibooks.org/wiki/Cryptography/Prime_Curve/Jacobian_Coordinates

void _ec_jacobi_dbl2(pe *r, const pe *p) {
  // if (Y == 0) return POINT_AT_INFINITY
  // S = 4*X*Y^2
  // M = 3*X^2 + a*Z^4
  // X' = M^2 - 2*S
  // Y' = M*(S - X') - 8*Y^4
  // Z' = 2*Y*Z
  u64 s[4], m[4], t[4];
  fe_modp_mul(r->z, p->y, p->z); // Y*Z
  fe_modp_add(r->z, r->z, r->z); // 2*Y*Z
  fe_modp_sqr(t, p->y);          // Y^2
  fe_modp_mul(s, p->x, t);       // X*Y^2
  fe_modp_add(s, s, s);          // 2*X*Y^2
  fe_modp_add(s, s, s);          // 4*X*Y^2
  fe_modp_sqr(t, t);             // Y^4
  fe_modp_add(r->y, t, t);       // 2*Y^4
  fe_modp_add(t, r->y, r->y);    // 4*Y^4
  fe_modp_add(r->y, t, t);       // 8*Y^4
  fe_modp_sqr(t, p->x);          // X^2
  fe_modp_add(m, t, t);          // 2*X^2
  fe_modp_add(m, m, t);          // 3*X^2
  fe_modp_sqr(r->x, m);          // M^2
  fe_modp_add(t, s, s);          // 2*S
  fe_modp_sub(r->x, r->x, t);    // M^2 - 2*S
  fe_modp_sub(t, s, r->x);       // S - X'
  fe_modp_mul(t, m, t);          // M*(S - X')
  fe_modp_sub(r->y, t, r->y);    // M*(S - X') - 8*Y^4
}

void _ec_jacobi_add2(pe *r, const pe *p, const pe *q) {
  // U1 = px * qz ** 2
  // S1 = py * qz ** 3
  // U2 = qx * pz ** 2
  // S2 = qy * pz ** 3
  // if (U1 == U2) return S1 != S2 ? (0, 0, 1) : jacobian_double(p)
  // H = U2 - U1
  // R = S2 - S1
  // U1H2 = U1 * H ** 2
  // nx = R ** 2 - H ** 3 - 2 * U1H2
  // ny = R * (U1H2 - nx) - S1 * H ** 3
  // nz = H * pz * qz
  fe u1, u2, s1, s2, tt, ta;
  fe_modp_sqr(tt, q->z);         // qz ** 2
  fe_modp_mul(u1, p->x, tt);     // U1 = px * qz ** 2
  fe_modp_mul(ta, tt, q->z);     // qz ** 3
  fe_modp_mul(s1, p->y, ta);     // S1 = py * qz ** 3
  fe_modp_sqr(tt, p->z);         // pz ** 2
  fe_modp_mul(u2, q->x, tt);     // U2 = qx * pz ** 2
  assert(fe_cmp(u1, u2) != 0);   // if (U1 == U2) return
  fe_modp_mul(ta, tt, p->z);     // pz ** 3
  fe_modp_mul(s2, q->y, ta);     // S2 = qy * pz ** 3
  fe_modp_sub(u2, u2, u1);       // H = U2 - U1              [u2 reused]
  fe_modp_sub(s2, s2, s1);       // R = S2 - S1              [s2 reused]
  fe_modp_sqr(tt, u2);           // H ** 2
  fe_modp_mul(u1, u1, tt);       // U1H2 = U1 * H ** 2       [s1 reused]
  fe_modp_mul(tt, tt, u2);       // H ** 3
  fe_modp_add(ta, u1, u1);       // 2 * U1H2
  fe_modp_sqr(r->x, s2);         // R ** 2
  fe_modp_sub(r->x, r->x, tt);   // R ** 2 - H ** 3
  fe_modp_sub(r->x, r->x, ta);   // nx = R ** 2 - H ** 3 - 2 * U1H2
  fe_modp_mul(ta, tt, s1);       // S1 * H ** 3
  fe_modp_sub(r->y, u1, r->x);   // U1H2 - nx
  fe_modp_mul(r->y, r->y, s2);   // R * (U1H2 - nx)
  fe_modp_sub(r->y, r->y, ta);   // R * (U1H2 - nx) - S1 * H ** 3
  fe_modp_mul(r->z, p->z, q->z); // pz * qz
  fe_modp_mul(r->z, r->z, u2);   // H * pz * qz
}

void _ec_jacobi_rdc2(pe *r, const pe *a) {
  // reduce Jacobian to Affine
  fe t;
  fe_clone(r->z, a->z);
  fe_modp_inv(r->z, r->z);
  fe_modp_sqr(t, r->z);       // z ** 2
  fe_modp_mul(r->x, a->x, t); // x = x * z ** 2
  fe_modp_mul(t, t, r->z);    // z ** 3
  fe_modp_mul(r->y, a->y, t); // y = y * z ** 3
  fe_set64(r->z, 0x1);
}

void _ec_jacobi_grprdc2(pe r[], u64 n) {
  fe *zz = (fe *)malloc(n * sizeof(fe));
  for (u64 i = 0; i < n; ++i) fe_clone(zz[i], r[i].z);
  fe_modp_grpinv(zz, n);

  fe z = {0};
  for (u64 i = 0; i < n; ++i) {
    fe_modp_sqr(z, zz[i]);          // z^2
    fe_modp_mul(r[i].x, r[i].x, z); // x = x * z^2
    fe_modp_mul(z, z, zz[i]);       // z^3
    fe_modp_mul(r[i].y, r[i].y, z); // y = y * z^3
    fe_set64(r[i].z, 0x1);
  }

  free(zz);
}

// v1. add: ~6.6M it/s, dbl: ~5.6M it/s
// v2. add: ~5.4M it/s, dbl: ~7.8M it/s
// v1 is used because add operation is more frequent

INLINE void ec_jacobi_dbl(pe *r, const pe *p) { return _ec_jacobi_dbl1(r, p); }
INLINE void ec_jacobi_add(pe *r, const pe *p, const pe *q) { return _ec_jacobi_add1(r, p, q); }
INLINE void ec_jacobi_rdc(pe *r, const pe *a) { return _ec_jacobi_rdc1(r, a); }
INLINE void ec_jacobi_grprdc(pe r[], u64 n) { return _ec_jacobi_grprdc1(r, n); }
// INLINE void ec_jacobi_dbl(pe *r, const pe *p) { return _ec_jacobi_dbl2(r, p); }
// INLINE void ec_jacobi_add(pe *r, const pe *p, const pe *q) { return _ec_jacobi_add2(r, p, q); }
// INLINE void ec_jacobi_rdc(pe *r, const pe *a) { return _ec_jacobi_rdc2(r, a); }
// INLINE void ec_jacobi_grprdc(pe r[], u64 n) { return _ec_jacobi_grprdc2(r, n); }

void ec_jacobi_mul(pe *r, const pe *p, const fe k) {
  // double-and-add in Jacobian space
  pe t;
  pe_clone(&t, p);
  fe_set64(r->x, 0x0); // todo: for first iteration to avoid point of infinity
  fe_set64(r->y, 0x0);
  fe_set64(r->z, 0x1);

  u32 bits = fe_bitlen(k);
  for (u32 i = 0; i < bits; ++i) {
    if (k[i / 64] & (1ULL << (i % 64))) {
      // todo: remove if condition / here simplified check to point of infinity
      if (r->x[0] == 0 && r->y[0] == 0) {
        // printf("add(0): %d ~ %d\n", i, i / 64);
        pe_clone(r, &t);
      } else {
        // printf("add(1): %d ~ %d\n", i, i / 64);
        ec_jacobi_add(r, r, &t);
      }
    }
    ec_jacobi_dbl(&t, &t);
  }
}

INLINE void ec_jacobi_addrdc(pe *r, const pe *p, const pe *q) {
  ec_jacobi_add(r, p, q);
  ec_jacobi_rdc(r, r);
}

INLINE void ec_jacobi_mulrdc(pe *r, const pe *p, const fe k) {
  ec_jacobi_mul(r, p, k);
  ec_jacobi_rdc(r, r);
}

INLINE void ec_jacobi_dblrdc(pe *r, const pe *p) {
  ec_jacobi_dbl(r, p);
  ec_jacobi_rdc(r, r);
}

bool ec_verify(const pe *p) {
  // y^2 = x^3 + 7
  pe q, g;
  pe_clone(&q, p);
  ec_jacobi_rdc(&q, &q);

  pe_clone(&g, &q);
  fe_modp_sqr(g.y, g.y);      // y^2
  fe_modp_sqr(g.x, g.x);      // x^2
  fe_modp_mul(g.x, g.x, q.x); // x^3
  fe_modp_sub(g.y, g.y, g.x); // y^2 - x^3
  return g.y[0] == 7 && g.y[1] == 0 && g.y[2] == 0 && g.y[3] == 0;
}

// MARK: EC GTable

u64 _GTABLE_W = 14;
pe *_gtable = NULL; // GTable for precomputed points

// https://www.sav.sk/journals/uploads/0215094304C459.pdf (Algorithm 3)
size_t ec_gtable_init() {
  u64 n = 1 << _GTABLE_W;
  u64 d = ((256 - 1) / _GTABLE_W) + 1;
  u64 s = n * d - d;

  size_t mem_size = s * sizeof(pe);
  if (_gtable != NULL) free(_gtable);
  _gtable = (pe *)malloc(mem_size);

  pe b, p;
  pe_clone(&b, &G1);
  for (u64 i = 0; i < d; ++i) {
    u64 x = (n - 1) * i;
    pe_clone(&_gtable[x], &b);
    pe_clone(&p, &b);
    for (u64 j = 1; j < n - 1; ++j) {
      j == 1 ? ec_jacobi_dbl(&p, &p) : ec_jacobi_add(&p, &p, &b);
      x = (n - 1) * i + j;
      pe_clone(&_gtable[x], &p);
    }
    ec_jacobi_add(&b, &p, &b);
  }

  ec_jacobi_grprdc(_gtable, s);
  return mem_size;
}

void ec_gtable_mul(pe *r, const fe pk) {
  if (_gtable == NULL) {
    printf("GTable is not initialized\n");
    exit(1);
  }

  u64 n = 1 << _GTABLE_W;
  u64 d = ((256 - 1) / _GTABLE_W) + 1;
  pe q = {0};
  fe k;
  fe_clone(k, pk);

  for (u64 i = 0; i < d; ++i) {
    u64 b = k[0] & (n - 1);
    fe_shiftr64(k, _GTABLE_W);
    if (!b) continue;

    u64 x = (n - 1) * i + b - 1;
    fe_iszero(q.x) ? pe_clone(&q, &_gtable[x]) : ec_jacobi_add(&q, &q, &_gtable[x]);
  }

  pe_clone(r, &q);
}
