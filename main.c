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
#define TAB_SIZE 2

#define MAX(a, b) \
  ({ __typeof__(a) a_ = (a); \
     __typeof__(b) b_ = (b); \
     a_ > b_ ? a_ : b_; })

#define MIN(a, b) \
  ({ __typeof__(a) a_ = (a); \
     __typeof__(b) b_ = (b); \
     a_ < b_ ? a_ : b_; })

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
int render_buf(struct Buffer *, struct Screen*, int);

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

  unsigned *lcol = malloc(2 * sizeof(unsigned));
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
  write(STDOUT_FILENO, "\033[2J", 4);
  write(STDOUT_FILENO, "\033[0;0H", 6);
  // write()
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

void write_to_position(int row, int column, const char* msg) {
  char esq_sec[200];
  sprintf(esq_sec, "\033[%d;%dH%s", row, column, msg);
  write(STDOUT_FILENO, esq_sec, strlen(esq_sec));
}

const char* get_command(const char * msg, struct Screen* scr) {
  char c;
  char str[200] = "";
  char str_msg[400];

  int w_limit = 199;
  while (w_limit--) {
    strcpy(str_msg, msg);
    strcat(str_msg, str);
    write_to_position(scr->lins , 0 , str_msg);
    read(STDIN_FILENO, &c, 1);
    if (c == 13 || c == '\n') {
      break;
    } 

    else if (c == 127) {
      str[strlen(str)-1] = 0;
    } 

    else {
      strncat(str, &c, 1);
    }
  }

  const char *com = (const char *) str;

  return com;
}

int render_buf(struct Buffer *buf, struct Screen* scr, int llimit) {
  unsigned int *lcol = malloc(2 * sizeof(unsigned int));
  lcol = get_term_lcol();
  scr->lins = lcol[0];
  scr->cols = lcol[1];
  free(lcol);

  write(STDOUT_FILENO, "\033[H", 3);
  
  long limit = MAX(0L, (long) buf->size - (long) scr->lins);
  // fprintf(stderr, "\n\n%li %u %u\n", limit, scr.lins, scr.cols);

  if (limit != llimit) {
    write(STDOUT_FILENO, "\033[2J", 4);

  }

  // fprintf(stderr, "%ld %lu", limit, buf->size);
  for (int i = limit; i <  buf->size; ++i) {
  // for (int i = 0; i < buf->size; ++i) {

    if (i > limit) {
      char c_out[] = "\r\n";
      write(STDOUT_FILENO, c_out, 2);
    }

    // fprintf(stderr, "               %d row_size: %lu", i, buf->row_size[i]);
    for (int j = 0; j < buf->row_size[i]; ++j) {
      // fprintf(stderr, "\n\nprint %d %d\n", i, j);
      char c_out = buf->rows[i][j];
      write(STDOUT_FILENO, &c_out, 1);
    }
  }
  return limit;
}

int get_input(struct Buffer* buf, struct Screen *scr) {
  char c_in;
  int bytesread;

  char esc_seq[300];
  int start = MAX(0, (int) buf->size - (int) scr->lins);
  int rx = (int) buf->cx - start + 1;
  // int ry = 
  // fprintf(stderr, "%d %d", start, rx);
  sprintf(esc_seq, "\033[%d;%dH", rx, buf->cy+1);
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

void buffer_init(struct Buffer* buf) {
  buf->size = 1;
  buf->r_size = 10;

  buf->rows = calloc(10, sizeof(char*));

  buf->row_size = calloc(10, sizeof(unsigned long));
  // for (int i = 0; i < 10; ++i) buf->row_size[i] = 0;

  buf->r_row_size = calloc(10, sizeof(unsigned long));
  // for (int i = 0; i < 10; ++i) buf->r_row_size[i] = 0;

  buf->rows[0] = calloc(100, sizeof(char));
}

void buffer_write(struct Buffer* buf, char c, struct Screen* scr) {

  char ret_code = 13;
  char nl = '\n';


  if (c == ret_code || c == nl) {
    // fprintf(stderr, "\n\n\n%lu %lu\n", buf->size, buf->r_size);
    if (buf->size + 1 >= buf->r_size) {
      int old_size = buf->r_size;
      buf->r_size += 10;

      buf->rows = realloc(buf->rows, buf->r_size * sizeof(char*));

      buf->row_size = realloc(buf->row_size, buf->r_size * sizeof(unsigned long));
      memset(buf->row_size+old_size, 0, buf->r_size-old_size);

      buf->r_row_size = realloc(buf->r_row_size, buf->r_size * sizeof(unsigned long));
      memset(buf->r_row_size+old_size, 0, buf->r_size-old_size);
    }
    buf->cx++;
    buf->size++;
    buf->cy = 0;
  } 


  else if (c == 127) { // backspace
    if (buf->cy <= 0) {
      return;
    }

    buf->cy--;
    buf->row_size[buf->cx]--;
    
    if (buf->cy < buf->row_size[buf->cx]) {
      memmove(buf->rows[buf->cx]+buf->cy, buf->rows[buf->cx]+buf->cy+1, buf->row_size[buf->cx]-buf->cy);
    } else {
      buf->rows[buf->cx][buf->cy] = '\0';
    }

    char esc_seq[100];

    int start = MAX(0, (int) buf->size - (int) scr->lins);
    int rx = (int) buf->cx - start + 1;
    sprintf(esc_seq, "\033[%d;%dH", rx, buf->cy);
    write(STDOUT_FILENO, esc_seq, strlen(esc_seq));
    write(STDOUT_FILENO, "\033[0K", 4);
  } 

  else if (c == '\t') { // convert tabs to spaces
    // fprintf(stderr, "\n\n\n%d %d", buf->row_size[buf->cx], buf->cy);

    if (buf->row_size[buf->cx] + TAB_SIZE >= buf->r_row_size[buf->cx]) {
      buf->r_row_size[buf->cx] += 100;
      buf->rows[buf->cx] = realloc(buf->rows[buf->cx],
                                   (buf->r_row_size[buf->cx]) * sizeof(char));
    }

    if (buf->row_size[buf->cx] == buf->cy) {
      for (int i = 0; i < TAB_SIZE; ++i) {
        strcat(buf->rows[buf->cx], " ");
      }
      buf->cy += TAB_SIZE;
      buf->row_size[buf->cx] += TAB_SIZE;

    } else {
      memmove(buf->rows[buf->cx]+buf->cy+TAB_SIZE, buf->rows[buf->cx]+buf->cy, buf->row_size[buf->cx]-buf->cy-1);
      for (int i = buf->cy; i < buf->cy+TAB_SIZE; ++i) {
        buf->rows[buf->cx][i] = ' ';
      }
      buf->cy += TAB_SIZE;
      buf->row_size[buf->cx] += TAB_SIZE;
    }
    
  }
  else { //writable chars

    if (buf->row_size[buf->cx] + 1 >= buf->r_row_size[buf->cx]) {
      buf->r_row_size[buf->cx] += 100;
      buf->rows[buf->cx] = realloc(buf->rows[buf->cx],
                                   (buf->r_row_size[buf->cx]) * sizeof(char));
    }



    if (buf->row_size[buf->cx] == buf->cy) {
      strncat(buf->rows[buf->cx], &c, 1);
      buf->cy++;
      buf->row_size[buf->cx]++;

    } else {
      memmove(buf->rows[buf->cx] + buf->cy + 1, buf->rows[buf->cx] + buf->cy, buf->row_size[buf->cx] - buf->cy+1);
      buf->rows[buf->cx][buf->cy] = c;
      buf->cy++;
      buf->row_size[buf->cx]++;
    }
  }
}

void handle_key(const char* str, struct Buffer* buf) {
  if (!strcmp(str, "up")) {
    if (buf->cx > 0) {
      buf->cx--;
      buf->cy = MIN(buf->cy, buf->row_size[buf->cx]);
    }
  } else if (!strcmp(str, "down")) {
    if (buf->cx < buf->size - 1) {
      buf->cx++;
      buf->cy = MIN(buf->cy, buf->row_size[buf->cx]);
    }
  } else if (!strcmp(str, "left")) {
    if (buf->cy > 0) {
      buf->cy--;
    }
  } else if (!strcmp(str, "right")) {
    if (buf->cy < buf->row_size[buf->cx]) {
      buf->cy++;
    }
  }
}

int save_file(const char* filename, struct Buffer* buf) {
  FILE* file_pointer = fopen(filename, "w+");

  if (file_pointer == NULL) {
    fprintf(stderr, "Couldn't open file %s\n", filename);
    return -2;
  }

  for (int i = 0; i < buf->size; ++i) {
    if (i > 0) {
      if (fputs("\n", file_pointer) == EOF) {
        fprintf(stderr, "Error while writing to file %s\n", filename);
        return -1;
      }
    }

    for (int j = 0; j < buf->row_size[i]; ++j) {
      if (fputc(buf->rows[i][j], file_pointer) == EOF) {
        fprintf(stderr, "Error while writing to file %s\n", filename);
        return -1;
      }
    }
  }
  return 0;
}

int main(int argc, char** argv) {

  if (!isatty(STDIN_FILENO))
    fatal_err("fatal error: not on a tty");

  if (tcgetattr(STDIN_FILENO, &orig_termios) < 0)
    fatal_err("fatal error: can't get tty settings");

  if (atexit(tty_atexit) != 0)
    fatal_err("atexit: can't register tty reset");

  char filename[300] = "";

  if (argc > 1) {
    strcpy(filename, argv[1]);
  }

  tty_raw();
  write(STDOUT_FILENO, "\033[2J", 4);
  write(STDOUT_FILENO, "\033[0;0H", 6);

  struct Screen* scr = malloc(sizeof(struct Screen));

  unsigned *lcol = get_term_lcol();
  scr->lins = lcol[1];
  scr->cols = lcol[0];

  free(lcol);

  struct Buffer *buf = (struct Buffer *)malloc(sizeof(struct Buffer));
  buf->cx = 0;
  buf->cy = 0;


  buffer_init(buf);

  int w_limit = 500;
  int llimit = 0;
  while (w_limit--) {
    // tcgetattr(STDIN_FILENO, &orig_termios);
    int i_inp;
    char c_inp;

    llimit = render_buf(buf, scr, llimit);
    i_inp = get_input(buf, scr);
    if (i_inp == 24) { // ctr-x
      break;
    }

    else if (i_inp == 6) {
      const char* newfname = get_command("Save file as: ", scr);
      write(STDOUT_FILENO, "\033[2J", 4);
      strcpy(filename, newfname);
    }

    else if ((char) i_inp == '\033') { // especial characters
      char sec_char = get_input(buf, scr);
      if (sec_char == '[') {
        char third_char = get_input(buf, scr);
        switch(third_char) {
          case 'A':
            handle_key("up", buf);
            break;

          case 'B':
            handle_key("down", buf);
            break;

          case 'C':
            handle_key("right", buf);
            break;

          case 'D':
            handle_key("left", buf);
            break;
        } 
      }
    } 

    else if (i_inp == 19) {
      save_file(filename, buf);
    } 

    else {
      c_inp = (char) i_inp;
      buffer_write(buf, c_inp, scr);
    }
    // tty_reset();
  }

  Buffer_dealocate(buf);

  return 0;
}
