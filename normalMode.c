#pragma once

#include "base.c"
#include <string.h>

void handleDoubleNormalKey(uint64_t c, uint64_t last_command) {
  switch (c) {
  case 'g': // gg: go to top of the file
    if (last_command == 'g')
      E.cy = 0;
    break;

  case 'd':
    if (last_command == 'd') {
      if (E.cx >= E.rows[E.cy + 1].render.len)
        E.cx = E.rows[E.cy + 1].chars.len;
      abClear(&E.rows[E.cy].chars);
      editorDelRow(E.cy);
    }
    break;

  default:
    if (last_command == 'r' && c != ESC) {
      // Replace one char with just typed char.
      E.rows[E.cy].chars.buf[E.cx] = c;
      updateRow(&E.rows[E.cy]);
    }
    break;
  }
}

void handleNormalKey(uint64_t c) {
  static uint64_t last_command = '\0';
  static uint_fast8_t quit_times = QUIT_TIMES;

  switch (c) {
  case ENTER:
    E.cy += 1; // Move to the line below.
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
    E.cx -= 1;
    break;

  case 'H': // Move to beginning of line.
    E.cx = 0;
    break;
  case 'L': // Move to end of line.
    E.cx = E.rows[E.cy].render.len;
    break;
  case 'G': // Move to the end of the file.
    E.cy = E.num_rows - 1;
    break;
  case 'O': { // Insert new line above the line of the cursor.
    E.cx = 0;
    editorInsertNewline();
    moveCursor(ARROW_UP);
    E.mode = INSERT;
  } break;

  // Cursor movements
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    moveCursor(c);
    break;
  case 'j':
    moveCursor(ARROW_DOWN);
    break;
  case 'k':
    moveCursor(ARROW_UP);
    break;
  case 'h':
    moveCursor(ARROW_LEFT);
    break;
  case 'l':
    moveCursor(ARROW_RIGHT);
    break;

  case '/':
    editorFind();
    break;

  case 'i':
    E.mode = INSERT;
    break;

  case 'x': // Delete the char under the cursor.
    rowDelChar(&E.rows[E.cy], E.cx);
    break;

  case 'b':
    // TODO Implement word traversal backwards.
    break;
  case 'w': {
    // TODO Implement word traversal right.
    char *line_from_cursor = &E.rows[E.cy].chars.buf[E.cx];
    char *first_space = strchr(line_from_cursor, ' ');
    // You should move X to the first non-white-space char.
    if (first_space) {
      E.cx += (first_space - line_from_cursor) + 1;
    } else {
      E.cx = 0;
      moveCursor(ARROW_DOWN);
    }
  } break;

  case 'u':
    // TODO implement undo.
    break;

  case 'o': { // Insert new line below the line of the cursor.
    E.cx = E.rows[E.cy + E.row_offset].render.len;
    editorInsertNewline();
    E.mode = INSERT;
  } break;

  default:
    handleDoubleNormalKey(c, last_command);
    last_command = c;
    break;
  }
}
