// Copyright (c) vladkens
// https://github.com/vladkens/ecloop
// Licensed under the MIT License.

#include <locale.h>
#include <stdbool.h>
#include <string.h>

#include "lib/addr.c"
#include "lib/bench.c"
#include "lib/ecc.c"
#include "lib/utils.c"

#define VERSION "0.3.0"
#define MAX_JOB_SIZE 1024 * 1024 * 2
#define GROUP_INV_SIZE 1024
#define MAX_LINE_SIZE 128

enum Cmd { CMD_NIL, CMD_ADD, CMD_MUL, CMD_RND };

typedef struct ctx_t {
  enum Cmd cmd;
  pthread_mutex_t lock;
  size_t threads_count;
  pthread_t *threads;
  u64 k_checked;
  u64 k_found;
  size_t stime;
  bool check_addr33;
  bool check_addr65;

  FILE *outfile;
  bool quiet;

  // filter file (bloom filter or hashes to search)
  h160_t *to_find_hashes;
  size_t to_find_count;
  blf_t blf;

  // cmd add
  fe range_s; // search range start
  fe range_e; // search range end
  fe gs;      // base step for G-point grid (2^offset)
  pe gpoints[GROUP_INV_SIZE];
  u64 job_size;

  // cmd mul
  queue_t queue;
  bool raw_text;

  // cmd rnd
  bool has_seed;
  u32 ord_offs; // offset (order) of range to search
  u32 ord_size; // size (span) in range to search
} ctx_t;

int get_cpu_count() {
  int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
  if (cpu_count == -1) {
    perror("sysconf: unable to get CPU count, defaulting to 1");
    return 1;
  }
  return cpu_count;
}

void load_filter(ctx_t *ctx, const char *filepath) {
  if (!filepath) {
    fprintf(stderr, "missing filter file\n");
    exit(1);
  }

  FILE *file = fopen(filepath, "rb");
  if (!file) {
    fprintf(stderr, "failed to open filter file: %s\n", filepath);
    exit(1);
  }

  char *ext = strrchr(filepath, '.');
  if (ext != NULL && strcmp(ext, ".blf") == 0) {
    if (!blf_load(filepath, &ctx->blf)) exit(1);
  } else {
    size_t capacity = 32;
    size_t size = 0;
    u32 *hashes = malloc(capacity * sizeof(u32) * 5);

    hex40 line;
    while (fgets(line, sizeof(line), file)) {
      if (strlen(line) != sizeof(line) - 1) continue;

      if (size >= capacity) {
        capacity *= 2;
        hashes = realloc(hashes, capacity * sizeof(u32) * 5);
      }

      for (size_t j = 0; j < sizeof(line) - 1; j += 8) {
        sscanf(line + j, "%8x", &hashes[size * 5 + j / 8]);
      }

      size += 1;
    }

    qsort(hashes, size, 5 * sizeof(u32), compare_160);
    ctx->to_find_hashes = (h160_t *)hashes;
    ctx->to_find_count = size;
  }

  fclose(file);
}

void ctx_print_status(ctx_t *ctx, bool final) {
  pthread_mutex_lock(&ctx->lock);
  double dt = (tsnow() - ctx->stime) / 1000.0;
  double it = ctx->k_checked / dt / 1000000;
  term_clear_line();
  fprintf(stderr, "%.2fs ~ %.2fM it/s ~ %'llu / %'llu%c", //
          dt, it, ctx->k_found, ctx->k_checked, final ? '\n' : '\r');
  pthread_mutex_unlock(&ctx->lock);

  fflush(stdout);
  fflush(stderr);
}

void ctx_write_found(ctx_t *ctx, const char *label, const h160_t hash, const fe pk) {
  pthread_mutex_lock(&ctx->lock);

  if (!ctx->quiet) {
    term_clear_line();
    printf("%s: %08x%08x%08x%08x%08x <- %016llx%016llx%016llx%016llx\n", //
           label, hash[0], hash[1], hash[2], hash[3], hash[4],           //
           pk[3], pk[2], pk[1], pk[0]);
  }

  if (ctx->outfile != NULL) {
    fprintf(ctx->outfile, "%s\t%08x%08x%08x%08x%08x\t%016llx%016llx%016llx%016llx\n", //
            label, hash[0], hash[1], hash[2], hash[3], hash[4],                       //
            pk[3], pk[2], pk[1], pk[0]);
    fflush(ctx->outfile);
  }

  pthread_mutex_unlock(&ctx->lock);
}

bool ctx_check_hash(ctx_t *ctx, const h160_t h) {
  if (ctx->to_find_hashes == NULL) {
    return blf_has(&ctx->blf, h);
  } else {
    h160_t *rs = bsearch(h, ctx->to_find_hashes, ctx->to_find_count, sizeof(h160_t), compare_160);
    if (rs) return true;
  }
  return false;
}

bool ctx_check_found(ctx_t *ctx, const pe *cp, const fe pk) {
  h160_t hash;
  bool found = false;

  if (ctx->check_addr33) {
    addr33(hash, cp);
    if (ctx_check_hash(ctx, hash)) {
      ctx_write_found(ctx, "addr33", hash, pk);
      ctx_print_status(ctx, false);
      found = true;
    }
  }

  if (ctx->check_addr65) {
    addr65(hash, cp);
    if (ctx_check_hash(ctx, hash)) {
      ctx_write_found(ctx, "addr65", hash, pk);
      ctx_print_status(ctx, false);
      found = true;
    }
  }

  return found;
}

void ctx_precompute_gpoints(ctx_t *ctx) {
  // precompute gpoints (Gi, Gi*2, Gi*3, ...)
  fe gs = {0};
  fe_set64(gs, 1);
  fe_shiftl(gs, ctx->ord_offs);
  fe_clone(ctx->gs, gs);

  pe g1, g2;
  ec_jacobi_mul(&g1, &G1, gs);
  ec_jacobi_dbl(&g2, &g1); // ecc can't calc Gi + Gi directly, so DBL(Gi) used
  ec_jacobi_rdc(&g1, &g1);
  ec_jacobi_rdc(&g2, &g2);

  memcpy(ctx->gpoints + 0, &g1, sizeof(pe));
  memcpy(ctx->gpoints + 1, &g2, sizeof(pe));
  for (u64 i = 2; i < GROUP_INV_SIZE; ++i) {
    ec_jacobi_add(ctx->gpoints + i, ctx->gpoints + i - 1, &g1);
    ec_jacobi_rdc(ctx->gpoints + i, ctx->gpoints + i);
  }
}

// MARK: CMD_ADD

u64 batch_add(ctx_t *ctx, const fe pk, const u64 iterations) {
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

      // ec_jacobi_add(&check_point, &check_point, &ctx->gpoints[0]);
      // ec_jacobi_rdc(&check_point, &check_point);
      // if (fe_cmp(rx, check_point.x) != 0 || fe_cmp(ry, check_point.y) != 0) {
      //   printf("error on 0x%llx\n", counter + i);
      //   exit(1);
      // }
    }

    for (u64 i = 0; i < dx_size; ++i) {
      fe_modadd(ck, ck, ctx->gs);
      if (ctx_check_found(ctx, &bp[i], ck)) found += 1;
    }

    memcpy(&start_point, &bp[dx_size - 1], sizeof(pe));
    counter += dx_size;
  }

  free(bp);
  return found;
}

void *cmd_add_worker(void *arg) {
  ctx_t *ctx = (ctx_t *)arg;

  fe initial_r; // keep initial range start to check overflow
  fe_clone(initial_r, ctx->range_s);

  // job_size multiply by 2^offset (iterate over desired digit order)
  // for example: 3013 3023 .. 30X3 .. 3093 3103 3113
  fe inc = {0};
  fe_set64(inc, ctx->job_size);
  fe_modmul(inc, inc, ctx->gs);

  fe pk;
  while (true) {
    pthread_mutex_lock(&ctx->lock);
    bool is_overflow = fe_cmp(ctx->range_s, initial_r) < 0;
    if (fe_cmp(ctx->range_s, ctx->range_e) >= 0 || is_overflow) {
      pthread_mutex_unlock(&ctx->lock);
      break;
    }

    fe_clone(pk, ctx->range_s);
    fe_modadd(ctx->range_s, ctx->range_s, inc);
    pthread_mutex_unlock(&ctx->lock);

    u64 found = batch_add(ctx, pk, ctx->job_size);

    pthread_mutex_lock(&ctx->lock);
    ctx->k_checked += ctx->job_size;
    ctx->k_found += found;
    pthread_mutex_unlock(&ctx->lock);
    ctx_print_status(ctx, false);
  }

  return NULL;
}

int cmd_add(ctx_t *ctx) {
  ctx_precompute_gpoints(ctx);

  fe range_size;
  fe_modsub(range_size, ctx->range_e, ctx->range_s);
  ctx->job_size = fe_cmp64(range_size, MAX_JOB_SIZE) < 0 ? range_size[0] : MAX_JOB_SIZE;

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_create(&ctx->threads[i], NULL, cmd_add_worker, ctx);
  }

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_join(ctx->threads[i], NULL);
  }

  ctx_print_status(ctx, true);
  return 0;
}

// MARK: CMD_MUL

typedef struct cmd_mul_job_t {
  size_t count;
  char lines[GROUP_INV_SIZE][MAX_LINE_SIZE];
} cmd_mul_job_t;

void *cmd_mul_worker(void *arg) {
  ctx_t *ctx = (ctx_t *)arg;

  // sha256 routine
  u8 msg[(MAX_LINE_SIZE + 63 + 9) / 64 * 64] = {0}; // 9 = 1 byte 0x80 + 8 byte bitlen
  u32 res[8] = {0};

  fe pk[GROUP_INV_SIZE];
  pe cp[GROUP_INV_SIZE];
  cmd_mul_job_t *job = NULL;

  while (true) {
    if (job != NULL) free(job);
    job = queue_get(&ctx->queue);
    if (job == NULL) break;

    // parse private keys from hex string
    if (!ctx->raw_text) {
      for (size_t i = 0; i < job->count; ++i) fe_from_hex(pk[i], job->lines[i]);
    } else {
      for (size_t i = 0; i < job->count; ++i) {
        size_t len = strlen(job->lines[i]);
        size_t msg_size = (len + 63 + 9) / 64 * 64;

        // caclulate sha256 hash
        size_t bitlen = len * 8;
        memcpy(msg, job->lines[i], len);
        memset(msg + len, 0, msg_size - len);
        msg[len] = 0x80;
        for (int j = 0; j < 8; j++) msg[msg_size - 1 - j] = bitlen >> (j * 8);
        sha256_final(res, (u8 *)msg, msg_size);

        // debug log (do with `-t 1`)
        // printf("\n%zu %s\n", len, job->lines[i]);
        // for (int i = 0; i < msg_size; i++) printf("%02x%s", msg[i], i % 16 == 15 ? "\n" : " ");
        // for (int i = 0; i < 8; i++) printf("%08x%s", res[i], i % 8 == 7 ? "\n" : "");

        pk[i][0] = (u64)res[6] << 32 | res[7];
        pk[i][1] = (u64)res[4] << 32 | res[5];
        pk[i][2] = (u64)res[2] << 32 | res[3];
        pk[i][3] = (u64)res[0] << 32 | res[1];
      }
    }

    // compute public keys in batch
    for (size_t i = 0; i < job->count; ++i) ec_gtable_mul(&cp[i], pk[i]);
    ec_jacobi_grprdc(cp, job->count);

    size_t found = 0;
    for (size_t i = 0; i < job->count; ++i) {
      if (ctx_check_found(ctx, &cp[i], pk[i])) found += 1;
    }

    pthread_mutex_lock(&ctx->lock);
    ctx->k_checked += job->count;
    ctx->k_found += found;
    pthread_mutex_unlock(&ctx->lock);
    ctx_print_status(ctx, false);
  }

  if (job != NULL) free(job);
  return NULL;
}

int cmd_mul(ctx_t *ctx) {
  ec_gtable_init();

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_create(&ctx->threads[i], NULL, cmd_mul_worker, ctx);
  }

  cmd_mul_job_t *job = calloc(1, sizeof(cmd_mul_job_t));
  char line[MAX_LINE_SIZE];

  while (fgets(line, sizeof(line), stdin) != NULL) {
    size_t len = strlen(line);
    if (len && line[len - 1] == '\n') line[--len] = '\0';
    if (len && line[len - 1] == '\r') line[--len] = '\0';
    if (len == 0) continue;

    strcpy(job->lines[job->count++], line);
    if (job->count == GROUP_INV_SIZE) {
      queue_put(&ctx->queue, job);
      job = calloc(1, sizeof(cmd_mul_job_t));
    }
  }

  if (job->count > 0 && job->count != GROUP_INV_SIZE) {
    queue_put(&ctx->queue, job);
  }

  queue_done(&ctx->queue);

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_join(ctx->threads[i], NULL);
  }

  ctx_print_status(ctx, true);
  return 0;
}

// MARK: CMD_RND

void gen_random_range(ctx_t *ctx, const fe a, const fe b) {
  fe_rand_range(ctx->range_s, a, b, !ctx->has_seed);
  fe_clone(ctx->range_e, ctx->range_s);
  for (u32 i = ctx->ord_offs; i < (ctx->ord_offs + ctx->ord_size); ++i) {
    ctx->range_s[i / 64] &= ~(1ULL << (i % 64));
    ctx->range_e[i / 64] |= 1ULL << (i % 64);
  }
}

void print_range_mask(fe range_s, u32 bits_size, u32 offset) {
  int mask_e = 255 - offset;
  int mask_s = mask_e - bits_size + 1;

  for (int i = 0; i < 64; i++) {
    if (i % 16 == 0 && i != 0) putchar(' ');

    int bits_s = i * 4;
    int bits_e = bits_s + 3;

    u32 fcc = (range_s[(255 - bits_e) / 64] >> ((255 - bits_e) % 64)) & 0xF;
    char cc = "0123456789abcdef"[fcc];

    bool flag = (bits_s >= mask_s && bits_s <= mask_e) || (bits_e >= mask_s && bits_e <= mask_e);
    if (flag) {
      term_color_yellow();
      putchar(cc);
      term_color_reset();
    } else {
      putchar(cc);
    }
  }

  putchar('\n');
}

int cmd_rnd(ctx_t *ctx) {
  ctx->ord_offs = MIN(ctx->ord_offs, 255 - ctx->ord_size);
  printf("[RANDOM MODE] offs: %d ~ bits: %d\n\n", ctx->ord_offs, ctx->ord_size);

  ctx_precompute_gpoints(ctx);
  ctx->job_size = MAX_JOB_SIZE;

  fe range_s, range_e;
  fe_clone(range_s, ctx->range_s);
  fe_clone(range_e, ctx->range_e);

  u64 last_c = 0, last_f = 0, s_time = tsnow();
  while (true) {
    last_c = ctx->k_checked;
    last_f = ctx->k_found;
    s_time = tsnow();

    gen_random_range(ctx, range_s, range_e);
    print_range_mask(ctx->range_s, ctx->ord_size, ctx->ord_offs);
    print_range_mask(ctx->range_e, ctx->ord_size, ctx->ord_offs);
    ctx_print_status(ctx, false);

    for (size_t i = 0; i < ctx->threads_count; ++i) {
      pthread_create(&ctx->threads[i], NULL, cmd_add_worker, ctx);
    }

    for (size_t i = 0; i < ctx->threads_count; ++i) {
      pthread_join(ctx->threads[i], NULL);
    }

    u64 dc = ctx->k_checked - last_c, df = ctx->k_found - last_f;
    double dt = MAX((tsnow() - s_time), 1) / 1000.0;
    term_clear_line();
    printf("%'llu / %'llu ~ %.1fs\n\n", df, dc, dt);
  }

  return 0;
}

// MARK: args helpers

void arg_search_range(args_t *args, fe range_s, fe range_e) {
  char *raw = arg_str(args, "-r");
  if (!raw) {
    fe_set64(range_s, GROUP_INV_SIZE);
    fe_clone(range_e, P);
    return;
  }

  char *sep = strchr(raw, ':');
  if (!sep) {
    fprintf(stderr, "invalid search range, use format: -r 8000:ffff\n");
    exit(1);
  }

  *sep = 0;
  fe_from_hex(range_s, raw);
  fe_from_hex(range_e, sep + 1);

  if (fe_cmp64(range_s, GROUP_INV_SIZE) < 0) fe_set64(range_s, GROUP_INV_SIZE);
  if (fe_cmp(range_e, P) > 0) fe_clone(range_e, P);

  if (fe_cmp(range_s, range_e) >= 0) {
    fprintf(stderr, "invalid search range, start >= end\n");
    exit(1);
  }
}

void load_offs_size(ctx_t *ctx, args_t *args) {
  const u32 MIN_SIZE = 20;
  const u32 MAX_SIZE = 64;

  u32 range_bits = fe_bitlen(ctx->range_e);
  u32 default_bits = range_bits < 32 ? MAX(MIN_SIZE, range_bits) : 32;
  u32 max_offs = MAX(1, MAX(MIN_SIZE, range_bits) - default_bits);

  char *raw = arg_str(args, "-d");
  if (!raw && ctx->cmd == CMD_RND) {
    ctx->ord_offs = rand64(!ctx->has_seed) % max_offs;
    ctx->ord_size = default_bits;
    return;
  }

  if (!raw) {
    ctx->ord_offs = 0;
    ctx->ord_size = default_bits;
    return;
  }

  char *sep = strchr(raw, ':');
  if (!sep) {
    fprintf(stderr, "invalid offset:size format, use format: -d 128:32\n");
    exit(1);
  }

  *sep = 0;
  u32 tmp_offs = atoi(raw);
  u32 tmp_size = atoi(sep + 1);

  if (tmp_offs > 255) {
    fprintf(stderr, "invalid offset, max is 255\n");
    exit(1);
  }

  if (tmp_size < MIN_SIZE || tmp_size > MAX_SIZE) {
    fprintf(stderr, "invalid size, min is %d and max is %d\n", MIN_SIZE, MAX_SIZE);
    exit(1);
  }

  ctx->ord_offs = tmp_offs;
  ctx->ord_size = tmp_size;
}

// MARK: main

void usage(const char *name) {
  printf("Usage: %s <cmd> [-t <threads>] [-f <file>] [-a <addr_type>] [-r <range>]\n", name);
  printf("v%s ~ https://github.com/vladkens/ecloop\n", VERSION);
  printf("\nCompute commands:\n");
  printf("  add             - search in given range with batch addition\n");
  printf("  mul             - search hex encoded private keys (from stdin)\n");
  printf("  rnd             - search random range of bits in given range\n");
  printf("\nCompute options:\n");
  printf("  -f <file>       - filter file to search (list of hashes or bloom fitler)\n");
  printf("  -o <file>       - output file to write found keys (default: stdout)\n");
  printf("  -t <threads>    - number of threads to run (default: 1)\n");
  printf("  -a <addr_type>  - address type to search: c - addr33, u - addr65 (default: c)\n");
  printf("  -r <range>      - search range in hex format (example: 8000:ffff, default all)\n");
  printf("  -d <offs:size>  - bit offset and size for search (example: 128:32, default: 0:32)\n");
  printf("  -q              - quiet mode (no output to stdout; -o required)\n");
  printf("\nOther commands:\n");
  printf("  blf-gen         - create bloom filter from list of hex-encoded hash160\n");
  printf("  bench           - run benchmark of internal functions\n");
  printf("  bench-gtable    - run benchmark of ecc multiplication (with different table size)\n");
  printf("\n");
}

void init(ctx_t *ctx, args_t *args) {
  // check other commands first
  if (args->argc > 1) {
    if (strcmp(args->argv[1], "blf-gen") == 0) return blf_gen(args);
    if (strcmp(args->argv[1], "bench") == 0) return run_bench();
    if (strcmp(args->argv[1], "bench-gtable") == 0) return run_bench_gtable();
  }

  ctx->cmd = CMD_NIL; // default show help
  if (args->argc > 1) {
    if (strcmp(args->argv[1], "add") == 0) ctx->cmd = CMD_ADD;
    if (strcmp(args->argv[1], "mul") == 0) ctx->cmd = CMD_MUL;
    if (strcmp(args->argv[1], "rnd") == 0) ctx->cmd = CMD_RND;
  }

  if (ctx->cmd == CMD_NIL) {
    if (args_bool(args, "-v")) printf("ecloop v%s\n", VERSION);
    else usage(args->argv[0]);
    exit(0);
  }

  ctx->has_seed = false;
  char *seed = arg_str(args, "-seed");
  if (seed != NULL) {
    ctx->has_seed = true;
    srand(encode_seed(seed));
  }

  char *path = arg_str(args, "-f");
  load_filter(ctx, path);

  ctx->quiet = args_bool(args, "-q");
  char *outfile = arg_str(args, "-o");
  if (outfile) ctx->outfile = fopen(outfile, "a");

  if (outfile == NULL && ctx->quiet) {
    fprintf(stderr, "quiet mode chosen without output file\n");
    exit(1);
  }

  char *addr = arg_str(args, "-a");
  if (addr) {
    ctx->check_addr33 = strstr(addr, "c") != NULL;
    ctx->check_addr65 = strstr(addr, "u") != NULL;
  }

  if (!ctx->check_addr33 && !ctx->check_addr65) {
    ctx->check_addr33 = true; // default to addr33
  }

  pthread_mutex_init(&ctx->lock, NULL);
  int cpus = get_cpu_count();
  ctx->threads_count = MIN(MAX(args_int(args, "-t", cpus), 1), 128);
  ctx->threads = malloc(ctx->threads_count * sizeof(pthread_t));
  ctx->k_checked = 0;
  ctx->k_found = 0;
  ctx->stime = tsnow();

  arg_search_range(args, ctx->range_s, ctx->range_e);
  load_offs_size(ctx, args);
  queue_init(&ctx->queue, ctx->threads_count * 3);

  printf("threads: %zu ~ addr33: %d ~ addr65: %d | filter: ", //
         ctx->threads_count, ctx->check_addr33, ctx->check_addr65);

  if (ctx->to_find_hashes != NULL) printf("list (%'zu)\n", ctx->to_find_count);
  else printf("bloom\n");

  if (ctx->cmd == CMD_ADD) {
    fe_print("range_s", ctx->range_s);
    fe_print("range_e", ctx->range_e);
  }

  if (ctx->cmd == CMD_MUL) {
    ctx->raw_text = args_bool(args, "-raw");
  }

  printf("----------------------------------------\n");
}

int main(int argc, const char **argv) {
  // https://stackoverflow.com/a/11695246
  setlocale(LC_NUMERIC, ""); // for comma separated numbers

  args_t args = {argc, argv};

  ctx_t ctx;
  init(&ctx, &args);

  if (ctx.cmd == CMD_ADD) cmd_add(&ctx);
  if (ctx.cmd == CMD_MUL) cmd_mul(&ctx);
  if (ctx.cmd == CMD_RND) cmd_rnd(&ctx);

  if (ctx.outfile != NULL) fclose(ctx.outfile);
  return 0;
}
