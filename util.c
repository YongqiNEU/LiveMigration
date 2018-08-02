#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
ShowError(char* msg, int errno)
{
  if (errno) {
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  } else {
    fprintf(stderr, "%s\n", msg);
  }

  exit(1);
}