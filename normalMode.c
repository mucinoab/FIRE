#pragma once

#include "base.c"

void handleDoubleNormalKey(uint64_t c, uint64_t last_command) {
  switch (c) {
  case 'g': // gg: go to top of the file
    if (last_command == 'g')
      E.cy = 0;
    break;

  case 'd':
    if (last_command == 'd')
      // TODO delete row
      break;

  default:
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

  case 'b':
  case 'w':
    // TODO
    break;

  case 'o': { // Insert new line below the line of the cursor.
    E.cx = E.rows[E.cy + E.row_offset].render.len;
    editorInsertNewline();
    E.mode = INSERT;
  } break;

  default:
    handleDoubleNormalKey(c, last_command);
    last_command = c;
    // No O
    break;
  }
}
