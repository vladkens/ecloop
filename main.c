#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/addr.c"
#include "lib/bench.c"
#include "lib/ecc.c"
#include "lib/util.c"

#define VERSION "0.2.1"
#define MAX_JOB_SIZE 1024 * 1024 * 2
#define GROUP_INV_SIZE 1024

typedef char hex40[41]; // rmd160 hex string
typedef char hex64[65]; // sha256 hex string

// MARK: args parser

typedef struct args_t {
  int argc;
  const char **argv;
} args_t;

bool args_bool(args_t *args, const char *name) {
  for (int i = 1; i < args->argc; ++i) {
    if (strcmp(args->argv[i], name) == 0) return true;
  }
  return false;
}

u64 args_int(args_t *args, const char *name, int def) {
  for (int i = 1; i < args->argc - 1; ++i) {
    if (strcmp(args->argv[i], name) == 0) {
      return strtoull(args->argv[i + 1], NULL, 10);
    }
  }
  return def;
}

char *arg_str(args_t *args, const char *name) {
  for (int i = 1; i < args->argc; ++i) {
    if (strcmp(args->argv[i], name) == 0) {
      if (i + 1 < args->argc) return (char *)args->argv[i + 1];
    }
  }
  return NULL;
}

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

// MARK: hex64 queue

typedef struct queue_item_t {
  size_t size;
  hex64 *data;
  struct queue_item_t *next;
} queue_item_t;

typedef struct queue_t {
  size_t capacity;
  size_t size;
  bool done;
  queue_item_t *head;
  queue_item_t *tail;
  pthread_mutex_t lock;
  pthread_cond_t cond_put;
  pthread_cond_t cond_get;
  size_t total_put;
  size_t total_get;
} queue_t;

void queue_init(queue_t *q, size_t capacity) {
  q->capacity = capacity;
  q->size = 0;
  q->done = false;
  q->head = NULL;
  q->tail = NULL;
  q->total_put = 0;
  q->total_get = 0;
  pthread_mutex_init(&q->lock, NULL);
  pthread_cond_init(&q->cond_put, NULL);
  pthread_cond_init(&q->cond_get, NULL);
}

void queue_done(queue_t *q) {
  pthread_mutex_lock(&q->lock);
  q->done = true;
  pthread_cond_broadcast(&q->cond_get);
  pthread_mutex_unlock(&q->lock);
}

void queue_put(queue_t *q, const hex64 data[], size_t size) {
  pthread_mutex_lock(&q->lock);
  if (q->done) {
    pthread_mutex_unlock(&q->lock);
    return;
  }

  while (q->size == q->capacity) {
    // printf("Waiting for put ~ %zu ~ %zu\n", q->total_put, q->total_get);
    pthread_cond_wait(&q->cond_put, &q->lock);
  }

  queue_item_t *item = malloc(sizeof(queue_item_t));
  item->size = size;
  item->data = malloc(size * sizeof(hex64));
  memcpy(item->data, data, size * sizeof(hex64));
  item->next = NULL;

  if (q->tail != NULL) q->tail->next = item;
  else q->head = item;

  q->tail = item;
  q->size += 1;
  q->total_put += 1;

  pthread_cond_signal(&q->cond_get);
  pthread_mutex_unlock(&q->lock);
}

void queue_get(queue_t *q, hex64 **data, size_t *size) {
  pthread_mutex_lock(&q->lock);
  while (q->size == 0 && !q->done) {
    // printf("Waiting for get ~ %zu ~ %zu\n", q->total_put, q->total_get);
    pthread_cond_wait(&q->cond_get, &q->lock);
  }

  free(*data);
  *data = NULL;
  *size = 0;

  if (q->size == 0) {
    pthread_mutex_unlock(&q->lock);
    return;
  }

  queue_item_t *item = q->head;
  q->head = item->next;
  if (!q->head) q->tail = NULL;

  *size = item->size;
  *data = item->data;
  free(item);

  q->size -= 1;
  q->total_get += 1;

  pthread_cond_signal(&q->cond_put);
  pthread_mutex_unlock(&q->lock);
}

// MARK: bloom filter

typedef struct blf_t {
  size_t size;
  u64 *bits;
} blf_t;

static inline void blf_setbit(blf_t *blf, size_t idx) {
  idx = idx % (blf->size * 64);
  blf->bits[idx / 64] |= (u64)1 << (idx % 64);
}

static inline bool blf_getbit(blf_t *blf, u64 idx) {
  idx = idx % (blf->size * 64);
  return (blf->bits[idx / 64] & ((u64)1 << (idx % 64))) != 0;
}

void blf_add(blf_t *blf, const h160_t hash) {
  u64 a1 = (u64)hash[0] << 32 | hash[1];
  u64 a2 = (u64)hash[2] << 32 | hash[3];
  u64 a3 = (u64)hash[4] << 32 | hash[0];
  u64 a4 = (u64)hash[1] << 32 | hash[2];
  u64 a5 = (u64)hash[3] << 32 | hash[4];

  u8 shifts[4] = {0, 48, 24, 16};
  for (size_t i = 0; i < 4; ++i) {
    u8 S = shifts[i];
    blf_setbit(blf, a1 << S | a2 >> S);
    blf_setbit(blf, a2 << S | a3 >> S);
    blf_setbit(blf, a3 << S | a4 >> S);
    blf_setbit(blf, a4 << S | a5 >> S);
    blf_setbit(blf, a5 << S | a1 >> S);
  }
}

bool blf_has(blf_t *blf, const h160_t hash) {
  u64 a1 = (u64)hash[0] << 32 | hash[1];
  u64 a2 = (u64)hash[2] << 32 | hash[3];
  u64 a3 = (u64)hash[4] << 32 | hash[0];
  u64 a4 = (u64)hash[1] << 32 | hash[2];
  u64 a5 = (u64)hash[3] << 32 | hash[4];

  u8 shifts[4] = {0, 48, 24, 16};
  for (size_t i = 0; i < 4; ++i) {
    u8 S = shifts[i];
    if (!blf_getbit(blf, a1 << S | a2 >> S)) return false;
    if (!blf_getbit(blf, a2 << S | a3 >> S)) return false;
    if (!blf_getbit(blf, a3 << S | a4 >> S)) return false;
    if (!blf_getbit(blf, a4 << S | a5 >> S)) return false;
    if (!blf_getbit(blf, a5 << S | a1 >> S)) return false;
  }

  return true;
}

void blf_gen_usage(args_t *args) {
  printf("Usage: %s blf-gen -n <count> -o <file>\n", args->argv[0]);
  printf("Generate a bloom filter from a list of hex-encoded hash160 values passed to stdin.\n");
  printf("\nOptions:\n");
  printf("  -n <count>      - Number of hashes to add.\n");
  printf("  -o <file>       - File to write bloom filter (must have a .blf extension).\n");
  exit(1);
}

void blf_gen(args_t *args) {
  u64 n = args_int(args, "-n", 0);
  if (n == 0) {
    fprintf(stderr, "[!] missing filter size (-n <number>)\n");
    return blf_gen_usage(args);
  }

  char *outfile = arg_str(args, "-o");
  if (outfile == NULL) {
    fprintf(stderr, "[!] missing output file (-o <file>)\n");
    return blf_gen_usage(args);
  }

  if (!strendswith(outfile, ".blf")) {
    fprintf(stderr, "output file should have .blf extension\n");
    exit(1);
  }

  if (access(outfile, F_OK) == 0) {
    fprintf(stderr, "output file already exists\n");
    exit(1);
  }

  // https://hur.st/bloomfilter/?n=500M&p=1000000&m=&k=20
  double p = 1.0 / 1000000.0;
  u64 m = (u64)(n * log(p) / log(1.0 / pow(2.0, log(2.0))));
  double size_mb = (double)m / 8 / 1024 / 1024;
  printf("creating bloom filter: n = %'llu | p = %f | m = %'llu (%'.1f MB)\n", n, p, m, size_mb);

  u64 size = (m + 63) / 64;
  u64 *bits = calloc(size, sizeof(u64));
  blf_t blf = {size, bits};

  u64 count = 0;
  hex40 line;
  while (fgets(line, sizeof(line), stdin) != NULL) {
    if (strlen(line) != sizeof(line) - 1) continue;

    h160_t hash;
    for (size_t j = 0; j < sizeof(line) - 1; j += 8) {
      sscanf(line + j, "%8x", &hash[j / 8]);
    }

    count += 1;
    blf_add(&blf, hash);
  }

  printf("added %'llu items; saving to %s\n", count, outfile);

  FILE *file = fopen(outfile, "wb");
  if (file == NULL) {
    fprintf(stderr, "failed to open output file\n");
    exit(1);
  }

  if (fwrite(&size, sizeof(size), 1, file) != 1) {
    fprintf(stderr, "failed to write bloom filter size\n");
    exit(1);
  }

  if (fwrite(bits, sizeof(u64), size, file) != size) {
    fprintf(stderr, "failed to write bloom filter bits\n");
    exit(1);
  }

  fclose(file);
  free(bits);
}

// MARK: shared context

enum Cmd { CMD_NIL, CMD_ADD, CMD_MUL };

typedef struct ctx_t {
  enum Cmd cmd;
  pthread_mutex_t lock;
  size_t threads_count;
  pthread_t *threads;
  size_t k_checked;
  size_t k_found;
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
  pe gpoints[GROUP_INV_SIZE];
  u64 job_size;

  // cmd mul
  queue_t queue;
} ctx_t;

void load_filter(ctx_t *ctx, const char *path) {
  if (!path) {
    fprintf(stderr, "missing filter file\n");
    exit(1);
  }

  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "failed to open filter file: %s\n", path);
    exit(1);
  }

  path = strrchr(path, '.');
  if (path != NULL && strcmp(path, ".blf") == 0) {
    size_t size;
    if (fread(&size, sizeof(size), 1, file) != 1) {
      fprintf(stderr, "failed to read bloom filter size\n");
      exit(1);
    }

    u64 *bits = malloc(size * sizeof(u64));
    if (fread(bits, sizeof(u64), size, file) != size) {
      fprintf(stderr, "failed to read bloom filter bits\n");
      exit(1);
    }

    ctx->blf.size = size;
    ctx->blf.bits = bits;
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

void ctx_print_status(ctx_t *ctx) {
  pthread_mutex_lock(&ctx->lock);
  double dt = (tsnow() - ctx->stime) / 1000.0;
  double it = ctx->k_checked / dt / 1000000;
  printf("\r%.2fs ~ %.2fM it/s ~ %'zu / %'zu", dt, it, ctx->k_found, ctx->k_checked);
  fflush(stdout);
  pthread_mutex_unlock(&ctx->lock);
}

void ctx_write_found(ctx_t *ctx, const char *label, const h160_t hash, const fe pk) {
  pthread_mutex_lock(&ctx->lock);

  if (!ctx->quiet) {
    printf("\r%s: %08x%08x%08x%08x%08x <- %016llx%016llx%016llx%016llx\n", //
           label, hash[0], hash[1], hash[2], hash[3], hash[4],             //
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
      ctx_print_status(ctx);
      found = true;
    }
  }

  if (ctx->check_addr65) {
    addr65(hash, cp);
    if (ctx_check_hash(ctx, hash)) {
      ctx_write_found(ctx, "addr65", hash, pk);
      ctx_print_status(ctx);
      found = true;
    }
  }

  return found;
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

      // ec_jacobi_add(&check_point, &check_point, &G1);
      // ec_jacobi_rdc(&check_point, &check_point);
      // if (fe_cmp(rx, check_point.x) != 0 || fe_cmp(ry, check_point.y) != 0) {
      //   printf("error on 0x%llx\n", counter + i);
      //   exit(1);
      // }
    }

    for (u64 i = 0; i < dx_size; ++i) {
      fe_add64(ck, 1);
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

  fe pk;
  while (true) {
    pthread_mutex_lock(&ctx->lock);
    if (fe_cmp(ctx->range_s, ctx->range_e) >= 0) {
      pthread_mutex_unlock(&ctx->lock);
      break;
    }

    fe_clone(pk, ctx->range_s);
    fe_add64(ctx->range_s, ctx->job_size);
    pthread_mutex_unlock(&ctx->lock);

    u64 found = batch_add(ctx, pk, ctx->job_size);

    pthread_mutex_lock(&ctx->lock);
    ctx->k_checked += ctx->job_size;
    ctx->k_found += found;
    pthread_mutex_unlock(&ctx->lock);
    ctx_print_status(ctx);
  }

  return NULL;
}

int cmd_add(ctx_t *ctx) {
  // precompute gpoints
  memcpy(ctx->gpoints + 0, &G1, sizeof(pe));
  memcpy(ctx->gpoints + 1, &G2, sizeof(pe));
  for (u64 i = 2; i < GROUP_INV_SIZE; ++i) {
    ec_jacobi_add(ctx->gpoints + i, ctx->gpoints + i - 1, &G1);
    ec_jacobi_rdc(ctx->gpoints + i, ctx->gpoints + i);
  }

  fe range_size;
  fe_modsub(range_size, ctx->range_e, ctx->range_s);
  ctx->job_size = fe_cmp64(range_size, MAX_JOB_SIZE) < 0 ? range_size[0] : MAX_JOB_SIZE;

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_create(&ctx->threads[i], NULL, cmd_add_worker, ctx);
  }

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_join(ctx->threads[i], NULL);
  }

  ctx_print_status(ctx);
  printf("\n");
  return 0;
}

// MARK: CMD_MUL

void *cmd_mul_worker(void *arg) {
  ctx_t *ctx = (ctx_t *)arg;

  hex64 *data = NULL;
  size_t size;

  fe pk[GROUP_INV_SIZE];
  pe cp[GROUP_INV_SIZE];

  while (true) {
    queue_get(&ctx->queue, &data, &size);
    if (size == 0) break;

    // parse private keys from hex string
    for (size_t i = 0; i < size; ++i) fe_from_hex(pk[i], data[i]);

    // compute public keys in batch
    for (size_t i = 0; i < size; ++i) ec_gtable_mul(&cp[i], pk[i]);
    ec_jacobi_grprdc(cp, size);

    size_t found = 0;
    for (size_t i = 0; i < size; ++i) {
      if (ctx_check_found(ctx, &cp[i], pk[i])) found += 1;
    }

    pthread_mutex_lock(&ctx->lock);
    ctx->k_checked += size;
    ctx->k_found += found;
    pthread_mutex_unlock(&ctx->lock);
    ctx_print_status(ctx);
  }

  return NULL;
}

int cmd_mul(ctx_t *ctx) {
  ec_gtable_init();

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_create(&ctx->threads[i], NULL, cmd_mul_worker, ctx);
  }

  hex64 data[GROUP_INV_SIZE];
  size_t count = 0;

  hex64 line;
  while (fgets(line, sizeof(line), stdin) != NULL) {
    if (strlen(line) != sizeof(line) - 1) continue;

    strcpy(data[count++ % GROUP_INV_SIZE], line);

    if (count == GROUP_INV_SIZE) {
      queue_put(&ctx->queue, data, count);
      count = 0;
    }
  }

  if (count > 0) queue_put(&ctx->queue, data, count);
  queue_done(&ctx->queue);

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_join(ctx->threads[i], NULL);
  }

  ctx_print_status(ctx);
  printf("\n");
  return 0;
}

// MARK: main

void usage(const char *name) {
  printf("Usage: %s <cmd> [-t <threads>] [-f <file>] [-a <addr_type>] [-r <range>]\n", name);
  printf("v%s ~ https://github.com/vladkens/ecloop\n", VERSION);
  printf("\nCompute commands:\n");
  printf("  add             - search in given range with batch addition\n");
  printf("  mul             - search hex encoded private keys (from stdin)\n");
  printf("\nCompute options:\n");
  printf("  -f <file>       - filter file to search (list of hashes or bloom fitler)\n");
  printf("  -o <file>       - output file to write found keys (default: stdout)\n");
  printf("  -t <threads>    - number of threads to run (default: 1)\n");
  printf("  -a <addr_type>  - address type to search: c - addr33, u - addr65 (default: c)\n");
  printf("  -r <range>      - search range in hex format (example: 8000:ffff, default all)\n");
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

  ctx->cmd = CMD_NIL; // default to batch add
  if (args->argc > 1) {
    if (strcmp(args->argv[1], "add") == 0) ctx->cmd = CMD_ADD;
    if (strcmp(args->argv[1], "mul") == 0) ctx->cmd = CMD_MUL;
  }

  if (ctx->cmd == CMD_NIL) {
    if (args_bool(args, "-v")) printf("ecloop v%s\n", VERSION);
    else usage(args->argv[0]);
    exit(0);
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
  ctx->threads_count = MIN(MAX(args_int(args, "-t", 1), 1), 128);
  ctx->threads = malloc(ctx->threads_count * sizeof(pthread_t));
  ctx->k_checked = 0;
  ctx->k_found = 0;
  ctx->stime = tsnow();

  arg_search_range(args, ctx->range_s, ctx->range_e);
  queue_init(&ctx->queue, ctx->threads_count * 3);

  printf("threads: %zu ~ addr33: %d ~ addr65: %d | filter: ", //
         ctx->threads_count, ctx->check_addr33, ctx->check_addr65);

  if (ctx->to_find_hashes != NULL) printf("list (%'zu)\n", ctx->to_find_count);
  else printf("bloom\n");

  if (ctx->cmd == CMD_ADD) {
    fe_print("range_s", ctx->range_s);
    fe_print("range_e", ctx->range_e);
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

  if (ctx.outfile != NULL) fclose(ctx.outfile);
  return 0;
}
