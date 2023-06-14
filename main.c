#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <termios.h>
#include <unistd.h>

#define DEBUG 1

struct termios orig_termios;

struct Buffer {
  unsigned long size;
  unsigned long r_size;
  char **rows;
  unsigned long *row_size;
  unsigned long *r_row_size;
  int cx, cy; // cursor position
};

struct Screen {
  unsigned int lins;
  unsigned int cols;
};

void Buffer_dealocate(struct Buffer*);
void fatal_err(char*);
unsigned *get_term_lcol(void);
int tty_reset(void);
void tty_atexit(void);
void tty_raw(void);
void render_buf(struct Buffer *, struct Screen);

void Buffer_dealocate(struct Buffer *buf) {
  if (buf->size > 0) {
    for (int i = 0; i < buf->r_size; ++i) {
      free(buf->rows[i]);
    }
    free(buf->row_size);
    free(buf->r_row_size);
    free(buf->rows);
  }
  buf->size = 0;
  free(buf);
}

unsigned *get_term_lcol(void) {
  char const *const term = getenv("TERM");
  if (term == NULL) {
    fprintf(stderr, "TERM environmnet variable not set\n");
    return 0;
  }

  char const *const cterm_path = ctermid(NULL);
  if (cterm_path == NULL || cterm_path[0] == '\0') {
    fprintf(stderr, "ctermid() failed\n");
    return 0;
  }

  int tty_fd = open(cterm_path, O_RDWR);
  if (tty_fd == -1) {
    fprintf(stderr, "open(\"%s\") failed (%d): %s\n", cterm_path, errno,
            strerror(errno));
    return 0;
  }

  int cols = 0, lins = 0;
  int setupterm_err;
  int flag = 0;

  if (setupterm((char *)term, tty_fd, &setupterm_err) == ERR) {
    switch (setupterm_err) {
    case -1:
      fprintf(stderr, "setupterm() failed: terminfo database not found\n");
      flag = 1;
      break;

    case 0:
      fprintf(stderr, "setupterm() failed: TERM=%s not found in database\n",
              term);
      flag = 1;
      break;

    case 1:
      fprintf(stderr, "setupterm() failed: terminal is hardcopy\n");
      flag = 1;
      break;
    }
  }

  if (!flag) {
    cols = tigetnum((char *)"cols");
    if (cols < 0) {
      fprintf(stderr, "tigetnum() failed (%d)\n", cols);
    }
    lins = tigetnum((char *)"lines");
    if (lins < 0) {
      fprintf(stderr, "tigetnum() failed (%d)\n", lins);
    }
  }

  if (tty_fd != -1) {
    close(tty_fd);
  }

  cols = cols < 0 ? 0 : cols;
  lins = lins < 0 ? 0 : lins;

  unsigned *lcol = malloc(2 * sizeof(int));
  lcol[0] = lins;
  lcol[1] = cols;

  return lcol;
}

int tty_reset(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) < 0)
    return -1;
  return 0;
}

void tty_atexit(void) { 
  tty_reset();
}

void tty_raw(void) {
  struct termios raw;
  raw = orig_termios;

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // control chars
  raw.c_cc[VMIN] = 5;
  raw.c_cc[VTIME] = 8; /* after 5 bytes or .8 seconds after first byte seen */

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) fatal_err("can't set raw mode");
}

void render_buf(struct Buffer *buf, struct Screen scr) {
  // write(STDOUT_FILENO, "\033[2J", 4);
  write(STDOUT_FILENO, "\033[H", 3);
  for (int i = 0; i < buf->size; ++i) {
    // printf("\n%u\n", buf->size);
    // printf("\n%u %d\n", buf->row_size[i], i);
    if (i > 0) {
      char c_out[] = "\r\n";
      write(STDOUT_FILENO, c_out, 2);
    }

    for (int j = 0; j < buf->row_size[i]; ++j) {
      char c_out = buf->rows[i][j];
      write(STDOUT_FILENO, &c_out, 1);
    }
  }
}

int get_input(struct Buffer* buf) {
  char c_in;
  int bytesread;

  char esc_seq[100];
  sprintf(esc_seq, "\033[%d;%dH", buf->cx+1, buf->cy+1);
  write(STDOUT_FILENO, esc_seq, strlen(esc_seq));


  bytesread = read(STDIN_FILENO, &c_in, 1);

  if (bytesread < 0) fatal_err("read error");
  if (bytesread == 0) { // timed out
    return -1;
  }

  else {
    switch(c_in) {
      default:
        return c_in;
    }
  }
}

void fatal_err(char *message) {
  fprintf(stderr, "fatal error: %s\n", message);
  exit(1);
}

void buffer_write(struct Buffer* buf, char c) {
  char up[] = "\033[A";
  char down[] = "\033[B";
  char right[] = "\033[C";
  char left[] = "\033[D";

  char ret_code = 13;
  char nl = '\n';

  if (buf->size == 0) {
    buf->size = 1;
    buf->r_size = 10;

    buf->rows = malloc(10 * sizeof(char*));

    buf->row_size = malloc(10 * sizeof(int*));
    buf->r_row_size = malloc(10 * sizeof(int*));

    buf->r_row_size[0] = 100;

    buf->rows[0] = malloc(100 * sizeof(char));

    strncat(buf->rows[buf->cx], &c, 1);
    buf->cy++;
    buf->row_size[0] = 1;
  }

  else if (c == ret_code || c == nl) {
    if (buf->size + 1 >= buf->r_size) {
      buf->r_size += 10;

      buf->rows = realloc(buf->rows, buf->r_size);
      buf->row_size = realloc(buf->row_size, buf->r_size);
      buf->r_row_size = realloc(buf->r_row_size, buf->r_size);
    }
    buf->cx++;
    buf->size++;
    buf->cy = 0;
  } 


  else if (c == 127) {
    buf->cy--;
    buf->row_size[buf->cx]--;
    buf->rows[buf->cx][buf->cy] = '\0';

  char esc_seq[100];
  sprintf(esc_seq, "\033[%d;%dH", buf->cx+1, buf->cy+1);
  write(STDOUT_FILENO, esc_seq, strlen(esc_seq));
  write(STDOUT_FILENO, "\033[0K", 4);
  }

  else {

    if (buf->row_size[buf->cx] + 1 >= buf->r_row_size[buf->cx]) {
      buf->r_row_size[buf->cx] += 100;
      buf->rows[buf->cx] = realloc(buf->rows[buf->cx],
                                   (buf->r_row_size[buf->cx]) * sizeof(char*));
    }


    strncat(buf->rows[buf->cx], &c, 1);
    buf->cy++;
    buf->row_size[buf->cx]++;
  }
}

int main() {

  if (!isatty(STDIN_FILENO))
    fatal_err("fatal error: not on a tty");

  if (tcgetattr(STDIN_FILENO, &orig_termios) < 0)
    fatal_err("fatal error: can't get tty settings");

  if (atexit(tty_atexit) != 0)
    fatal_err("atexit: can't register tty reset");

  tty_raw();
  write(STDOUT_FILENO, "\033[2J", 4);
  write(STDOUT_FILENO, "\033[0;0H", 6);

  struct Screen scr;

  unsigned *lcol = get_term_lcol();
  scr.lins = lcol[1];
  scr.cols = lcol[0];

  free(lcol);

  struct Buffer *buf = (struct Buffer *)malloc(sizeof(struct Buffer));
  buf->cx = 0;
  buf->cy = 0;

  int w_limit = 500;
  while (w_limit--) {
    // tcgetattr(STDIN_FILENO, &orig_termios);
    int i_inp;
    char c_inp;

    render_buf(buf, scr);
    i_inp = get_input(buf);
    if (i_inp == 24) {
      break;
    }

    c_inp = (char) i_inp;
    buffer_write(buf, c_inp);
    // tty_reset();
  }

  Buffer_dealocate(buf);

  return 0;
}
