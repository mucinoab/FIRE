#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include "appendBuffer.c"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
/// It sets the upper 3 bits of the character to 0, like the Ctrl key.
#define CTRL_KEY(k) ((k)&0x1f)
#define TAB_STOP 4

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/

typedef struct row {
  appendBuffer chars;
  appendBuffer render;
} row;

row new_row() {
  row r = {0};
  r.chars = new_appendBuffer();
  r.render = new_appendBuffer();

  return r;
}

struct editorConfig {
  struct termios orig_termios;

  // Size of the terminal
  int_fast32_t screen_cols;
  int_fast32_t screen_rows;

  // Cursor Position
  int_fast32_t cx;
  int_fast32_t cy;

  // File contents, line by line
  int_fast32_t num_rows;
  row *rows;

  // Current view posiiton
  int_fast32_t row_offset;
  int_fast32_t col_offset;
};

struct editorConfig E = {0};

/*** terminal ***/
void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");

  tcgetattr(STDIN_FILENO, &E.orig_termios);
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // Control Characters
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int64_t readKey() {
  char c = '\0';
  int32_t nread = 0;

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  // Handle multibyte sequeces
  if (c == '\x1b') {
    char seq[3] = {0};
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return (int64_t)c;

    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return (int64_t)c;

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return (int64_t)'\x1b';

        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        // Arrow keys
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
  }

  switch (c) {
  case 'j':
    return ARROW_DOWN;
  case 'k':
    return ARROW_UP;
  case 'h':
    return ARROW_LEFT;
  case 'l':
    return ARROW_RIGHT;
  }

  return (int64_t)c;
}

void getCursorPosition() {
  char buf[32] = {0};
  uint_fast16_t i = 0;

  write(STDOUT_FILENO, "\x1b[6n", 4);

  while (i < (sizeof(buf) - 1)) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;

    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    die("cursor");

  if (sscanf(&buf[2], "%li;%li", &E.cx, &E.cy) != 2)
    die("cursor");
}

void getWindowSize() {
  struct winsize ws = {0};

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // Put cursor at the end of the screen and read the positon.
    write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12);
    getCursorPosition();

    E.screen_cols = E.cx;
    E.screen_rows = E.cy;
  } else {
    E.screen_cols = ws.ws_col;
    E.screen_rows = ws.ws_row;
  }
}

/*** row operations ***/

/// Copies Chars into Renders and replaces tabs for spaces
void editorUpdateRow(row *r) {
  size_t tabs = 0;

  for (size_t j = 0; j < r->chars.len; j++)
    if (r->chars.buf[j] == '\t')
      tabs++;

  // More memory for the spaces.
  abResize(&r->render, r->chars.len + tabs * (TAB_STOP - 1) + 1);

  // Replace tabs for 8 spaces.
  size_t idx = 0;

  for (size_t j = 0; j < r->chars.len; j++) {
    if (r->chars.buf[j] == '\t') {
      for (int_fast8_t i = 0; i < TAB_STOP; i++)
        r->render.buf[idx++] = ' ';
    } else {
      r->render.buf[idx++] = r->chars.buf[j];
    }
  }

  r->render.buf[idx] = '\0';
  r->render.len = idx;
}

void appendRow(char *s) {
  // TODO efficient resize
  E.rows = realloc(E.rows, sizeof(*E.rows) * (E.num_rows + 1));
  E.rows[E.num_rows] = new_row();

  abAppend(&E.rows[E.num_rows].chars, s);
  editorUpdateRow(&E.rows[E.num_rows]);

  E.num_rows++;
}

/*** file i/o ***/
void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");

  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen = -1;
  size_t n = 0;

  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    n += 1;

    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;

    line[linelen] = '\0';
    appendRow(line);
  }

  free(line);
  fclose(fp);
}

/*** input ***/
void moveCursor(uint64_t key) {
  appendBuffer *row = (E.cy >= E.num_rows) ? NULL : &E.rows[E.cy].chars;

  switch (key) {
  case ARROW_DOWN:
    if (E.cy < E.num_rows)
      E.cy++;
    break;
  case ARROW_UP:
    if (E.cy != 0)
      E.cy--;
    break;
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    break;
  case ARROW_RIGHT:
    if (row && E.cx < (int_fast32_t)row->len) {
      E.cx++;
    }
    break;
  }

  // If you change to a shorter line, the cursor column position should move
  // too.
  row = (E.cy >= E.num_rows) ? NULL : &E.rows[E.cy].chars;
  int_fast32_t rowlen = row ? row->len : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void processKeypress() {
  int64_t c = readKey();

  switch (c) {
  case CTRL_KEY('c'):
    exit(0);

  case 'H':
    E.cx = 0;
    break;
  case 'L':
    E.cx = E.screen_cols - 1;
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    uint_fast16_t times = E.screen_rows;
    while (times--)
      moveCursor(PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  // Cursor movment
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    moveCursor(c);
    break;
  }
}

/*** output ***/

void editorScroll() {
  if (E.cy < E.row_offset) {
    E.row_offset = E.cy;
  }
  if (E.cy >= E.row_offset + E.screen_rows) {
    E.row_offset = E.cy - E.screen_rows + 1;
  }

  if (E.cx < E.col_offset) {
    E.col_offset = E.cx;
  }
  if (E.cx >= E.col_offset + E.screen_cols) {
    E.col_offset = E.cx - E.screen_cols + 1;
  }
}

void drawRows(appendBuffer *ab) {
  for (int_fast32_t y = 0; y < E.screen_rows; y++) {
    int_fast32_t file_row = y + E.row_offset;
    abAppend(ab, "~");

    if (file_row < E.num_rows) {
      int_fast32_t len = E.rows[file_row].render.len - E.col_offset;

      if (len < 0)
        len = 0;

      if (E.screen_cols < len)
        len = E.screen_cols - 1;

      char place_holder = E.rows[file_row].render.buf[len + E.col_offset];
      E.rows[file_row].render.buf[len + E.col_offset] = '\0';

      abAppend(ab, &E.rows[file_row].render.buf[E.col_offset]);

      // Resotre the original line
      E.rows[file_row].render.buf[len + E.col_offset] = place_holder;
    }

    abAppend(ab, "\x1b[K"); // Erases part of the current line.

    if (y != (E.screen_rows - 1))
      abAppend(ab, "\r\n"); // Avoid new line at last line
  }

  write(STDOUT_FILENO, ab->buf, ab->len);
}

void editorRefreshScreen() {
  editorScroll();

  appendBuffer ab = new_appendBuffer();

  // https: // vt100.net/docs/vt100-ug/chapter3.html#ED
  abAppend(&ab, "\x1b[?25l"); // Hide cursor
  abAppend(&ab, "\x1b[H");    // Put cursor at the top left.

  drawRows(&ab);

  // Put cursor at his position.
  char buf[16] = {0};
  snprintf(buf, sizeof(buf), "\x1b[%lu;%luH", (E.cy - E.row_offset) + 1,
           (E.cx - E.col_offset) + 1);
  abAppend(&ab, buf);

  abAppend(&ab, "\x1b[?25h"); // Show cursor
  write(STDOUT_FILENO, ab.buf, ab.len);

  abFree(&ab);
}

/*** init ***/

void initEditor() {
  getWindowSize();
  enableRawMode();
}

int main(int argc, char *argv[]) {
  initEditor();

  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  while (1) {
    editorRefreshScreen();
    processKeypress();
  }

  return 0;
}
