#define _GNU_SOURCE

#include "appendBuffer.c"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
/// It sets the upper 3 bits of the character to 0, like the Ctrl key.
#define CTRL_KEY(k) ((k)&0x1f)
#define TAB_STOP 4
#define STATUS_MSG_TIMEOUT 5
#define QUIT_TIMES 2

enum editorKey {
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
};

enum editorHighlight { HL_NORMAL = 0, HL_NUMBER, HL_MATCH };

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
void editorRefreshScreen();
void setStatusMessage(const char *fmt, ...);

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

uint64_t readKey() {
  uint64_t c = '\0';
  int32_t nread = 0;

  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  // Handle multibyte sequences.
  if (c == '\x1b') {
    uint64_t seq[3] = {0};
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return c;

    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return c;

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

  return c;
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

  if (sscanf(&buf[2], "%lu;%lu", &E.cx, &E.cy) != 2)
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

/*** syntax highlighting ***/

void editorUpdateSyntax(row *row) {
  row->hl = realloc(row->hl, row->render.len);
  memset(row->hl, HL_NORMAL, row->render.len);

  // TODO Add logic that sets the highlighted areas.
}

uint8_t editorSyntaxToColor(uint8_t hl) {
  switch (hl) {
  case HL_NUMBER:
    return 31;

  case HL_MATCH:
    return 34;

  default:
    return 37;
  }
}

/*** row operations ***/

/// Translates the pointer position from actual to render.
uint_fast32_t editorRowCxToRx(row *row, uint_fast32_t cx) {
  uint_fast32_t rx = 0;

  for (uint_fast32_t j = 0; j < cx; j++) {
    if (row->chars.buf[j] == '\t')
      rx += TAB_STOP - (rx % TAB_STOP);

    rx++;
  }

  return rx;
}

uint_fast32_t editorRowRxToCx(row *row, uint_fast32_t rx) {
  uint_fast32_t cur_rx = 0;
  uint_fast32_t cx = 0;

  for (cx = 0; cx < row->chars.len; cx++) {
    if (row->chars.buf[cx] == '\t')
      cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
    cur_rx++;

    if (cur_rx > rx)
      return cx;
  }

  return cx;
}

/// Copies Chars into Renders and replaces tabs for spaces
void updateRow(row *r) {
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

  editorUpdateSyntax(r);
}

void insertRowAt(char *s, size_t at) {
  if (at > E.num_rows)
    return;

  // TODO efficient resize.
  E.rows = realloc(E.rows, sizeof(row) * (E.num_rows + 1));
  memmove(&E.rows[at + 1], &E.rows[at], sizeof(row) * (E.num_rows - at));

  E.rows[at] = new_row();
  abAppend(&E.rows[at].chars, s);
  updateRow(&E.rows[at]);

  E.num_rows++;
}

void editorFreeRow(row *row) {
  abFree(&row->chars);
  abFree(&row->render);
  free(row->hl);
}

void editorDelRow(size_t at) {
  if (at >= (size_t)E.num_rows)
    return;

  editorFreeRow(&E.rows[at]);

  memmove(&E.rows[at], &E.rows[at + 1], sizeof(row) * (E.num_rows - at - 1));

  E.num_rows--;
  E.dirty = 1; // Mark file as dirty.
}

void rowInsertChar(row *row, size_t at, size_t c) {
  abInsertAt(&row->chars, at, c);
  updateRow(row);
}

void rowDelChar(row *row, size_t at) {
  abRemoveAt(&row->chars, at);
  updateRow(row);

  E.dirty = 1; // Mark file as dirty.
}

void editorRowAppendString(row *src, row *dst) {
  abAppend(&dst->chars, src->chars.buf);
  updateRow(dst);

  E.dirty = 1; // Mark file as dirty.
}

void editorInsertNewline() {
  if (E.cx == 0) {
    insertRowAt("", E.cy);
  } else {
    row *row = &E.rows[E.cy];

    insertRowAt(&row->chars.buf[E.cx], E.cy + 1); // TODO fix length maybe
    row = &E.rows[E.cy];
    row->chars.len = E.cx;
    row->chars.buf[row->chars.len] = '\0';

    updateRow(row);
  }

  E.cy++;
  E.cx = 0;
}

void editorDelChar() {
  if (E.cy == E.num_rows)
    return;

  // On the first char of the file.
  if (E.cx == 0 && E.cy == 0)
    return;

  row *row = &E.rows[E.cy];

  if (E.cx > 0) {
    rowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    // At the beginning of a line, we have to move the contents of the current
    // line to the one above it.
    E.cx = E.rows[E.cy - 1].chars.len;
    editorRowAppendString(row, &E.rows[E.cy - 1]);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/*** editor operations ***/
void editorInsertChar(size_t c) {
  if (E.cy == E.num_rows) {
    insertRowAt("", 0);
  }

  rowInsertChar(&E.rows[E.cy], E.cx, c);
  E.cx++;
  E.dirty = 1; // Mark file as dirty.
}

/*** file I/O ***/

/// Join all the rows in the file into a single `appendBuffer`.
appendBuffer editorRowsToString() {
  appendBuffer ab = newAppendBuffer();

  for (uint_fast32_t idx = 0; idx < E.num_rows; idx++) {
    abAppend(&ab, E.rows[idx].chars.buf);
    abAppend(&ab, "\n");
  }

  return ab;
}

void editorOpen(char *filename) {
  FILE *fp = fopen(filename, "r");

  if (!fp)
    die("fopen");

  E.filename = strdup(filename);
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen = -1;

  while ((linelen = getline(&line, &linecap, fp)) != -1) {

    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;

    line[linelen] = '\0';
    insertRowAt(line, E.num_rows);
  }

  free(line);
  fclose(fp);
}

void editorSave() {
  // TODO Will this block the UI? Probably. Make the save async.
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s", NULL);

    if (E.filename == NULL) {
      setStatusMessage("Save aborted");
      return;
    }
  }

  // 0644: Owner can read an write, everyone else just read.
  int64_t fd = open(E.filename, O_RDWR | O_CREAT, 0644);

  if (fd == -1) {
    close(fd);
    return;
  }

  appendBuffer file_content = editorRowsToString();

  if (ftruncate(fd, file_content.len) == -1 ||
      write(fd, file_content.buf, file_content.len) !=
          (ssize_t)file_content.len) {
    setStatusMessage("Can't save! I/O error: %s", strerror(errno));
  } else {
    setStatusMessage("%d bytes written to disk", file_content.len);
    E.dirty = 0; // Mark file as clean.
  }

  close(fd);
  abFree(&file_content);
}

/*** find ***/

void editorFindCallback(char *query, size_t key) {
  static ssize_t last_match = -1;
  static ssize_t direction = 1;

  static size_t saved_hl_line = 0;
  static char *saved_hl = NULL;

  if (saved_hl) {
    // Restore previous highlighted match.
    memcpy(E.rows[saved_hl_line].hl, saved_hl,
           E.rows[saved_hl_line].render.len);
    free(saved_hl);
    saved_hl = NULL;
  }

  if (key == ENTER || key == ESC) {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    // Next match. TODO make 'n'
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    // Previous match. TODO make 'p'
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1)
    direction = 1;

  ssize_t current = last_match;

  for (uint_fast32_t i = 0; i < E.num_rows; i++) {
    current += direction;

    if (current == -1)
      current = E.num_rows - 1;
    else if (current == (ssize_t)E.num_rows)
      current = 0;

    row *row = &E.rows[current];
    char *match = strstr(row->render.buf, query);

    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render.buf);
      E.row_offset = E.num_rows;

      // Highlight the match, and save the line to restore it later.
      saved_hl_line = current;
      saved_hl = calloc(sizeof(row->render.buf), row->render.len);
      memcpy(saved_hl, row->hl, row->render.len);
      memset(&row->hl[match - row->render.buf], HL_MATCH, strlen(query));

      break;
    }
  }
}

/// Prompts the user for a string and moves the cursor to the first match in
/// the file.
void editorFind() {
  // TODO Highlight all the matches.
  uint_fast32_t saved_cx = E.cx;
  uint_fast32_t saved_cy = E.cy;
  uint_fast32_t saved_rowoff = E.row_offset;
  uint_fast32_t saved_coloff = E.col_offset;

  char *query =
      editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

  if (query) {
    free(query);
  } else {
    // Restore the cursor position.
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.row_offset = saved_rowoff;
    E.col_offset = saved_coloff;
  }
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, size_t)) {
  // TODO put cursor in the status bar.
  appendBuffer input = newAppendBuffer();

  while (1) {
    setStatusMessage(prompt, input.buf);
    editorRefreshScreen();

    uint64_t c = readKey();

    switch (c) {
    case BACKSPACE:
      abPop(&input);
      break;

    case ESC:
      setStatusMessage("");
      if (callback)
        callback(input.buf, c);

      abFree(&input);
      return NULL;

    case ENTER:
      if (input.len != 0) {
        setStatusMessage("");
        if (callback)
          callback(input.buf, c);

        return input.buf; // TODO free the rest of the input.
      }
      break;

    default:
      if (!iscntrl(c) && c < 128)
        abAppendChar(&input, c);
      break;
    }

    if (callback)
      callback(input.buf, c);
  }
}

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
    if (E.cx > (E.left_margin - 3))
      E.cx--;
    break;
  case ARROW_RIGHT:
    if (row && E.cx < (uint_fast32_t)row->len) {
      E.cx++;
    }
    break;
  }

  // If you change to a shorter line, the cursor column position should
  // move too.
  row = (E.cy >= E.num_rows) ? NULL : &E.rows[E.cy].chars;
  uint_fast32_t rowlen = row ? row->len : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void handleNormalKey(uint64_t c) {
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

  default:
    // No Op
    break;
  }
}

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

void processKeypress() {
  uint64_t c = readKey();

  if (E.mode == NORMAL)
    handleNormalKey(c);
  else
    handleInsertKey(c);
}

/*** output ***/

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.num_rows) {
    E.rx = editorRowCxToRx(&E.rows[E.cy], E.cx);
  }

  if (E.cy < (uint_fast32_t)E.row_offset) {
    E.row_offset = E.cy;
  }
  if (E.cy >= E.row_offset + E.screen_rows) {
    E.row_offset = E.cy - E.screen_rows + 1;
  }

  if (E.rx < (uint_fast32_t)E.col_offset) {
    E.col_offset = E.rx;
  }
  if (E.rx >= E.col_offset + E.screen_cols) {
    E.col_offset = E.rx - E.screen_cols + 1;
  }
}

char *foreground_from_rgb(char *buf, uint8_t r, uint8_t g, uint8_t b) {
  snprintf(buf, 32, "\033[48;2;%i;%i;%im", r, g, b);
  return buf;
}

char *background_from_rgb(char *buf, uint8_t r, uint8_t g, uint8_t b) {
  snprintf(buf, 32, "\033[38;2;%i;%i;%im", r, g, b);
  return buf;
}

void add_line_number(appendBuffer *ab, uint_fast32_t line, size_t max_width) {
  if (line > E.num_rows) {
    // File content is smaller than the height of the screen.
    return;
  }

  char back[32] = {0};
  char buf[16] = {0};
  char pad[8] = {0};

  size_t idx = 0;
  size_t num_digits = (size_t)floor(log10(line));

  while (idx != (max_width - num_digits))
    pad[idx++] = ' ';

  if (getCy() == (line - 1 - E.row_offset))   // Current row.
    background_from_rgb(back, 150, 188, 100); // Green.
  else                                        // Grey.
    background_from_rgb(back, 60, 65, 72);

  abAppend(ab, back);
  snprintf(buf, 16, "%s%lu ", pad, line);
  abAppend(ab, buf);

  abAppend(ab, background_from_rgb(back, 194, 179, 149)); // Restore, blueish
}

void drawRows(appendBuffer *ab) {
  size_t row_num_width = (size_t)floor(log10(E.num_rows + 1));
  E.left_margin = row_num_width + 1;

  for (uint_fast32_t y = 0; y < E.screen_rows; y++) {
    uint_fast32_t file_row = y + E.row_offset;
    add_line_number(ab, file_row + 1, row_num_width);

    if (file_row < E.num_rows) {
      int_fast32_t len = E.rows[file_row].render.len - E.col_offset;

      if (len < 0)
        len = 0;

      if (E.screen_cols < (uint_fast32_t)len)
        len = E.screen_cols - 1;

      char *c = &E.rows[file_row].render.buf[E.col_offset];
      uint8_t *hl = &E.rows[file_row].hl[E.col_offset];
      int8_t current_color = -1;
      char buf[32] = {0};

      // Append to the buffer char by char, while adding highlighting.
      for (int_fast32_t j = 0; j < len; j++) {
        if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, background_from_rgb(buf, 194, 179, 149));
            current_color = -1;
          }
        } else {
          uint8_t color = editorSyntaxToColor(hl[j]);

          if (color != current_color) {
            current_color = color;
            abAppend(ab, background_from_rgb(buf, 255, 165, 0));
            abAppend(ab, buf);
          }
        }

        abAppendChar(ab, c[j]);
      }
    }

    // Erases from current position to the end of the line and jumps to
    // new line.
    abAppend(ab, "\x1b[K\r\n");
  }

  write(STDOUT_FILENO, ab->buf, ab->len);
}

void drawStatusBar(appendBuffer *ab) {
  // TODO Handle narrow terminals.
  char status[256] = {0};
  char rstatus[16] = {0};
  char back[32] = {0};
  char *mode = E.mode == NORMAL ? "Normal" : "Insert";

  if (E.mode == NORMAL) {
    abAppend(ab, background_from_rgb(back, 242, 198, 128)); // Orange
  } else {
    abAppend(ab, background_from_rgb(back, 93, 198, 128)); // Green
  }

  size_t len = snprintf(status, sizeof(status), "%s > \"%.20s\" - %ldL %s",
                        mode, E.filename ? E.filename : "[No Name]", E.num_rows,
                        E.dirty ? "(modified)" : "");

  size_t rlen =
      snprintf(rstatus, sizeof(rstatus), "%ld,%ld", E.cy + 1, E.cx + 1);

  if (len > (size_t)E.screen_cols) {
    len = E.screen_cols;
  }

  abAppend(ab, "\x1b[7m");              // Invert colors.
  abAppend(ab, status);                 // Filename and number of lines.
  while (E.screen_cols - len != rlen) { // Fill with white space.
    abAppend(ab, " ");
    len++;
  }
  abAppend(ab, rstatus); // Ruler: line and column position of the cursor.
  abAppend(ab,
           "\x1b[m\r\n"); // Revert inverted colors and move to new line.
}

void drawMessageBar(appendBuffer *ab) {
  abAppend(ab, "\x1b[K"); // Clear message bar.

  // No message to display or the message timed out.
  if (E.status_msg.len == 0 ||
      (time(NULL) - E.status_msg_time) > STATUS_MSG_TIMEOUT) {
    return;
  }

  if (E.status_msg.len > (size_t)E.screen_cols) {
    E.status_msg.buf[E.screen_cols] = '\0';
    E.status_msg.len = E.screen_cols;
  }

  abAppend(ab, E.status_msg.buf);
}

void editorRefreshScreen() {
  // TODO take into account screen resize.
  //
  char back[32] = {0};
  editorScroll();

  appendBuffer ab = newAppendBuffer();

  // https: // vt100.net/docs/vt100-ug/chapter3.html#ED
  abAppend(&ab, "\x1b[?25l"); // Hide cursor
  abAppend(&ab, "\x1b[H");    // Put cursor at the top left.

  abAppend(&ab, foreground_from_rgb(back, 38, 42, 51)); // gray

  drawRows(&ab);
  drawStatusBar(&ab);
  drawMessageBar(&ab);

  // Put cursor at his position.
  char buf[16] = {0};
  snprintf(buf, sizeof(buf), "\x1b[%lu;%luH", getCy() + 1,
           getCx() + 2 + E.left_margin);
  abAppend(&ab, buf);

  if (E.mode == NORMAL) {
    abAppend(&ab, "\033[2 q"); // Cursor as a block: █
  } else {
    abAppend(&ab, "\033[6 q"); // Cursor as a beam:  |
  }

  abAppend(&ab, "\x1b[?25h"); // Show cursor
  write(STDOUT_FILENO, ab.buf, ab.len);

  abFree(&ab);
}

void setStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  abResize(&E.status_msg, 256);
  E.status_msg.len = vsnprintf(E.status_msg.buf, E.status_msg.cap, fmt, ap);

  va_end(ap);

  E.status_msg_time = time(NULL);
}

/*** init ***/

void initEditor() {
  getWindowSize();
  enableRawMode();

  // Leave space for the status bar and message bar.
  E.screen_rows -= 2;
  E.mode = NORMAL;
}

int main(int argc, char *argv[]) {
  initEditor();

  if (argc >= 2) {
    editorOpen(argv[1]);
  }
  setStatusMessage("HELP: Ctrl-S = save | Ctrl-C = quit | / = search");

  while (1) {
    editorRefreshScreen();
    processKeypress();
  }

  return 0;
}
