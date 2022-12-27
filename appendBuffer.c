#pragma once

#include <math.h>
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

/// Inserts the given string to the end of the buffer.
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

/// Clears the contents of the appendBuffer while keeping the allocated buffer
/// intact and ready to reused.
void abClear(appendBuffer *ab) {
  ab->len = 0;

  if (ab->cap >= 1)
    ab->buf[0] = '\0'; // Null terminated
}

/// Inserts `c` at the requested position in the buffer.
/// Beware that this is a O(N) operations, as all the elements from `at` to the
/// end need to be shifted.
void abInsertAt(appendBuffer *r, size_t at, size_t c) {
  // TODO use something more insert-efficient. (Skip-list,Tiered Vector)
  if (at > r->len)
    at = r->len;

  if (r->cap < (r->len + 2))
    abResize(r, (r->len + 2));

  memmove(&r->buf[at + 1], &r->buf[at], r->len - at + 1);
  r->len++;
  r->buf[at] = c;
  // TODO does this move the null terminated char? I think so.
}

/// Removes the element that is at the requested position in the buffer.
/// Beware that this is a O(N) operations, as all the elements from `at` to the
/// end need to be shifted.
void abRemoveAt(appendBuffer *ab, size_t at) {
  if (at >= ab->len)
    return;

  memmove(&ab->buf[at], &ab->buf[at + 1], ab->len - at);
  ab->len--;
  // TODO does this move the null terminated char? I think so.
}

void abFree(appendBuffer *ab) { free(ab->buf); }
