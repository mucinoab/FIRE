#pragma once

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*** append buffer ***/
/// A growable buffer.
typedef struct appendBuffer {
  char *buf;
  size_t cap;
  size_t len;
} appendBuffer;

/// Creates a new buffer with some memory already allocated.
appendBuffer newAppendBuffer() {
  size_t cap = 256;
  appendBuffer ap = {.buf = calloc(cap, sizeof(char)), .cap = cap, .len = 0};

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
  size_t extra_needed = strlen(s);

  if (extra_needed == 0) {
    return;
  }

  size_t new_len = ab->len + extra_needed;
  if (ab->cap <= new_len) {
    abResize(ab, new_len);
  }

  memcpy(&ab->buf[ab->len], s, extra_needed);
  ab->len = new_len;

  ab->buf[ab->len] = '\0'; // Null terminated
}

/// Inserts the provided char at the end of the buffer.
void abAppendChar(appendBuffer *ab, char c) {
  size_t new_len = ab->len + 1;

  if (ab->cap <= new_len) {
    abResize(ab, new_len);
  }

  ab->buf[ab->len] = c;
  ab->len++;
  ab->buf[ab->len] = '\0'; // Null terminated
}

/// Removes and returns the char at the end of the buffer.
char abPop(appendBuffer *ab) {
  char out = '\0';

  if (ab->len != 0) {
    ab->len -= 1;
    out = ab->buf[ab->len];
    ab->buf[ab->len] = '\0';
  }

  return out;
}

/// Copies all the contents of the `src` buffer into the `dst` buffer.
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

/// Frees the resources used by the buffer.
void abFree(appendBuffer *ab) { free(ab->buf); }
