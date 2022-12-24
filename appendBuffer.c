#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*** append buffer ***/
/// A growable buffer

typedef struct appendBuffer {
  char *buf;
  size_t cap;
  size_t len;
} appendBuffer;

struct appendBuffer new_appendBuffer() {
  const size_t cap = 256;
  appendBuffer ap = {calloc(sizeof(char), cap), cap, 0};

  return ap;
}

void abAppend(appendBuffer *ab, const char *s) {
  size_t len = strlen(s);

  if (len == 0) {
    return;
  }

  size_t new_len = ab->len + len;

  if (ab->cap <= new_len) {
    // Allocate more memory as needed but in powers of two.
    ab->cap = 1 << (size_t)ceil(log2(new_len + 1));
    ab->buf = realloc(ab->buf, ab->cap);
  }

  memcpy(&ab->buf[ab->len], s, len);
  ab->len = new_len;

  ab->buf[ab->len] = '\0'; // Null terminated
}

void abFree(appendBuffer *ab) { free(ab->buf); }
