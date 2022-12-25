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

appendBuffer new_appendBuffer() {
  const size_t cap = 256;
  appendBuffer ap = {calloc(sizeof(char), cap), cap, 0};

  return ap;
}

/// Resize the buffer into the next power of 2 of `cap`.
void abResize(appendBuffer *ab, size_t cap) {
  size_t new_cap = 1 << (size_t)ceil(log2(cap + 1));

  if (ab->cap >= new_cap) {
    return;
  }

  ab->buf = realloc(ab->buf, new_cap);
  ab->cap = new_cap;
}

void abAppend(appendBuffer *ab, const char *s) {
  size_t len = strlen(s);

  if (len == 0) {
    return;
  }

  size_t new_len = ab->len + len;
  if (ab->cap <= new_len) {
    abResize(ab, new_len);
  }

  memcpy(&ab->buf[ab->len], s, len);
  ab->len = new_len;

  ab->buf[ab->len] = '\0'; // Null terminated
}

void abCopyInto(appendBuffer *src, appendBuffer *dst) {
  if (dst->cap < src->cap) {
    abResize(dst, src->cap + 1);
  }

  memcpy(dst->buf, src->buf, src->len);
  dst->len = src->len;
  dst->buf[dst->len] = '\0'; // Null terminated
}

void abFree(appendBuffer *ab) { free(ab->buf); }
