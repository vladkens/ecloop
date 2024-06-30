#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

size_t tsnow() {
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

char *unhex(const char *hex) {
  size_t len = strlen(hex);
  char *buf = malloc(len / 2 + 1);
  for (size_t i = 0; i < len; i += 2) {
    sscanf(hex + i, "%2hhx", buf + i / 2);
  }
  buf[len / 2] = '\0';
  return buf;
}
