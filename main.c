#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "lib/addr.c"
#include "lib/ec64.c"

#define MAX_THREADS 64
#define JOB_SIZE 1024 * 1024 * 2
#define GROUP_INV_SIZE 1024

typedef struct ctx_t {
  pthread_mutex_t mutex;
  clock_t stime;
  clock_t ltime;
  size_t threads;
  u64 checked;
  u64 found;

  bool check_addr33;
  bool check_addr65;
  fe range_s; // search range start
  fe range_e; // search range end
  pe gpoints[GROUP_INV_SIZE];

  h160_t *to_find_hashes;
  size_t to_find_count;
} ctx_t;

bool arg_bool(int argc, const char **argv, const char *name) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], name) == 0) return true;
  }
  return false;
}

int arg_int(int argc, const char **argv, const char *name, int def) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], name) == 0) {
      if (i + 1 < argc) return atoi(argv[i + 1]);
    }
  }
  return def;
}

char *arg_str(int argc, const char **argv, const char *name) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], name) == 0) {
      if (i + 1 < argc) return (char *)argv[i + 1];
    }
  }
  return NULL;
}

void parse_search_range(int argc, const char **argv, fe range_s, fe range_e) {
  char *raw = arg_str(argc, argv, "-r");
  if (!raw) {
    fe_set64(range_s, GROUP_INV_SIZE);
    fe_clone(range_e, P);
    return;
  }

  char *sep = strchr(raw, ':');
  if (!sep) {
    printf("invalid search range, use format: -r 8000:ffff\n");
    exit(1);
  }

  *sep = 0;
  fe_from_hex(range_s, raw);
  fe_from_hex(range_e, sep + 1);

  if (fe_cmp64(range_s, GROUP_INV_SIZE) < 0) fe_set64(range_s, GROUP_INV_SIZE);
  if (fe_cmp(range_e, P) > 0) fe_clone(range_e, P);

  if (fe_cmp(range_s, range_e) >= 0) {
    printf("invalid search range, start >= end\n");
    exit(1);
  }
}

void load_hashes(ctx_t *ctx, const char *path) {
  if (!path) {
    printf("missing input file\n");
    exit(1);
  }

  FILE *file = fopen(path, "rb");
  if (!file) {
    printf("failed to open file: %s\n", path);
    exit(1);
  }

  size_t capacity = 32;
  size_t size = 0;
  u32 *hashes = malloc(capacity * sizeof(u32) * 5);

  char line[41];
  while (fgets(line, 41, file)) {
    if (strlen(line) != 40) continue;

    if (size >= capacity) {
      capacity *= 2;
      hashes = realloc(hashes, capacity * sizeof(u32) * 5);
    }

    for (size_t j = 0; j < 40; j += 8) {
      sscanf(line + j, "%8x", &hashes[size * 5 + j / 8]);
    }

    size += 1;
  }

  fclose(file);
  qsort(hashes, size, 5 * sizeof(u32), compare_160);
  ctx->to_find_hashes = (h160_t *)hashes;
  ctx->to_find_count = size;
}

void print_status(const ctx_t *ctx) {
  double dt = ((double)(ctx->ltime - ctx->stime)) / CLOCKS_PER_SEC / ctx->threads;
  printf("\033[F\n");
  printf("%.2f s ~ %.2fM it/s ~ found: %llu", dt, ctx->checked / dt / 1000000, ctx->found);
}

void print_fount(char *label, const h160_t hash, const fe pk) {
  printf("\033[F\n%s: %08x%08x%08x%08x%08x <- %016llx%016llx%016llx%016llx\n", //
         label, hash[0], hash[1], hash[2], hash[3], hash[4],                   //
         pk[3], pk[2], pk[1], pk[0]);
}

bool check_h160(const ctx_t *ctx, const pe *p, const fe pk) {
  h160_t hash, *resp;
  bool found = false;

  if (ctx->check_addr33) {
    addr33(hash, p);
    resp = bsearch(hash, ctx->to_find_hashes, ctx->to_find_count, sizeof(h160_t), compare_160);
    if (resp) {
      print_fount("found addr33", hash, pk);
      print_status(ctx);
      found = true;
    }
  }

  if (ctx->check_addr65) {
    addr65(hash, p);
    resp = bsearch(hash, ctx->to_find_hashes, ctx->to_find_count, sizeof(h160_t), compare_160);
    if (resp) {
      print_fount("found addr65", hash, pk);
      print_status(ctx);
      found = true;
    }
  }

  return found;
}

u64 process_b(const ctx_t *ctx, const fe pk, const u64 iterations) {
  u64 dx_size = MIN(GROUP_INV_SIZE, iterations);
  u64 found = 0;

  fe ck, ss, rx, ry;
  fe dx[dx_size], di[dx_size]; // can dx_size be used here?
  memcpy(ck, pk, sizeof(ck));

  pe start_point, check_point;
  pe *bp = malloc(dx_size * sizeof(pe));
  ec_jacobi_mul(&start_point, &G1, ck);
  ec_jacobi_rdc(&start_point, &start_point);
  memcpy(&check_point, &start_point, sizeof(pe));

  u64 counter = 0;
  while (counter < iterations) {
    for (u64 i = 0; i < dx_size; ++i) {
      fe_modsub(dx[i], ctx->gpoints[i].x, start_point.x);
    }

    memcpy(di, dx, sizeof(dx));
    fe_grpinv(di, dx_size);

    for (u64 i = 0; i < dx_size; ++i) {
      fe_modsub(ss, ctx->gpoints[i].y, start_point.y);
      fe_modmul(ss, ss, di[i]); // λ = (y2 - y1) / (x2 - x1)

      fe_modsqr(rx, ss);
      fe_modsub(rx, rx, start_point.x);
      fe_modsub(rx, rx, ctx->gpoints[i].x); // rx = λ² - x1 - x2

      fe_modsub(ry, ctx->gpoints[i].x, rx);
      fe_modmul(ry, ss, ry);
      fe_modsub(ry, ry, ctx->gpoints[i].y); // ry = λ(x1 - x3) - y1

      fe_clone(bp[i].x, rx);
      fe_clone(bp[i].y, ry);

      // ec_jacobi_add(&check_point, &check_point, &G1);
      // ec_jacobi_rdc(&check_point, &check_point);
      // if (fe_cmp(rx, check_point.x) != 0 || fe_cmp(ry, check_point.y) != 0) {
      //   printf("error on 0x%llx\n", counter + i);
      //   exit(1);
      // }
    }

    for (u64 i = 0; i < dx_size; ++i) {
      fe_add64(ck, 1);
      if (check_h160(ctx, &bp[i], ck)) found += 1;
    }

    memcpy(&start_point, &bp[dx_size - 1], sizeof(pe));
    counter += dx_size;
  }

  free(bp);
  return found;
}

void *run_thread(void *arg) {
  ctx_t *ctx = (ctx_t *)arg;

  fe pk;
  while (fe_cmp(ctx->range_s, ctx->range_e) < 0) {
    pthread_mutex_lock(&ctx->mutex);
    fe_clone(pk, ctx->range_s);
    fe_add64(ctx->range_s, JOB_SIZE);
    pthread_mutex_unlock(&ctx->mutex);

    u64 found = process_b(ctx, pk, JOB_SIZE);

    pthread_mutex_lock(&ctx->mutex);
    ctx->checked += JOB_SIZE;
    ctx->found += found;
    ctx->ltime = clock();
    pthread_mutex_unlock(&ctx->mutex);

    print_status(ctx);
  }

  return NULL;
}

void print_help() {
  printf("Usage: ./ecloop -i <filepath> [-t <threads>] [-r <range>] [-b] [-u]\n");
  printf("  -i <filepath>    - file with hashes to search\n");
  printf("  -t <threads>     - number of threads to run (default: 1)\n");
  printf("  -r <range>       - search range in hex format (example: 8000:ffff, default all)\n");
  printf("  -b               - check both addr33 and addr65 (default: addr33)\n");
  printf("  -u               - check only addr65\n");
}

int main(int argc, const char **argv) {
  pthread_t threads[MAX_THREADS];

  if (arg_bool(argc, argv, "-h")) {
    print_help();
    return 0;
  }

  ctx_t ctx = {
      .mutex = PTHREAD_MUTEX_INITIALIZER,
      .stime = clock(),
      .threads = arg_int(argc, argv, "-t", 1),
      .check_addr33 = arg_bool(argc, argv, "-b") || !arg_bool(argc, argv, "-u"),
      .check_addr65 = arg_bool(argc, argv, "-b") || arg_bool(argc, argv, "-u"),
      .checked = 0,
  };

  if (ctx.threads < 1 && ctx.threads > MAX_THREADS) {
    printf("threads must be between 1 and %d\n", MAX_THREADS);
    return 1;
  }

  parse_search_range(argc, argv, ctx.range_s, ctx.range_e);

  char *path = arg_str(argc, argv, "-i");
  load_hashes(&ctx, path);
  if (ctx.to_find_count == 0) {
    printf("no hashes to search\n");
    return 1;
  }
  // for (size_t i = 0; i < ctx.to_find_count; ++i) print_h160(ctx.to_find_hashes[i]);

  // precompute gpoints
  memcpy(ctx.gpoints + 0, &G1, sizeof(pe));
  memcpy(ctx.gpoints + 1, &G2, sizeof(pe));
  for (u64 i = 2; i < GROUP_INV_SIZE; ++i) {
    ec_jacobi_add(ctx.gpoints + i, ctx.gpoints + i - 1, &G1);
    ec_jacobi_rdc(ctx.gpoints + i, ctx.gpoints + i);
  }

  // welcome message
  printf("threads: %zu ~ addr33: %d ~ addr65: %d ~ hashes to search: %zu\n", ctx.threads,
         ctx.check_addr33, ctx.check_addr65, ctx.to_find_count);
  fe_print("range_s", ctx.range_s);
  fe_print("range_e", ctx.range_e);
  printf("----------------------------------------\n");

  // run threads
  ctx.stime = clock();
  for (u64 i = 0; i < ctx.threads; ++i) {
    pthread_create(&threads[i], NULL, &run_thread, (void *)&ctx);
  }

  for (u64 i = 0; i < ctx.threads; ++i) {
    pthread_join(threads[i], NULL);
  }

  pthread_mutex_destroy(&ctx.mutex);

  printf("\033[F\n");
  printf("----------------------------------------\n");
  double dt = ((double)(clock() - ctx.stime)) / CLOCKS_PER_SEC / ctx.threads;
  printf("%.2f s ~ %.2fM it/s\n", dt, ctx.checked / dt / 1000000);
  return 0;
}
