#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <unistd.h>

struct Buffer {
  unsigned long size;
  char **rows;
  unsigned long *row_size;
};

void Buffer_dealocate(struct Buffer *buf) {
  if (buf->size > 0) {
    for (int i = 0; i < buf->size; ++i) {
      for (int j = 0; j < buf->row_size[i]; ++j) {
        free(buf->rows[i]);
      }
    }
    free(buf->row_size);
  }
  buf->size = 0;
  free(buf);
}

unsigned get_term_width(void) {
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

  int cols = 0;
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
  }

  if (tty_fd != -1) {
    close (tty_fd);
  }
  return cols < 0 ? 0 : cols;
}

int main() {
  printf("%d\n", get_term_width());

  struct Buffer *buf = (struct Buffer *)malloc(sizeof(struct Buffer));

  Buffer_dealocate(buf);

  return 0;
}
