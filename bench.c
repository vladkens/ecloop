#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "lib/addr.c"
#include "lib/ec64.c"

void print_res(char *label, clock_t stime, u64 iters) {
  double dt = ((double)(clock() - stime)) / CLOCKS_PER_SEC;
  printf("%20s: %.2fM it/s ~ %.2f s\n", label, iters / dt / 1000000, dt);
}

int main() {
  clock_t stime;
  u64 iters;
  pe g;
  fe f;

  // jacobian coordinates
  iters = 1000 * 1000 * 10;

  pe_clone(&g, &G2);
  stime = clock();
  for (u64 i = 0; i < iters; ++i) _ec_jacobi_add1(&g, &g, &G1);
  print_res("_ec_jacobi_add1", stime, iters);

  pe_clone(&g, &G2);
  stime = clock();
  for (u64 i = 0; i < iters; ++i) _ec_jacobi_add2(&g, &g, &G1);
  print_res("_ec_jacobi_add2", stime, iters);

  pe_clone(&g, &G2);
  stime = clock();
  for (u64 i = 0; i < iters; ++i) _ec_jacobi_dbl1(&g, &g);
  print_res("_ec_jacobi_dbl1", stime, iters);

  pe_clone(&g, &G2);
  stime = clock();
  for (u64 i = 0; i < iters; ++i) _ec_jacobi_dbl2(&g, &g);
  print_res("_ec_jacobi_dbl2", stime, iters);

  // montgomery ladder
  iters = 1000 * 50;

  pe_clone(&g, &G2);
  fe_set64(f, 0);
  f[0] = ((u64)2 << 63) - 1;
  f[2] = ((u64)2 << 31) - 1;
  stime = clock();
  for (u64 i = 0; i < iters; ++i) ec_jacobi_mul(&g, &g, f);
  print_res("ec_jacobi_mul", stime, iters);

  // affine coordinates
  iters = 1000 * 500;

  pe_clone(&g, &G2);
  stime = clock();
  for (u64 i = 0; i < iters; ++i) ec_affine_add(&g, &g, &G1);
  print_res("ec_affine_add", stime, iters);

  pe_clone(&g, &G2);
  stime = clock();
  for (u64 i = 0; i < iters; ++i) ec_affine_dbl(&g, &g);
  print_res("ec_affine_dbl", stime, iters);

  // modular inversion
  iters = 1000 * 100;

  for (u64 i = 0; i < iters; ++i) _fe_modinv_binpow(f, g.x);
  print_res("_fe_modinv_binpow", stime, iters);

  for (u64 i = 0; i < iters; ++i) _fe_modinv_addchn(f, g.x);
  print_res("_fe_modinv_addchn", stime, iters);
}
