#pragma once

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef __uint128_t u128;
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned char u8;

typedef char hex40[41]; // rmd160 hex string
typedef char hex64[65]; // sha256 hex string
typedef u32 h160_t[5];

// MARK: helpers

u64 tsnow() {
  struct timespec ts;
  // clock_gettime(CLOCK_MONOTONIC, &ts);
  clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1e6;
}

bool strendswith(const char *str, const char *suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  return (str_len >= suffix_len) && (strcmp(str + str_len - suffix_len, suffix) == 0);
}

// MARK: args

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

// MARK: queue

typedef struct queue_item_t {
  void *data_ptr;
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
} queue_t;

void queue_init(queue_t *q, size_t capacity) {
  q->capacity = capacity;
  q->size = 0;
  q->done = false;
  q->head = NULL;
  q->tail = NULL;
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

void queue_put(queue_t *q, void *data_ptr) {
  pthread_mutex_lock(&q->lock);
  if (q->done) {
    pthread_mutex_unlock(&q->lock);
    return;
  }

  while (q->size == q->capacity) {
    pthread_cond_wait(&q->cond_put, &q->lock);
  }

  queue_item_t *item = malloc(sizeof(queue_item_t));
  item->data_ptr = data_ptr;
  item->next = NULL;

  if (q->tail != NULL) q->tail->next = item;
  else q->head = item;

  q->tail = item;
  q->size += 1;

  pthread_cond_signal(&q->cond_get);
  pthread_mutex_unlock(&q->lock);
}

void *queue_get(queue_t *q) {
  pthread_mutex_lock(&q->lock);
  while (q->size == 0 && !q->done) {
    pthread_cond_wait(&q->cond_get, &q->lock);
  }

  if (q->size == 0) {
    pthread_mutex_unlock(&q->lock);
    return NULL;
  }

  queue_item_t *item = q->head;
  q->head = item->next;
  if (!q->head) q->tail = NULL;

  void *data_ptr = item->data_ptr;
  free(item);
  q->size -= 1;

  pthread_cond_signal(&q->cond_put);
  pthread_mutex_unlock(&q->lock);
  return data_ptr;
}

// MARK: bloom filter

#define BLF_MAGIC 0x45434246 // FourCC: ECBF
#define BLF_VERSION 1

typedef struct blf_t {
  size_t size;
  u64 *bits;
} blf_t;

static inline void blf_setbit(blf_t *blf, size_t idx) {
  blf->bits[idx % (blf->size * 64) / 64] |= (u64)1 << (idx % 64);
}

static inline bool blf_getbit(blf_t *blf, u64 idx) {
  return (blf->bits[idx % (blf->size * 64) / 64] & ((u64)1 << (idx % 64))) != 0;
}

void blf_add(blf_t *blf, const h160_t hash) {
  u64 a1 = (u64)hash[0] << 32 | hash[1];
  u64 a2 = (u64)hash[2] << 32 | hash[3];
  u64 a3 = (u64)hash[4] << 32 | hash[0];
  u64 a4 = (u64)hash[1] << 32 | hash[2];
  u64 a5 = (u64)hash[3] << 32 | hash[4];

  u8 shifts[4] = {24, 28, 36, 40};
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

  u8 shifts[4] = {24, 28, 36, 40};
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

bool blf_save(const char *filepath, blf_t *blf) {
  FILE *file = fopen(filepath, "wb");
  if (file == NULL) {
    fprintf(stderr, "failed to open output file\n");
    exit(1);
  }

  u32 blf_magic = BLF_MAGIC;
  u32 blg_version = BLF_VERSION;

  if (fwrite(&blf_magic, sizeof(blf_magic), 1, file) != 1) {
    fprintf(stderr, "failed to write bloom filter magic\n");
    return false;
  };

  if (fwrite(&blg_version, sizeof(blg_version), 1, file) != 1) {
    fprintf(stderr, "failed to write bloom filter version\n");
    return false;
  }

  if (fwrite(&blf->size, sizeof(blf->size), 1, file) != 1) {
    fprintf(stderr, "failed to write bloom filter size\n");
    return false;
  }

  if (fwrite(blf->bits, sizeof(u64), blf->size, file) != blf->size) {
    fprintf(stderr, "failed to write bloom filter bits\n");
    return false;
  }

  fclose(file);
  return true;
}

bool blf_load(const char *filepath, blf_t *blf) {
  FILE *file = fopen(filepath, "rb");
  if (file == NULL) {
    fprintf(stderr, "failed to open input file\n");
    return false;
  }

  u32 blf_magic, blf_version;
  size_t size;

  bool is_ok = true;
  is_ok = is_ok && fread(&blf_magic, sizeof(blf_magic), 1, file) == 1;
  is_ok = is_ok && fread(&blf_version, sizeof(blf_version), 1, file) == 1;
  is_ok = is_ok && fread(&size, sizeof(size), 1, file) == 1;
  if (!is_ok) {
    fprintf(stderr, "failed to read bloom filter header\n");
    return false;
  }

  if (blf_magic != BLF_MAGIC || blf_version != BLF_VERSION) {
    fprintf(stderr, "invalid bloom filter version; create a new filter with blf-gen command\n");
    return false;
  }

  u64 *bits = calloc(size, sizeof(u64));
  if (fread(bits, sizeof(u64), size, file) != size) {
    fprintf(stderr, "failed to read bloom filter bits\n");
    return false;
  }

  fclose(file);
  blf->size = size;
  blf->bits = bits;
  return true;
}

void __blf_gen_usage(args_t *args) {
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
    return __blf_gen_usage(args);
  }

  char *filepath = arg_str(args, "-o");
  if (filepath == NULL) {
    fprintf(stderr, "[!] missing output file (-o <file>)\n");
    return __blf_gen_usage(args);
  }

  blf_t blf = {.size = 0, .bits = NULL};
  if (access(filepath, F_OK) == 0) blf_load(filepath, &blf);

  // https://hur.st/bloomfilter/?n=500M&p=1000000&m=&k=20
  u64 r = 1e9;
  double p = 1.0 / (double)r;
  u64 m = (u64)(n * log(p) / log(1.0 / pow(2.0, log(2.0))));
  double mb = (double)m / 8 / 1024 / 1024;
  printf("creating bloom filter: n = %'llu | p = 1:%'llu | m = %'llu (%'.1f MB)\n", n, r, m, mb);

  blf.size = (m + 63) / 64;
  blf.bits = calloc(blf.size, sizeof(u64));

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

  printf("added %'llu items; saving to %s\n", count, filepath);

  if (!blf_save(filepath, &blf)) {
    fprintf(stderr, "failed to save bloom filter\n");
    exit(1);
  }

  free(blf.bits);
}
