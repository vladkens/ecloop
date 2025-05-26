// Copyright (c) vladkens
// https://github.com/vladkens/ecloop
// Licensed under the MIT License.

#pragma once
#include <assert.h>

#include "addr.c"
#include "ecc.c"
#include "utils.c"

void print_res(char *label, size_t stime, size_t iters) {
  double dt = MAX((tsnow() - stime), 1ul) / 1000.0;
  printf("%20s: %.2fM it/s ~ %.2fs\n", label, iters / dt / 1000000, dt);
}

void run_bench() {
  ec_gtable_init();

  // note: asserts used to prevent compiler optimization
  size_t stime, iters, i;
  pe g;
  fe f;

  // projective & jacobian coordinates
  iters = 1000 * 1000 * 6;

  stime = tsnow();
  pe_clone(&g, &G2);
  for (i = 0; i < iters; ++i) _ec_jacobi_add1(&g, &g, &G1);
  print_res("_ec_jacobi_add1", stime, iters);
  assert(fe_cmp(g.x, G1.x) != 0);

  pe_clone(&g, &G2);
  stime = tsnow();
  for (i = 0; i < iters; ++i) _ec_jacobi_add2(&g, &g, &G1);
  print_res("_ec_jacobi_add2", stime, iters);
  assert(fe_cmp(g.x, G1.x) != 0);

  pe_clone(&g, &G2);
  stime = tsnow();
  for (i = 0; i < iters; ++i) _ec_jacobi_dbl1(&g, &g);
  print_res("_ec_jacobi_dbl1", stime, iters);
  assert(fe_cmp(g.x, G1.x) != 0);

  pe_clone(&g, &G2);
  stime = tsnow();
  for (i = 0; i < iters; ++i) _ec_jacobi_dbl2(&g, &g);
  print_res("_ec_jacobi_dbl2", stime, iters);
  assert(fe_cmp(g.x, G1.x) != 0);

  // ec multiplication
  srand(42);
  size_t numSize = 1024 * 16;
  fe numbers[numSize];
  for (size_t i = 0; i < numSize; ++i) fe_prand(numbers[i]);
  pe_clone(&g, &G2);

  iters = 1000 * 10;
  stime = tsnow();
  for (i = 0; i < iters; ++i) ec_jacobi_mul(&g, &G1, numbers[i % numSize]);
  print_res("ec_jacobi_mul", stime, iters);
  assert(fe_cmp(g.x, G1.x) != 0);

  iters = 1000 * 500;
  stime = tsnow();
  for (i = 0; i < iters; ++i) ec_gtable_mul(&g, numbers[i % numSize]);
  print_res("ec_gtable_mul", stime, iters);
  assert(fe_cmp(g.x, G1.x) != 0);

  // affine coordinates
  iters = 1000 * 500;

  pe_clone(&g, &G2);
  stime = tsnow();
  for (i = 0; i < iters; ++i) ec_affine_add(&g, &g, &G1);
  print_res("ec_affine_add", stime, iters);
  assert(fe_cmp(g.x, G1.x) != 0);

  pe_clone(&g, &G2);
  stime = tsnow();
  for (i = 0; i < iters; ++i) ec_affine_dbl(&g, &g);
  print_res("ec_affine_dbl", stime, iters);
  assert(fe_cmp(g.x, G1.x) != 0);

  // modular inversion
  iters = 1000 * 100;

  stime = tsnow();
  for (i = 0; i < iters; ++i) _fe_modp_inv_binpow(f, g.x);
  print_res("_fe_modinv_binpow", stime, iters);
  assert(fe_cmp(f, G1.x) != 0);

  stime = tsnow();
  for (i = 0; i < iters; ++i) _fe_modp_inv_addchn(f, g.x);
  print_res("_fe_modinv_addchn", stime, iters);
  assert(fe_cmp(f, G1.x) != 0);

  // hash functions
  iters = 1000 * 1000 * 10;
  h160_t h160;

  stime = tsnow();
  for (i = 0; i < iters; ++i) addr33(h160, &g);
  print_res("addr33", stime, iters);
  assert(h160[0] != 0);

  stime = tsnow();
  for (i = 0; i < iters; ++i) addr65(h160, &g);
  print_res("addr65", stime, iters);
  assert(h160[0] != 0);
}

void run_bench_gtable() {
  srand(42);
  size_t numSize = 1024 * 16;
  fe numbers[numSize];
  for (size_t i = 0; i < numSize; ++i) fe_prand(numbers[i]);

  size_t iters = 1000 * 500;
  size_t stime;
  double gent, mult;
  pe g;

  size_t mem_used;
  for (int i = 8; i <= 22; i += 2) {
    _GTABLE_W = i;

    stime = tsnow();
    mem_used = ec_gtable_init();
    gent = ((double)(tsnow() - stime)) / 1000;

    stime = tsnow();
    for (size_t i = 0; i < iters; ++i) ec_gtable_mul(&g, numbers[i % numSize]);
    mult = ((double)(tsnow() - stime)) / 1000;

    double mem = (double)mem_used / 1024 / 1024;                              // MB
    printf("w=%02d: %.1fK it/s | gen: %5.2fs | mul: %5.2fs | mem: %8.1fMB\n", //
           i, iters / mult / 1000, gent, mult, mem);
  }
}

void mult_verify() {
  ec_gtable_init();

  pe r1, r2;
  fe pk;
  for (int i = 0; i < 1000 * 16; ++i) {
    fe_set64(pk, i + 2);

    ec_jacobi_mulrdc(&r1, &G1, pk);
    ec_verify(&r1);

    ec_gtable_mul(&r2, pk);
    ec_jacobi_rdc(&r2, &r2);
    ec_verify(&r2);

    if (memcmp(&r1, &r2, sizeof(pe)) != 0) {
      printf("invalid on %d\n", i);
      fe_print("pk", pk);
      fe_print("r1", r1.x);
      fe_print("r2", r2.x);
      exit(1);
    }
  }
}
