#pragma once
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define INLINE static inline

typedef __uint128_t u128;
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned char u8;

INLINE u64 umul128(const u64 a, const u64 b, u64 *hi) {
  // https://stackoverflow.com/a/50958815
  // https://botan.randombit.net/doxygen/mul128_8h_source.html
  u128 t = (u128)a * b;
  *hi = t >> 64;
  return t;
}

// MARK: Field element
typedef u64 fe[4]; // (256bit as 4x64bit)

// prime field P = 2^256 - 2^32 - 977
static const fe P = {
    0xfffffffefffffc2f,
    0xffffffffffffffff,
    0xffffffffffffffff,
    0xffffffffffffffff,
};

INLINE void fe_print(const char *label, const fe a) {
  printf("%s: %016llx %016llx %016llx %016llx\n", label, a[3], a[2], a[1], a[0]);
}

INLINE bool fe_iszero(const fe r) { return r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0; }
INLINE void fe_clone(fe r, const fe a) { memcpy(r, a, sizeof(fe)); }
INLINE void fe_set64(fe r, const u64 a) {
  memset(r, 0, sizeof(fe));
  r[0] = a;
}

void fe_add64(fe r, const u64 a) {
  u64 c = 0;
  r[0] = __builtin_addcll(r[0], a, 0, &c);
  r[1] = __builtin_addcll(r[1], 0, c, &c);
  r[2] = __builtin_addcll(r[2], 0, c, &c);
  r[3] = __builtin_addcll(r[3], 0, c, &c);
}

int fe_cmp64(const fe a, const u64 b) {
  if (a[3] != 0) return 1;
  if (a[2] != 0) return 1;
  if (a[1] != 0) return 1;
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

// MARK: modular arithmetic

void fe_modneg(fe r, const fe a) { // r = -a (mod P)
  u64 c = 0;
  r[0] = __builtin_subcll(P[0], a[0], c, &c);
  r[1] = __builtin_subcll(P[1], a[1], c, &c);
  r[2] = __builtin_subcll(P[2], a[2], c, &c);
  r[3] = __builtin_subcll(P[3], a[3], c, &c);
}

void fe_modsub(fe r, const fe a, const fe b) { // r = a - b (mod P)
  u64 c = 0;
  r[0] = __builtin_subcll(a[0], b[0], c, &c);
  r[1] = __builtin_subcll(a[1], b[1], c, &c);
  r[2] = __builtin_subcll(a[2], b[2], c, &c);
  r[3] = __builtin_subcll(a[3], b[3], c, &c);

  if (c) {
    r[0] = __builtin_addcll(r[0], P[0], 0, &c);
    r[1] = __builtin_addcll(r[1], P[1], c, &c);
    r[2] = __builtin_addcll(r[2], P[2], c, &c);
    r[3] = __builtin_addcll(r[3], P[3], c, &c);
  }
}

void fe_modadd(fe r, const fe a, const fe b) { // r = a + b (mod P)
  u64 c = 0;
  r[0] = __builtin_addcll(a[0], b[0], c, &c);
  r[1] = __builtin_addcll(a[1], b[1], c, &c);
  r[2] = __builtin_addcll(a[2], b[2], c, &c);
  r[3] = __builtin_addcll(a[3], b[3], c, &c);

  if (c) {
    r[0] = __builtin_subcll(r[0], P[0], 0, &c);
    r[1] = __builtin_subcll(r[1], P[1], c, &c);
    r[2] = __builtin_subcll(r[2], P[2], c, &c);
    r[3] = __builtin_subcll(r[3], P[3], c, &c);
  }

  u64 f = (r[3] >= P[3] && r[2] >= P[2] && r[1] >= P[1] && r[0] >= P[0]);
  if (f) printf("todo overflow: %llu ~ %llu\n", c, f);
}

void fe_mul_scalar(u64 r[5], const fe a, const u64 b) { // 256bit * 64bit -> 320bit
  u64 h1, h2, c = 0;
  r[0] = umul128(a[0], b, &h1);
  r[1] = __builtin_addcll(umul128(a[1], b, &h2), h1, c, &c);
  r[2] = __builtin_addcll(umul128(a[2], b, &h1), h2, c, &c);
  r[3] = __builtin_addcll(umul128(a[3], b, &h2), h1, c, &c);
  r[4] = __builtin_addcll(0, h2, c, &c);
}

void fe_modmul(fe r, const fe a, const fe b) {
  u64 rr[8] = {0}, tt[5] = {0}, c = 0;

  // 256bit * 256bit -> 512bit
  fe_mul_scalar(rr, a, b[0]);
  fe_mul_scalar(tt, a, b[1]);
  rr[1] = __builtin_addcll(rr[1], tt[0], c, &c);
  rr[2] = __builtin_addcll(rr[2], tt[1], c, &c);
  rr[3] = __builtin_addcll(rr[3], tt[2], c, &c);
  rr[4] = __builtin_addcll(rr[4], tt[3], c, &c);
  rr[5] = __builtin_addcll(rr[5], tt[4], c, &c);
  fe_mul_scalar(tt, a, b[2]);
  rr[2] = __builtin_addcll(rr[2], tt[0], c, &c);
  rr[3] = __builtin_addcll(rr[3], tt[1], c, &c);
  rr[4] = __builtin_addcll(rr[4], tt[2], c, &c);
  rr[5] = __builtin_addcll(rr[5], tt[3], c, &c);
  rr[6] = __builtin_addcll(rr[6], tt[4], c, &c);
  fe_mul_scalar(tt, a, b[3]);
  rr[3] = __builtin_addcll(rr[3], tt[0], c, &c);
  rr[4] = __builtin_addcll(rr[4], tt[1], c, &c);
  rr[5] = __builtin_addcll(rr[5], tt[2], c, &c);
  rr[6] = __builtin_addcll(rr[6], tt[3], c, &c);
  rr[7] = __builtin_addcll(rr[7], tt[4], c, &c);

  // reduce 512bit -> 320bit
  fe_mul_scalar(tt, rr + 4, 0x1000003D1ULL);
  rr[0] = __builtin_addcll(rr[0], tt[0], 0, &c);
  rr[1] = __builtin_addcll(rr[1], tt[1], c, &c);
  rr[2] = __builtin_addcll(rr[2], tt[2], c, &c);
  rr[3] = __builtin_addcll(rr[3], tt[3], c, &c);

  // reduce 320bit -> 256bit
  u64 hi, lo;
  lo = umul128(tt[4] + c, 0x1000003D1ULL, &hi);
  r[0] = __builtin_addcll(rr[0], lo, 0, &c);
  r[1] = __builtin_addcll(rr[1], hi, c, &c);
  r[2] = __builtin_addcll(rr[2], 0, c, &c);
  r[3] = __builtin_addcll(rr[3], 0, c, &c);

  if (fe_cmp(r, P) >= 0) fe_modsub(r, r, P);
}

void fe_modsqr(fe r, const fe a) {
  // from: https://github.com/JeanLucPons/VanitySearch/blob/1.19/IntMod.cpp#L1034
  // ~8% faster than fe_modmul(r, a, a)
  // return fe_modmul(r, a, a);

  // k=0
  u64 rr[8] = {0}, tt[5] = {0}, c = 0, t1, t2, lo, hi;
  rr[0] = umul128(a[0], a[0], &tt[1]);

  // k=1
  tt[3] = umul128(a[0], a[1], &tt[4]);
  tt[3] = __builtin_addcll(tt[3], tt[3], 0, &c);
  tt[4] = __builtin_addcll(tt[4], tt[4], c, &c);
  t1 = c;
  tt[3] = __builtin_addcll(tt[1], tt[3], 0, &c);
  tt[4] = __builtin_addcll(tt[4], 0, c, &c);
  t1 += c;
  rr[1] = tt[3];

  // k=2
  tt[0] = umul128(a[0], a[2], &tt[1]);
  tt[0] = __builtin_addcll(tt[0], tt[0], 0, &c);
  tt[1] = __builtin_addcll(tt[1], tt[1], c, &c);
  t2 = c;
  lo = umul128(a[1], a[1], &hi);
  tt[0] = __builtin_addcll(tt[0], lo, 0, &c);
  tt[1] = __builtin_addcll(tt[1], hi, c, &c);
  t2 += c;
  tt[0] = __builtin_addcll(tt[0], tt[4], 0, &c);
  tt[1] = __builtin_addcll(tt[1], t1, c, &c);
  t2 += c;
  rr[2] = tt[0];

  // k=3
  tt[3] = umul128(a[0], a[3], &tt[4]);
  lo = umul128(a[1], a[2], &hi);
  tt[3] = __builtin_addcll(tt[3], lo, 0, &c);
  tt[4] = __builtin_addcll(tt[4], hi, c, &c);
  t1 = c + c;
  tt[3] = __builtin_addcll(tt[3], tt[3], 0, &c);
  tt[4] = __builtin_addcll(tt[4], tt[4], c, &c);
  t1 += c;
  tt[3] = __builtin_addcll(tt[1], tt[3], 0, &c);
  tt[4] = __builtin_addcll(tt[4], t2, c, &c);
  t1 += c;
  rr[3] = tt[3];

  // k=4
  tt[0] = umul128(a[1], a[3], &tt[1]);
  tt[0] = __builtin_addcll(tt[0], tt[0], 0, &c);
  tt[1] = __builtin_addcll(tt[1], tt[1], c, &c);
  t2 = c;
  lo = umul128(a[2], a[2], &hi);
  tt[0] = __builtin_addcll(tt[0], lo, 0, &c);
  tt[1] = __builtin_addcll(tt[1], hi, c, &c);
  t2 += c;
  tt[0] = __builtin_addcll(tt[0], tt[4], 0, &c);
  tt[1] = __builtin_addcll(tt[1], t1, c, &c);
  t2 += c;
  rr[4] = tt[0];

  // k=5
  tt[3] = umul128(a[2], a[3], &tt[4]);
  tt[3] = __builtin_addcll(tt[3], tt[3], 0, &c);
  tt[4] = __builtin_addcll(tt[4], tt[4], c, &c);
  t1 = c;
  tt[3] = __builtin_addcll(tt[3], tt[1], 0, &c);
  tt[4] = __builtin_addcll(tt[4], t2, c, &c);
  t1 += c;
  rr[5] = tt[3];

  // k=6
  tt[0] = umul128(a[3], a[3], &tt[1]);
  tt[0] = __builtin_addcll(tt[0], tt[4], 0, &c);
  tt[1] = __builtin_addcll(tt[1], t1, c, &c);
  rr[6] = tt[0];

  // k=7
  rr[7] = tt[1];

  // reduce 512bit -> 320bit
  fe_mul_scalar(tt, rr + 4, 0x1000003D1ULL);
  rr[0] = __builtin_addcll(rr[0], tt[0], 0, &c);
  rr[1] = __builtin_addcll(rr[1], tt[1], c, &c);
  rr[2] = __builtin_addcll(rr[2], tt[2], c, &c);
  rr[3] = __builtin_addcll(rr[3], tt[3], c, &c);

  // reduce 320bit -> 256bit
  lo = umul128(tt[4] + c, 0x1000003D1ULL, &hi);
  r[0] = __builtin_addcll(rr[0], lo, 0, &c);
  r[1] = __builtin_addcll(rr[1], hi, c, &c);
  r[2] = __builtin_addcll(rr[2], 0, c, &c);
  r[3] = __builtin_addcll(rr[3], 0, c, &c);

  if (fe_cmp(r, P) >= 0) fe_modsub(r, r, P);
}

INLINE void fe_shiftr(fe r, u8 n) {
  r[0] = (r[0] >> n) | (r[1] << (64 - n));
  r[1] = (r[1] >> n) | (r[2] << (64 - n));
  r[2] = (r[2] >> n) | (r[3] << (64 - n));
  r[3] = (r[3] >> n);
}

void _fe_modinv_binpow(fe r, const fe a) {
  // a^(P-2) = a^-1 (mod P)
  // https://e-maxx.ru/algo/reverse_element https://e-maxx.ru/algo/binary_pow
  fe q = {1}, p, t;
  fe_clone(p, P);
  fe_clone(t, a);
  p[0] -= 2;

  while (p[0] || p[1] || p[2] || p[3]) {     // p > 0
    if ((a[0] & 1) != 0) fe_modmul(q, q, t); // when odd
    fe_modsqr(t, t);
    fe_shiftr(p, 1);
  }

  fe_clone(r, q);
}

void _fe_modinv_addchn(fe r, const fe a) {
  // https://briansmith.org/ecc-inversion-addition-chains-01#secp256k1_field_inversion
  fe x2, x3, x6, x9, x11, x22, x44, x88, x176, x220, x223, t1;
  fe_modsqr(x2, a);
  fe_modmul(x2, x2, a);

  fe_clone(x3, x2);
  fe_modsqr(x3, x2);
  fe_modmul(x3, x3, a);

  fe_clone(x6, x3);
  for (int j = 0; j < 3; j++) fe_modsqr(x6, x6);
  fe_modmul(x6, x6, x3);

  fe_clone(x9, x6);
  for (int j = 0; j < 3; j++) fe_modsqr(x9, x9);
  fe_modmul(x9, x9, x3);

  fe_clone(x11, x9);
  for (int j = 0; j < 2; j++) fe_modsqr(x11, x11);
  fe_modmul(x11, x11, x2);

  fe_clone(x22, x11);
  for (int j = 0; j < 11; j++) fe_modsqr(x22, x22);
  fe_modmul(x22, x22, x11);

  fe_clone(x44, x22);
  for (int j = 0; j < 22; j++) fe_modsqr(x44, x44);
  fe_modmul(x44, x44, x22);

  fe_clone(x88, x44);
  for (int j = 0; j < 44; j++) fe_modsqr(x88, x88);
  fe_modmul(x88, x88, x44);

  fe_clone(x176, x88);
  for (int j = 0; j < 88; j++) fe_modsqr(x176, x176);
  fe_modmul(x176, x176, x88);

  fe_clone(x220, x176);
  for (int j = 0; j < 44; j++) fe_modsqr(x220, x220);
  fe_modmul(x220, x220, x44);

  fe_clone(x223, x220);
  for (int j = 0; j < 3; j++) fe_modsqr(x223, x223);
  fe_modmul(x223, x223, x3);

  fe_clone(t1, x223);
  for (int j = 0; j < 23; j++) fe_modsqr(t1, t1);
  fe_modmul(t1, t1, x22);
  for (int j = 0; j < 5; j++) fe_modsqr(t1, t1);
  fe_modmul(t1, t1, a);
  for (int j = 0; j < 3; j++) fe_modsqr(t1, t1);
  fe_modmul(t1, t1, x2);
  for (int j = 0; j < 2; j++) fe_modsqr(t1, t1);
  fe_modmul(r, t1, a);
}

INLINE void fe_modinv(fe r, const fe a) { return _fe_modinv_addchn(r, a); }

void fe_grpinv(fe r[], const u32 n) {
  fe *zs = (fe *)malloc(n * sizeof(fe));

  fe_clone(*zs, r[0]);
  for (u32 i = 1; i < n; ++i) {
    fe_modmul(*(zs + i), *(zs + (i - 1)), r[i]);
  }

  fe t1, t2;
  fe_clone(t1, *(zs + (n - 1)));
  fe_modinv(t1, t1);

  for (u32 i = n - 1; i > 0; --i) {
    fe_modmul(t2, t1, *(zs + (i - 1)));
    fe_modmul(t1, r[i], t1);
    fe_clone(r[i], t2);
  }

  fe_clone(r[0], t1);
  free(zs);
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

  if (fe_cmp(r, P) >= 0) fe_modsub(r, r, P);
}

// MARK: EC Point
// https://eprint.iacr.org/2015/1060.pdf
// https://hyperelliptic.org/EFD/g1p/auto-shortw.html

typedef struct pe {
  fe x, y, z;
} pe;

static const pe G1 = {
    .x = {0x59f2815b16f81798, 0x029bfcdb2dce28d9, 0x55a06295ce870b07, 0x79be667ef9dcbbac},
    .y = {0x9c47d08ffb10d4b8, 0xfd17b448a6855419, 0x5da4fbfc0e1108a8, 0x483ada7726a3c465},
    .z = {0x1, 0x0, 0x0, 0x0},
};

static const pe G2 = {
    .x = {0xabac09b95c709ee5, 0x5c778e4b8cef3ca7, 0x3045406e95c07cd8, 0xc6047f9441ed7d6d},
    .y = {0x236431a950cfe52a, 0xf7f632653266d0e1, 0xa3c58419466ceaee, 0x1ae168fea63dc339},
    .z = {0x1, 0x0, 0x0, 0x0},
};

void pe_clone(pe *r, const pe *a) {
  fe_clone(r->x, a->x);
  fe_clone(r->y, a->y);
  fe_clone(r->z, a->z);
}

// https://en.wikibooks.org/wiki/Cryptography/Prime_Curve/Affine_Coordinates

void ec_affine_dbl(pe *r, const pe *p) {
  // λ = (3 * x^2) / (2 * y)
  // x = λ^2 - 2 * x
  // y = λ * (x1 - x) - y1
  fe t1, t2, t3;
  fe_modsqr(t1, p->x);       // x^2
  fe_modadd(t2, t1, t1);     // 2 * x^2
  fe_modadd(t2, t2, t1);     // 3 * x^2
  fe_modadd(t1, p->y, p->y); // 2 * y
  fe_modinv(t1, t1);         //
  fe_modmul(t1, t2, t1);     // λ = (3 * x^2) / (2 * y)
  fe_modsqr(t3, t1);         // λ^2
  fe_modsub(t3, t3, p->x);   // λ^2 - x1
  fe_modsub(t3, t3, p->x);   // λ^2 - x1 - x1
  fe_modsub(t2, p->x, t3);   // x1 - x
  fe_modmul(t2, t1, t2);     // λ * (x1 - x)
  fe_modsub(r->y, t2, p->y); // λ * (x1 - x) - y1
  fe_clone(r->x, t3);
}

void ec_affine_add(pe *r, const pe *p, const pe *q) {
  // λ = (y1 - y2) / (x1 - x2)
  // x = λ^2 - x1 - x2
  // y = λ * (x1 - x) - y1
  fe t1, t2, t3, t4;
  fe_modsub(t1, p->y, q->y); // y1 - y2
  fe_modsub(t2, p->x, q->x); // x1 - x2
  fe_modinv(t2, t2);         //
  fe_modmul(t1, t1, t2);     // λ = (y1 - y2) / (x1 - x2)
  fe_modsqr(t3, t1);         // λ^2
  fe_modsub(t3, t3, p->x);   // λ^2 - x1
  fe_modsub(t3, t3, q->x);   // λ^2 - x1 - x2
  fe_modsub(t4, p->x, t3);   // x1 - x
  fe_modmul(t4, t1, t4);     // λ * (x1 - x)
  fe_modsub(r->y, t4, p->y); // λ * (x1 - x) - y1
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
  fe_modsqr(t, p->x);          // X^2
  fe_modadd(w, t, t);          // 2*X^2
  fe_modadd(w, w, t);          // 3*X^2
  fe_modmul(s, p->y, p->z);    // Y*Z
  fe_modmul(b, p->x, p->y);    // X*Y
  fe_modmul(b, b, s);          // X*Y*S
  fe_modadd(b, b, b);          // 2*B
  fe_modadd(b, b, b);          // 4*B
  fe_modadd(t, b, b);          // 8*B
  fe_modsqr(h, w);             // W^2
  fe_modsub(h, h, t);          // W^2 - 8*B
  fe_modmul(r->x, h, s);       // H*S
  fe_modadd(r->x, r->x, r->x); // 2*H*S
  fe_modsub(t, b, h);          // 4*B - H
  fe_modmul(t, w, t);          // W*(4*B - H)
  fe_modsqr(r->y, p->y);       // Y^2
  fe_modsqr(h, s);             // S^2
  fe_modmul(r->y, r->y, h);    // Y^2*S^2
  fe_modadd(r->y, r->y, r->y); // 2*Y^2*S^2
  fe_modadd(r->y, r->y, r->y); // 4*Y^2*S^2
  fe_modadd(r->y, r->y, r->y); // 8*Y^2*S^2
  fe_modsub(r->y, t, r->y);    // W*(4*B - H) - 8*Y^2*S^2
  fe_modmul(r->z, h, s);       // S^3
  fe_modadd(r->z, r->z, r->z); // 2*S^3
  fe_modadd(r->z, r->z, r->z); // 4*S^3
  fe_modadd(r->z, r->z, r->z); // 8*S^3
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
  fe_modmul(u2, p->y, q->z); // u2 = py * qz
  fe_modmul(v2, p->x, q->z); // v2 = px * qz
  fe_modmul(u, q->y, p->z);  // u1 = qy * pz
  fe_modmul(v, q->x, p->z);  // v1 = qx * pz
  fe_modmul(w, p->z, q->z);  // w = pz * qz
  fe_modsub(u, u, u2);       // u = u1 - u2
  fe_modsub(v, v, v2);       // v = v1 - v2
  fe_modsqr(vs, v);          // v^2
  fe_modmul(vc, vs, v);      // v^3
  fe_modmul(vs, vs, v2);     // v^2 * v2
  fe_modmul(r->z, vc, w);    // z3 = v^3 * w
  fe_modsqr(a, u);           // u^2
  fe_modmul(a, a, w);        // u^2 * w
  fe_modadd(w, vs, vs);      // 2 * v^2 * v2                        [w reused]
  fe_modsub(a, a, vc);       // u^2 * w - v^3
  fe_modsub(a, a, w);        // u^2 * w - v^3 - 2 * v^2 * v2
  fe_modmul(r->x, v, a);     // x3 = v * a
  fe_modsub(a, vs, a);       // v^2 * v2 - a                        [a reused]
  fe_modmul(a, a, u);        // u * (v^2 * v2 - a)                  [a reused]
  fe_modmul(u, vc, u2);      // v^3 * u2                            [u reused]
  fe_modsub(r->y, a, u);     // y3 = u * (v^2 * v2 - a) - v^3 * u2
}

void _ec_jacobi_rdc1(pe *r, const pe *a) {
  // reduce Standard Projective to Affine
  fe_clone(r->z, a->z);
  fe_modinv(r->z, r->z);
  fe_modmul(r->x, a->x, r->z);
  fe_modmul(r->y, a->y, r->z);
  fe_set64(r->z, 0x1);
}

void _ec_jacobi_grprdc1(pe r[], u64 n) {
  fe *zz = (fe *)malloc(n * sizeof(fe));
  for (u64 i = 0; i < n; ++i) fe_clone(zz[i], r[i].z);
  fe_grpinv(zz, n);

  for (u64 i = 0; i < n; ++i) {
    fe_modmul(r[i].x, r[i].x, zz[i]);
    fe_modmul(r[i].y, r[i].y, zz[i]);
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
  fe_modmul(r->z, p->y, p->z); // Y*Z
  fe_modadd(r->z, r->z, r->z); // 2*Y*Z
  fe_modsqr(t, p->y);          // Y^2
  fe_modmul(s, p->x, t);       // X*Y^2
  fe_modadd(s, s, s);          // 2*X*Y^2
  fe_modadd(s, s, s);          // 4*X*Y^2
  fe_modsqr(t, t);             // Y^4
  fe_modadd(r->y, t, t);       // 2*Y^4
  fe_modadd(t, r->y, r->y);    // 4*Y^4
  fe_modadd(r->y, t, t);       // 8*Y^4
  fe_modsqr(t, p->x);          // X^2
  fe_modadd(m, t, t);          // 2*X^2
  fe_modadd(m, m, t);          // 3*X^2
  fe_modsqr(r->x, m);          // M^2
  fe_modadd(t, s, s);          // 2*S
  fe_modsub(r->x, r->x, t);    // M^2 - 2*S
  fe_modsub(t, s, r->x);       // S - X'
  fe_modmul(t, m, t);          // M*(S - X')
  fe_modsub(r->y, t, r->y);    // M*(S - X') - 8*Y^4
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
  fe_modsqr(tt, q->z);         // qz ** 2
  fe_modmul(u1, p->x, tt);     // U1 = px * qz ** 2
  fe_modmul(ta, tt, q->z);     // qz ** 3
  fe_modmul(s1, p->y, ta);     // S1 = py * qz ** 3
  fe_modsqr(tt, p->z);         // pz ** 2
  fe_modmul(u2, q->x, tt);     // U2 = qx * pz ** 2
  fe_modmul(ta, tt, p->z);     // pz ** 3
  fe_modmul(s2, q->y, ta);     // S2 = qy * pz ** 3
  fe_modsub(u2, u2, u1);       // H = U2 - U1              [u2 reused]
  fe_modsub(s2, s2, s1);       // R = S2 - S1              [s2 reused]
  fe_modsqr(tt, u2);           // H ** 2
  fe_modmul(u1, u1, tt);       // U1H2 = U1 * H ** 2       [s1 reused]
  fe_modmul(tt, tt, u2);       // H ** 3
  fe_modadd(ta, u1, u1);       // 2 * U1H2
  fe_modsqr(r->x, s2);         // R ** 2
  fe_modsub(r->x, r->x, tt);   // R ** 2 - H ** 3
  fe_modsub(r->x, r->x, ta);   // nx = R ** 2 - H ** 3 - 2 * U1H2
  fe_modmul(ta, tt, s1);       // S1 * H ** 3
  fe_modsub(r->y, u1, r->x);   // U1H2 - nx
  fe_modmul(r->y, r->y, s2);   // R * (U1H2 - nx)
  fe_modsub(r->y, r->y, ta);   // R * (U1H2 - nx) - S1 * H ** 3
  fe_modmul(r->z, p->z, q->z); // pz * qz
  fe_modmul(r->z, r->z, u2);   // H * pz * qz
}

void _ec_jacobi_rdc2(pe *r, const pe *a) {
  // reduce Jacobian to Affine
  fe t;
  fe_clone(r->z, a->z);
  fe_modinv(r->z, r->z);
  fe_modsqr(t, r->z);       // z ** 2
  fe_modmul(r->x, a->x, t); // x = x * z ** 2
  fe_modmul(t, t, r->z);    // z ** 3
  fe_modmul(r->y, a->y, t); // y = y * z ** 3
  fe_set64(r->z, 0x1);
}

void _ec_jacobi_grprdc2(pe r[], u64 n) {
  fe *zz = (fe *)malloc(n * sizeof(fe));
  for (u64 i = 0; i < n; ++i) fe_clone(zz[i], r[i].z);
  fe_grpinv(zz, n);

  fe z = {0};
  for (u64 i = 0; i < n; ++i) {
    fe_modsqr(z, zz[i]);          // z^2
    fe_modmul(r[i].x, r[i].x, z); // x = x * z^2
    fe_modmul(z, z, zz[i]);       // z^3
    fe_modmul(r[i].y, r[i].y, z); // y = y * z^3
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

//

bool ec_verify(const pe *p) {
  // y^2 = x^3 + 7
  pe q, g;
  pe_clone(&q, p);
  ec_jacobi_rdc(&q, &q);

  pe_clone(&g, &q);
  fe_modsqr(g.y, g.y);      // y^2
  fe_modsqr(g.x, g.x);      // x^2
  fe_modmul(g.x, g.x, q.x); // x^3
  fe_modsub(g.y, g.y, g.x); // y^2 - x^3
  return g.y[0] == 7 && g.y[1] == 0 && g.y[2] == 0 && g.y[3] == 0;
}

//

void ec_jacobi_mul(pe *r, const pe *p, const fe k) {
  // double-and-add in Jacobian space
  pe t;
  fe_clone(t.x, p->x);
  fe_clone(t.y, p->y);
  fe_clone(t.z, p->z);
  fe_set64(r->x, 0x0);
  fe_set64(r->y, 0x0);
  fe_set64(r->z, 0x1);

  // count significant bits in k
  u64 bits = 256;
  for (int i = 3; i >= 0; --i) {
    if (k[i] == 0) {
      bits -= 64;
    } else {
      bits = 64 * i + (64 - __builtin_clzll(k[i]));
      break;
    }
  }

  for (int i = 0; i < bits; ++i) {
    if (k[i / 64] & (1ULL << (i % 64))) {
      // todo: remove if condition / here simplified check to point of infinity
      if (r->x[0] == 0 && r->y[0] == 0) {
        // printf("add(0): %d ~ %d\n", i, i / 64);
        fe_clone(r->x, t.x);
        fe_clone(r->y, t.y);
        fe_clone(r->z, t.z);
      } else {
        // printf("add(1): %d ~ %d\n", i, i / 64);
        ec_jacobi_add(r, r, &t);
      }
    }
    ec_jacobi_dbl(&t, &t);
  }
}

// MARK: EC GTable

u64 GTABLE_W = 14;
pe *_gtable = NULL;

// https://www.sav.sk/journals/uploads/0215094304C459.pdf (Algorithm 3)

size_t ec_gtable_init() {
  u64 n = 1 << GTABLE_W;
  u64 d = ((256 - 1) / GTABLE_W) + 1;
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

  u64 n = 1 << GTABLE_W;
  u64 d = ((256 - 1) / GTABLE_W) + 1;
  pe q = {0};
  fe k;
  fe_clone(k, pk);

  for (u64 i = 0; i < d; ++i) {
    u64 b = k[0] & (n - 1);
    fe_shiftr(k, GTABLE_W);
    if (!b) continue;

    u64 x = (n - 1) * i + b - 1;
    fe_iszero(q.x) ? pe_clone(&q, &_gtable[x]) : ec_jacobi_add(&q, &q, &_gtable[x]);
  }

  pe_clone(r, &q);
}
