#define _GNU_SOURCE

#pragma once

#include "appendBuffer.c"
#include <stdint.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
/// It sets the upper 3 bits of the character to 0, like the Ctrl key.
#define CTRL_KEY(k) ((k)&0x1f)
#define TAB_STOP 4
#define STATUS_MSG_TIMEOUT 5
#define QUIT_TIMES 2

typedef enum editorKey {
  BACKSPACE = 127,
  ESC = '\x1b',
  ENTER = '\r',
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
} editorKey;

typedef enum editorHighlight {
  HL_NORMAL = 0,
  HL_NUMBER,
  HL_MATCH
} editorHighlight;

typedef enum Mode { NORMAL, INSERT } Mode;

/*** data ***/
typedef struct row {
  appendBuffer chars;
  appendBuffer render;
  uint8_t *hl; // Highlight information. TODO use a bitset.
} row;

row new_row() {
  row r = {0};
  r.chars = newAppendBuffer();
  r.render = newAppendBuffer();

  return r;
}

/// Holds all the state of the editor.
struct editorConfig {
  struct termios orig_termios;

  // Current screen buffer.
  appendBuffer screen;

  // Size of the terminal
  uint_fast32_t screen_cols;
  uint_fast32_t screen_rows;

  // Left margin width
  uint_fast32_t left_margin;

  // Cursor Position
  uint_fast32_t cx;
  uint_fast32_t cy;

  // Cursor Render Position
  uint_fast32_t rx;

  // File contents, line by line
  uint_fast32_t num_rows;
  row *rows;

  // Current view posiiton
  int_fast32_t row_offset;
  int_fast32_t col_offset;

  // Status bar stuff
  char *filename;
  appendBuffer status_msg;
  time_t status_msg_time;

  // State Flags
  uint_fast8_t dirty;
  Mode mode;
};

struct editorConfig E = {0};

uint_fast32_t getCy() { return (E.cy - E.row_offset); }
uint_fast32_t getCx() { return (E.rx - E.col_offset); }

/*** prototypes ***/
char *editorPrompt(char *prompt, void (*callback)(char *, size_t));
void editorDelChar();
void editorFind();
void editorInsertChar(size_t c);
void editorInsertNewline();
void editorRefreshScreen();
void editorSave();
void moveCursor(uint64_t key);
void rowDelChar(row *row, size_t at);
void rowInsertChar(row *row, size_t at, size_t c);
void setStatusMessage(const char *fmt, ...);
void updateRow(row *r);
void editorRowAppendString(row *src, row *dst);
void editorDelRow(size_t at);
