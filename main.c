// Copyright (c) vladkens
// https://github.com/vladkens/ecloop
// Licensed under the MIT License.

#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <termios.h>

#include "lib/addr.c"
#include "lib/bench.c"
#include "lib/ecc.c"
#include "lib/utils.c"

#define VERSION "0.5.0"
#define MAX_JOB_SIZE 1024 * 1024 * 2
#define GROUP_INV_SIZE 2048ul
#define MAX_LINE_SIZE 1025

static_assert(GROUP_INV_SIZE % HASH_BATCH_SIZE == 0,
              "GROUP_INV_SIZE must be divisible by HASH_BATCH_SIZE");

enum Cmd { CMD_NIL, CMD_ADD, CMD_MUL, CMD_RND };

typedef struct ctx_t {
  enum Cmd cmd;
  pthread_mutex_t lock;
  size_t threads_count;
  pthread_t *threads;
  size_t k_checked;
  size_t k_found;
  bool check_addr33;
  bool check_addr65;
  bool use_endo;

  FILE *outfile;
  bool quiet;
  bool use_color;

  bool finished;      // true if the program is exiting
  bool paused;        // true if the program is paused
  size_t ts_started;  // timestamp of start
  size_t ts_updated;  // timestamp of last update
  size_t ts_printed;  // timestamp of last print
  size_t paused_time; // time spent in paused state

  // filter file (bloom filter or hashes to search)
  h160_t *to_find_hashes;
  size_t to_find_count;
  blf_t blf;

  // cmd add
  fe range_s;  // search range start
  fe range_e;  // search range end
  fe stride_k; // precomputed stride key (step for G-points, 2^offset)
  pe stride_p; // precomputed stride point (G * pk)
  pe gpoints[GROUP_INV_SIZE];
  size_t job_size;

  // cmd mul
  queue_t queue;
  bool raw_text;

  // cmd rnd
  bool has_seed;
  u32 ord_offs; // offset (order) of range to search
  u32 ord_size; // size (span) in range to search
} ctx_t;

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
    fclose(file);
    return;
  }

  size_t hlen = sizeof(u32) * 5;
  assert(hlen == sizeof(h160_t));
  size_t capacity = 32;
  size_t size = 0;
  u32 *hashes = malloc(capacity * hlen);

  hex40 line;
  while (fgets(line, sizeof(line), file)) {
    if (strlen(line) != sizeof(line) - 1) continue;

    if (size >= capacity) {
      capacity *= 2;
      hashes = realloc(hashes, capacity * hlen);
    }

    for (size_t j = 0; j < sizeof(line) - 1; j += 8) {
      sscanf(line + j, "%8x", &hashes[size * 5 + j / 8]);
    }

    size += 1;
  }

  fclose(file);
  qsort(hashes, size, hlen, compare_160);

  // remove duplicates
  size_t unique_count = 0;
  for (size_t i = 1; i < size; ++i) {
    if (memcmp(&hashes[unique_count * 5], &hashes[i * 5], hlen) != 0) {
      unique_count++;
      memcpy(&hashes[unique_count * 5], &hashes[i * 5], hlen);
    }
  }

  ctx->to_find_hashes = (h160_t *)hashes;
  ctx->to_find_count = unique_count + 1;

  // generate in-memory bloom filter
  ctx->blf.size = ctx->to_find_count * 2;
  ctx->blf.bits = malloc(ctx->blf.size * sizeof(u64));
  for (size_t i = 0; i < ctx->to_find_count; ++i) blf_add(&ctx->blf, hashes + i * 5);
}

// note: this function is not thread-safe; use mutex lock before calling
void ctx_print_unlocked(ctx_t *ctx) {
  char *msg = ctx->finished ? "" : (ctx->paused ? " ('r' – resume)" : " ('p' – pause)");

  int64_t effective_time = (int64_t)(ctx->ts_updated - ctx->ts_started) - (int64_t)ctx->paused_time;
  double dt = MAX(1, effective_time) / 1000.0;
  double it = ctx->k_checked / dt / 1000000;
  term_clear_line();
  fprintf(stderr, "%.2fs ~ %.2f Mkeys/s ~ %'zu / %'zu%s%c", //
          dt, it, ctx->k_found, ctx->k_checked, msg, ctx->finished ? '\n' : '\r');
  fflush(stderr);
}

void ctx_print_status(ctx_t *ctx) {
  pthread_mutex_lock(&ctx->lock);
  ctx_print_unlocked(ctx);
  pthread_mutex_unlock(&ctx->lock);
}

void ctx_check_paused(ctx_t *ctx) {
  if (ctx->paused) {
    while (ctx->paused) usleep(100000);
  }
}

void ctx_update(ctx_t *ctx, size_t k_checked) {
  size_t ts = tsnow();

  pthread_mutex_lock(&ctx->lock);
  bool need_print = (ts - ctx->ts_printed) >= 100;
  ctx->k_checked += k_checked;
  ctx->ts_updated = ts;
  if (need_print) {
    ctx->ts_printed = ts;
    ctx_print_unlocked(ctx);
  }
  pthread_mutex_unlock(&ctx->lock);

  ctx_check_paused(ctx);
}

void ctx_finish(ctx_t *ctx) {
  pthread_mutex_lock(&ctx->lock);
  ctx->finished = true;
  ctx_print_unlocked(ctx);
  if (ctx->outfile != NULL) fclose(ctx->outfile);
  pthread_mutex_unlock(&ctx->lock);
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

  ctx->k_found += 1;
  ctx_print_unlocked(ctx);

  pthread_mutex_unlock(&ctx->lock);
}

bool ctx_check_hash(ctx_t *ctx, const h160_t h) {
  // bloom filter only mode
  if (ctx->to_find_hashes == NULL) {
    return blf_has(&ctx->blf, h);
  }

  // check by hashes list
  if (!blf_has(&ctx->blf, h)) return false; // fast check with bloom filter

  // if bloom filter check passed, do full check
  h160_t *rs = bsearch(h, ctx->to_find_hashes, ctx->to_find_count, sizeof(h160_t), compare_160);
  return rs != NULL;
}

void ctx_precompute_gpoints(ctx_t *ctx) {
  // precalc addition step with stride (2^offset)
  fe_set64(ctx->stride_k, 1);
  fe_shiftl(ctx->stride_k, ctx->ord_offs);

  fe t; // precalc stride point
  fe_modn_add_stride(t, FE_ZERO, ctx->stride_k, GROUP_INV_SIZE);
  ec_jacobi_mulrdc(&ctx->stride_p, &G1, t); // G * (GROUP_INV_SIZE * gs)

  pe g1, g2;
  ec_jacobi_mulrdc(&g1, &G1, ctx->stride_k);
  ec_jacobi_dblrdc(&g2, &g1);

  size_t hsize = GROUP_INV_SIZE / 2;

  // K+1, K+2, .., K+N/2-1
  pe_clone(ctx->gpoints + 0, &g1);
  pe_clone(ctx->gpoints + 1, &g2);
  for (size_t i = 2; i < hsize; ++i) {
    ec_jacobi_addrdc(ctx->gpoints + i, ctx->gpoints + i - 1, &g1);
  }

  // K-1, K-2, .., K-N/2
  for (size_t i = 0; i < hsize; ++i) {
    pe_clone(&ctx->gpoints[hsize + i], &ctx->gpoints[i]);
    fe_modp_neg(ctx->gpoints[hsize + i].y, ctx->gpoints[i].y); // y = -y
  }
}

void pk_verify_hash(const fe pk, const h160_t hash, bool c, size_t endo) {
  pe point;
  ec_jacobi_mulrdc(&point, &G1, pk);

  h160_t h;
  c ? addr33(h, &point) : addr65(h, &point);

  bool is_equal = memcmp(h, hash, sizeof(h160_t)) == 0;
  if (!is_equal) {
    fprintf(stderr, "[!] error: hash mismatch (compressed: %d endo: %zu)\n", c, endo);
    fprintf(stderr, "pk: %016llx%016llx%016llx%016llx\n", pk[3], pk[2], pk[1], pk[0]);
    fprintf(stderr, "lh: %08x%08x%08x%08x%08x\n", hash[0], hash[1], hash[2], hash[3], hash[4]);
    fprintf(stderr, "rh: %08x%08x%08x%08x%08x\n", h[0], h[1], h[2], h[3], h[4]);
    exit(1);
  }
}

// MARK: CMD_ADD

void calc_priv(fe pk, const fe start_pk, const fe stride_k, size_t pk_off, u8 endo) {
  fe_modn_add_stride(pk, start_pk, stride_k, pk_off);

  if (endo == 0) return;
  if (endo == 1) fe_modn_neg(pk, pk);
  if (endo == 2 || endo == 3) fe_modn_mul(pk, pk, A1);
  if (endo == 3) fe_modn_neg(pk, pk);
  if (endo == 4 || endo == 5) fe_modn_mul(pk, pk, A2);
  if (endo == 5) fe_modn_neg(pk, pk);
}

void check_hash(ctx_t *ctx, bool c, const h160_t h, const fe start_pk, u64 pk_off, size_t endo) {
  if (!ctx_check_hash(ctx, h)) return;

  fe ck;
  calc_priv(ck, start_pk, ctx->stride_k, pk_off, endo);
  pk_verify_hash(ck, h, c, endo);
  ctx_write_found(ctx, c ? "addr33" : "addr65", h, ck);
}

void check_found_add(ctx_t *ctx, fe const start_pk, const pe *points) {
  h160_t hs33[HASH_BATCH_SIZE];
  h160_t hs65[HASH_BATCH_SIZE];

  for (size_t i = 0; i < GROUP_INV_SIZE; i += HASH_BATCH_SIZE) {
    if (ctx->check_addr33) addr33_batch(hs33, points + i, HASH_BATCH_SIZE);
    if (ctx->check_addr65) addr65_batch(hs65, points + i, HASH_BATCH_SIZE);
    for (size_t j = 0; j < HASH_BATCH_SIZE; ++j) {
      if (ctx->check_addr33) check_hash(ctx, true, hs33[j], start_pk, i + j, 0);
      if (ctx->check_addr33) check_hash(ctx, false, hs65[j], start_pk, i + j, 0);
    }
  }

  if (!ctx->use_endo) return;

  // https://bitcointalk.org/index.php?topic=5527935.msg65000919#msg65000919
  // PubKeys  = (x,y) (x,-y) (x*beta,y) (x*beta,-y) (x*beta^2,y) (x*beta^2,-y)
  // PrivKeys = (pk) (!pk) (pk*alpha) !(pk*alpha) (pk*alpha^2) !(pk*alpha^2)

  size_t esize = HASH_BATCH_SIZE * 5;
  pe endos[esize];
  for (size_t i = 0; i < esize; ++i) fe_set64(endos[i].z, 1);

  size_t ci = 0;
  for (size_t k = 0; k < GROUP_INV_SIZE; ++k) {
    size_t idx = (k * 5) % esize;

    fe_clone(endos[idx + 0].x, points[k].x); // (x, -y)
    fe_modp_neg(endos[idx + 0].y, points[k].y);

    fe_modp_mul(endos[idx + 1].x, points[k].x, B1); // (x * beta, y)
    fe_clone(endos[idx + 1].y, points[k].y);

    fe_clone(endos[idx + 2].x, endos[idx + 1].x); // (x * beta, -y)
    fe_clone(endos[idx + 2].y, endos[idx + 0].y);

    fe_modp_mul(endos[idx + 3].x, points[k].x, B2); // (x * beta^2, y)
    fe_clone(endos[idx + 3].y, points[k].y);

    fe_clone(endos[idx + 4].x, endos[idx + 3].x); // (x * beta^2, -y)
    fe_clone(endos[idx + 4].y, endos[idx + 0].y);

    bool is_full = (idx + 5) % esize == 0 || k == GROUP_INV_SIZE - 1;
    if (!is_full) continue;

    for (size_t i = 0; i < esize; i += HASH_BATCH_SIZE) {
      if (ctx->check_addr33) addr33_batch(hs33, endos + i, HASH_BATCH_SIZE);
      if (ctx->check_addr65) addr65_batch(hs65, endos + i, HASH_BATCH_SIZE);

      for (size_t j = 0; j < HASH_BATCH_SIZE; ++j) {
        // if (ci >= (GROUP_INV_SIZE * 5)) break;
        // printf(">> %6zu | %6zu ~ %zu\n", ci, ci / 5, (ci % 5) + 1);
        if (ctx->check_addr33) check_hash(ctx, true, hs33[j], start_pk, ci / 5, (ci % 5) + 1);
        if (ctx->check_addr65) check_hash(ctx, false, hs65[j], start_pk, ci / 5, (ci % 5) + 1);
        ci += 1;
      }
    }
  }

  assert(ci == GROUP_INV_SIZE * 5);
}

void batch_add(ctx_t *ctx, const fe pk, const size_t iterations) {
  size_t hsize = GROUP_INV_SIZE / 2;

  pe bp[GROUP_INV_SIZE]; // calculated ec points
  fe dx[hsize];          // delta x for group inversion
  pe GStart;             // iteration points
  fe ck, rx, ry;         // current start point; tmp for x3, y3
  fe ss, dd;             // temp variables

  // set start point to center of the group
  fe_modn_add_stride(ss, pk, ctx->stride_k, hsize);
  ec_jacobi_mulrdc(&GStart, &G1, ss); // G * (pk + hsize * gs)

  // group addition with single inversion (with stride support)
  // structure: K-N/2 .. K-2 K-1 [K] K+1 K+2 .. K+N/2-1 (last K dropped to have odd size)
  // points in `bp` already order by `pk` increment
  fe_clone(ck, pk); // start pk for current iteration

  size_t counter = 0;
  while (counter < iterations) {
    for (size_t i = 0; i < hsize; ++i) fe_modp_sub(dx[i], ctx->gpoints[i].x, GStart.x);
    fe_modp_grpinv(dx, hsize);

    pe_clone(&bp[hsize + 0], &GStart); // set K value

    for (size_t D = 0; D < 2; ++D) {
      bool positive = D == 0;
      size_t g_idx = positive ? 0 : hsize; // plus points in first half, minus in second half
      size_t g_max = positive ? hsize - 1 : hsize; // skip K+N/2, since we don't need it
      for (size_t i = 0; i < g_max; ++i) {
        fe_modp_sub(ss, ctx->gpoints[g_idx + i].y, GStart.y); // y2 - y1
        fe_modp_mul(ss, ss, dx[i]);                           // λ = (y2 - y1) / (x2 - x1)
        fe_modp_sqr(rx, ss);                                  // λ²
        fe_modp_sub(rx, rx, GStart.x);                        // λ² - x1
        fe_modp_sub(rx, rx, ctx->gpoints[g_idx + i].x);       // rx = λ² - x1 - x2
        fe_modp_sub(dd, GStart.x, rx);                        // x1 - rx
        fe_modp_mul(dd, ss, dd);                              // λ * (x1 - rx)
        fe_modp_sub(ry, dd, GStart.y);                        // ry = λ * (x1 - rx) - y1

        // ordered by pk:
        // [0]: K-N/2, [1]: K-N/2+1, .., [N/2-1]: K-1 // all minus points
        // [N/2]: K, [N/2+1]: K+1, .., [N-1]: K+N/2-1 // K, plus points without last element
        size_t idx = positive ? hsize + i + 1 : hsize - 1 - i;
        fe_clone(bp[idx].x, rx);
        fe_clone(bp[idx].y, ry);
        fe_set64(bp[idx].z, 0x1);
      }
    }

    check_found_add(ctx, ck, bp);
    fe_modn_add_stride(ck, ck, ctx->stride_k, GROUP_INV_SIZE); // move pk to next group START
    ec_jacobi_addrdc(&GStart, &GStart, &ctx->stride_p);        // move GStart to next group CENTER
    counter += GROUP_INV_SIZE;
  }
}

void *cmd_add_worker(void *arg) {
  ctx_t *ctx = (ctx_t *)arg;

  fe initial_r; // keep initial range start to check overflow
  fe_clone(initial_r, ctx->range_s);

  // job_size multiply by 2^offset (iterate over desired digit order)
  // for example: 3013 3023 .. 30X3 .. 3093 3103 3113
  fe inc = {0};
  fe_set64(inc, ctx->job_size);
  fe_modn_mul(inc, inc, ctx->stride_k);

  fe pk;
  while (true) {
    pthread_mutex_lock(&ctx->lock);
    bool is_overflow = fe_cmp(ctx->range_s, initial_r) < 0;
    if (fe_cmp(ctx->range_s, ctx->range_e) >= 0 || is_overflow) {
      pthread_mutex_unlock(&ctx->lock);
      break;
    }

    fe_clone(pk, ctx->range_s);
    fe_modn_add(ctx->range_s, ctx->range_s, inc);
    pthread_mutex_unlock(&ctx->lock);

    batch_add(ctx, pk, ctx->job_size);
    ctx_update(ctx, ctx->use_endo ? ctx->job_size * 6 : ctx->job_size);
  }

  return NULL;
}

void cmd_add(ctx_t *ctx) {
  ctx_precompute_gpoints(ctx);

  fe range_size;
  fe_modn_sub(range_size, ctx->range_e, ctx->range_s);
  ctx->job_size = fe_cmp64(range_size, MAX_JOB_SIZE) < 0 ? range_size[0] : MAX_JOB_SIZE;
  ctx->ts_started = tsnow(); // actual start time

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_create(&ctx->threads[i], NULL, cmd_add_worker, ctx);
  }

  for (size_t i = 0; i < ctx->threads_count; ++i) {
    pthread_join(ctx->threads[i], NULL);
  }

  ctx_finish(ctx);
}

// MARK: CMD_MUL

void check_found_mul(ctx_t *ctx, const fe *pk, const pe *cp, size_t cnt) {
  h160_t hs33[HASH_BATCH_SIZE];
  h160_t hs65[HASH_BATCH_SIZE];

  for (size_t i = 0; i < cnt; i += HASH_BATCH_SIZE) {
    size_t batch_size = MIN(HASH_BATCH_SIZE, cnt - i);
    if (ctx->check_addr33) addr33_batch(hs33, cp + i, batch_size);
    if (ctx->check_addr65) addr65_batch(hs65, cp + i, batch_size);

    for (size_t j = 0; j < HASH_BATCH_SIZE; ++j) {
      if (ctx->check_addr33 && ctx_check_hash(ctx, hs33[j])) {
        // pk_verify_hash(pk[i + j], hs33[j], true, 0);
        ctx_write_found(ctx, "addr33", hs33[j], pk[i + j]);
      }

      if (ctx->check_addr65 && ctx_check_hash(ctx, hs65[j])) {
        // pk_verify_hash(pk[i + j], hs65[j], false, 0);
        ctx_write_found(ctx, "addr65", hs65[j], pk[i + j]);
      }
    }
  }
}

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
      for (size_t i = 0; i < job->count; ++i) fe_modn_from_hex(pk[i], job->lines[i]);
    } else {
      for (size_t i = 0; i < job->count; ++i) {
        size_t len = strlen(job->lines[i]);
        size_t msg_size = (len + 63 + 9) / 64 * 64;

        // calculate sha256 hash
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

    check_found_mul(ctx, pk, cp, job->count);
    ctx_update(ctx, job->count);
  }

  if (job != NULL) free(job);
  return NULL;
}

void cmd_mul(ctx_t *ctx) {
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

  ctx_finish(ctx);
}

// MARK: CMD_RND

void gen_random_range(ctx_t *ctx, const fe a, const fe b) {
  fe_rand_range(ctx->range_s, a, b, !ctx->has_seed);
  fe_clone(ctx->range_e, ctx->range_s);
  for (u32 i = ctx->ord_offs; i < (ctx->ord_offs + ctx->ord_size); ++i) {
    ctx->range_s[i / 64] &= ~(1ULL << (i % 64));
    ctx->range_e[i / 64] |= 1ULL << (i % 64);
  }

  // put in bounds
  if (fe_cmp(ctx->range_s, a) <= 0) fe_clone(ctx->range_s, a);
  if (fe_cmp(ctx->range_e, b) >= 0) fe_clone(ctx->range_e, b);
}

void print_range_mask(fe range_s, u32 bits_size, u32 offset, bool use_color) {
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
      if (use_color) fputs(COLOR_YELLOW, stdout);
      putchar(cc);
      if (use_color) fputs(COLOR_RESET, stdout);
    } else {
      putchar(cc);
    }
  }

  putchar('\n');
}

void cmd_rnd(ctx_t *ctx) {
  ctx->ord_offs = MIN(ctx->ord_offs, 255 - ctx->ord_size);
  printf("[RANDOM MODE] offs: %d ~ bits: %d\n\n", ctx->ord_offs, ctx->ord_size);

  ctx_precompute_gpoints(ctx);
  ctx->job_size = MAX_JOB_SIZE;
  ctx->ts_started = tsnow(); // actual start time

  fe range_s, range_e;
  fe_clone(range_s, ctx->range_s);
  fe_clone(range_e, ctx->range_e);

  size_t last_c = 0, last_f = 0, s_time = 0;
  while (true) {
    last_c = ctx->k_checked;
    last_f = ctx->k_found;
    s_time = tsnow();

    gen_random_range(ctx, range_s, range_e);
    print_range_mask(ctx->range_s, ctx->ord_size, ctx->ord_offs, ctx->use_color);
    print_range_mask(ctx->range_e, ctx->ord_size, ctx->ord_offs, ctx->use_color);
    ctx_print_status(ctx);

    // if full range is used, skip break after first iteration
    bool is_full = fe_cmp(ctx->range_s, range_s) == 0 && fe_cmp(ctx->range_e, range_e) == 0;

    for (size_t i = 0; i < ctx->threads_count; ++i) {
      pthread_create(&ctx->threads[i], NULL, cmd_add_worker, ctx);
    }

    for (size_t i = 0; i < ctx->threads_count; ++i) {
      pthread_join(ctx->threads[i], NULL);
    }

    size_t dc = ctx->k_checked - last_c, df = ctx->k_found - last_f;
    double dt = MAX((tsnow() - s_time), 1ul) / 1000.0;
    term_clear_line();
    printf("%'zu / %'zu ~ %.1fs\n\n", df, dc, dt);

    if (is_full) break;
  }

  ctx_finish(ctx);
}

// MARK: args helpers

void arg_search_range(args_t *args, fe range_s, fe range_e) {
  char *raw = arg_str(args, "-r");
  if (!raw) {
    fe_set64(range_s, GROUP_INV_SIZE);
    fe_clone(range_e, FE_P);
    return;
  }

  char *sep = strchr(raw, ':');
  if (!sep) {
    fprintf(stderr, "invalid search range, use format: -r 8000:ffff\n");
    exit(1);
  }

  *sep = 0;
  fe_modn_from_hex(range_s, raw);
  fe_modn_from_hex(range_e, sep + 1);

  // if (fe_cmp64(range_s, GROUP_INV_SIZE) <= 0) fe_set64(range_s, GROUP_INV_SIZE + 1);
  // if (fe_cmp(range_e, FE_P) > 0) fe_clone(range_e, FE_P);

  if (fe_cmp64(range_s, GROUP_INV_SIZE) <= 0) {
    fprintf(stderr, "invalid search range, start <= %#lx\n", GROUP_INV_SIZE);
    exit(1);
  }

  if (fe_cmp(range_e, FE_P) > 0) {
    fprintf(stderr, "invalid search range, end > FE_P\n");
    exit(1);
  }

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
  u32 max_offs = MAX(1ul, MAX(MIN_SIZE, range_bits) - default_bits);

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

  ctx->ord_offs = MIN(max_offs, tmp_offs);
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
  printf("  blf-check       - check bloom filter for given hex-encoded hash160\n");
  printf("  bench           - run benchmark of internal functions\n");
  printf("  bench-gtable    - run benchmark of ecc multiplication (with different table size)\n");
  printf("\n");
}

void init(ctx_t *ctx, args_t *args) {
  // check other commands first
  if (args->argc > 1) {
    if (strcmp(args->argv[1], "blf-gen") == 0) return blf_gen(args);
    if (strcmp(args->argv[1], "blf-check") == 0) return blf_check(args);
    if (strcmp(args->argv[1], "bench") == 0) return run_bench();
    if (strcmp(args->argv[1], "bench-gtable") == 0) return run_bench_gtable();
    if (strcmp(args->argv[1], "mult-verify") == 0) return mult_verify();
  }

  ctx->use_color = isatty(fileno(stdout));

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
    free(seed);
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

  ctx->use_endo = args_bool(args, "-endo");
  if (ctx->cmd == CMD_MUL) ctx->use_endo = false; // no endo for mul command

  pthread_mutex_init(&ctx->lock, NULL);
  int cpus = get_cpu_count();
  ctx->threads_count = MIN(MAX(args_uint(args, "-t", cpus), 1ul), 320ul);
  ctx->threads = malloc(ctx->threads_count * sizeof(pthread_t));
  ctx->finished = false;
  ctx->k_checked = 0;
  ctx->k_found = 0;
  ctx->ts_started = tsnow();
  ctx->ts_updated = ctx->ts_started;
  ctx->ts_printed = ctx->ts_started - 5e3;
  ctx->paused_time = 0;
  ctx->paused = false;

  arg_search_range(args, ctx->range_s, ctx->range_e);
  load_offs_size(ctx, args);
  queue_init(&ctx->queue, ctx->threads_count * 3);

  printf("threads: %zu ~ addr33: %d ~ addr65: %d ~ endo: %d | filter: ", //
         ctx->threads_count, ctx->check_addr33, ctx->check_addr65, ctx->use_endo);

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

void *kb_listener(void *arg) {
  ctx_t *ctx = (ctx_t *)arg;

  int tty_fd = open("/dev/tty", O_RDONLY);
  if (tty_fd < 0) {
    perror("open /dev/tty");
    return NULL;
  }

  struct termios t;
  if (tcgetattr(tty_fd, &t) == 0) {
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(tty_fd, TCSANOW, &t);
  }

  size_t ts_paused = 0;
  fd_set fds;
  char ch;

  while (true) {
    FD_ZERO(&fds);
    FD_SET(tty_fd, &fds);

    int ret = select(tty_fd + 1, &fds, NULL, NULL, NULL);
    if (ret < 0) {
      perror("select");
      break;
    }

    if (FD_ISSET(tty_fd, &fds)) {
      if (read(tty_fd, &ch, 1) > 0) {
        if (ch == 'p' && !ctx->paused) {
          ts_paused = tsnow();
          ctx->paused = true;
          ctx_print_status(ctx);
        }

        if (ch == 'r' && ctx->paused) {
          ctx->paused_time += tsnow() - ts_paused;
          ctx->paused = false;
          ctx_print_status(ctx);
        }
      }
    }
  }

  return NULL;
}

struct termios _original_termios;
int _tty_fd = -1;

void cleanup() {
  // restore terminal settings on exit
  if (_tty_fd >= 0) {
    tcsetattr(_tty_fd, TCSANOW, &_original_termios);
    close(_tty_fd);
    _tty_fd = -1;
  }
}

void handle_sigint(int sig) {
  fflush(stderr);
  fflush(stdout);
  printf("\n");
  cleanup();
  exit(sig);
}

int main(int argc, const char **argv) {
  // https://stackoverflow.com/a/11695246
  setlocale(LC_NUMERIC, ""); // for comma separated numbers
  args_t args = {argc, argv};

  ctx_t ctx = {0};
  init(&ctx, &args);

  // save original terminal settings
  atexit(cleanup);
  int _tty_fd = open("/dev/tty", O_RDONLY);
  if (_tty_fd >= 0) {
    tcgetattr(_tty_fd, &_original_termios);
  }

  pthread_t kb_thread;
  pthread_create(&kb_thread, NULL, kb_listener, &ctx);
  signal(SIGINT, handle_sigint); // Keep last progress line on Ctrl-C

  if (ctx.cmd == CMD_ADD) cmd_add(&ctx);
  if (ctx.cmd == CMD_MUL) cmd_mul(&ctx);
  if (ctx.cmd == CMD_RND) cmd_rnd(&ctx);

  cleanup();
  return 0;
}
