#pragma once

#include "base.c"

void handleInsertKey(uint64_t c) {
  static uint_fast8_t quit_times = QUIT_TIMES;

  switch (c) {
  case ENTER:
    editorInsertNewline();
    break;

  case CTRL_KEY('c'):
    if (E.dirty && quit_times > 0) {
      setStatusMessage("¡WARNING! File has unsaved changes. Press Ctrl-C %d "
                       "more times to quit.",
                       quit_times);
      quit_times--;
      return;
    }
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7); // Clear screen.
    exit(0);
    break;

  case CTRL_KEY('s'):
    editorSave();
    break;

  case BACKSPACE:
  case CTRL_KEY('h'):
    editorDelChar();
    break;

  // Cursor movement
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    moveCursor(c);
    break;

  case ESC:
    E.mode = NORMAL;
    break;

  default:
    editorInsertChar(c);
    break;
  }
}
