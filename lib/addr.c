// Copyright (c) vladkens
// https://github.com/vladkens/ecloop
// Licensed under the MIT License.

#pragma once
#include "compat.c"
#include "ecc.c"
#include "rmd160.c"
#include "sha256.c"

typedef u32 h160_t[5];

int compare_160(const void *a, const void *b) {
  const u32 *ua = (const u32 *)a;
  const u32 *ub = (const u32 *)b;
  for (int i = 0; i < 5; i++) {
    if (ua[i] < ub[i]) return -1;
    if (ua[i] > ub[i]) return 1;
  }
  return 0;
}

void print_h160(const h160_t h) {
  for (int i = 0; i < 5; i++) printf("%08x", h[i]);
  printf("\n");
}

void addr33(u32 r[5], const pe *point) {
  u8 msg[64] = {0}; // sha256 payload
  u32 rs[16] = {0}; // sha256 output and rmd160 input

  msg[0] = point->y[0] & 1 ? 0x03 : 0x02;
  for (int i = 0; i < 4; i++) {
    u64 x_be = swap64(point->x[3 - i]);
    memcpy(&msg[1 + i * 8], &x_be, sizeof(u64));
  }

  msg[33] = 0x80;
  msg[62] = 0x01;
  msg[63] = 0x08;
  sha256_final(rs, msg, sizeof(msg));

  for (int i = 0; i < 8; i++) rs[i] = swap32(rs[i]);
  rs[8] = 0x00000080;
  rs[14] = 256;
  rs[15] = 0;
  rmd160_final(r, rs);
}

void addr65(u32 *r, const pe *point) {
  u8 msg[128] = {0}; // sha256 payload
  u32 rs[16] = {0};  // sha256 output and rmd160 input

  msg[0] = 0x04;

  // copy point->x into msg[1..33] in big-endian order
  for (int i = 0; i < 4; i++) {
    u64 x_be = swap64(point->x[3 - i]);
    memcpy(&msg[1 + i * 8], &x_be, sizeof(u64));
  }

  // copy point->y into msg[33..65] in big-endian order
  for (int i = 0; i < 4; i++) {
    u64 y_be = swap64(point->y[3 - i]);
    memcpy(&msg[33 + i * 8], &y_be, sizeof(u64));
  }

  msg[65] = 0x80;
  msg[126] = 0x02;
  msg[127] = 0x08;
  sha256_final(rs, msg, sizeof(msg));

  for (int i = 0; i < 0x8; i++) rs[i] = swap32(rs[i]);
  rs[8] = 0x00000080;
  rs[14] = 256;
  rs[15] = 0;
  rmd160_final(r, rs);
}
