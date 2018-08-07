#define _GNU_SOURCE

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "migration_header.h"

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

// write memory seections to destination fd;
void
writeToImage(int fd, struct memorySection* section)
{
  ssize_t ret = write(fd, section, sizeof(struct memorySection));
  if (ret != sizeof(struct memorySection)) {
    printf("Section write failed \n");
  }

  ret = write(fd, section->start, section->end - section->start);
  if (ret != section->end - section->start) {
    printf("Section data failed to write\n");
  }
}

int
readLine(int checkpoint_image_fd, char line[256])
{
  char c = 0;
  int len = 0;

  while (len == 0 || c != '\n') {
    if (read(checkpoint_image_fd, &c, 1) == 0) {
      // printf("Reach the end of the file \n");
      return len;
    }

    *line = c;
    line += 1;
    len += 1;
  }

  *line = '\0';
  return len;
}

// parse a section header from a memory section
// store info into memory section structure
void
parseSectionHeader(char* header, struct memorySection* section)
{
  sscanf(header,
         "%p-%p %c%c%c%c %x",
         &section->start,
         &section->end,
         &section->permissions[0],
         &section->permissions[1],
         &section->permissions[2],
         &section->permissions[3],
         &section->offset);
}

char*
getNameFromSectionLine(char* offset)
{
  int i;
  char *str1, *delim = " ", *saveptr, *token;

  for (i = 1, str1 = offset;; i++, str1 = NULL) {
    token = strtok_r(str1, delim, &saveptr);
    if (token == NULL) {
      break;
    }

    if (i == 6) {
      return token;
    }
  }

  return NULL;
}

void
copyMemorySection(struct memorySection* dest, struct memorySection* src)
{
  int i;

  dest->start = src->start;
  dest->end = src->end;

  for (i = 0; i < 4; i++) {
    dest->permissions[i] = src->permissions[i];
  }

  dest->offset = src->offset;
}

// Returns: -1 in case of error and errno set appropriately
//          0 when size bytes are written to addr
int
ReadUsingPoll(int sockFd, int timeout, void* addr, int size)
{
  struct pollfd* fd;
  int ret = 0, curPtr = 0;

  while (size > 0) {
    fd = malloc(sizeof(struct pollfd));
    fd->fd = sockFd;
    fd->events = POLLIN | POLLRDHUP | POLLHUP;

    ret = poll(fd, 1, timeout);
    if (ret == -1) {
      return -1;
    }

    if (fd->revents & (POLLHUP | POLLRDHUP)) {
      ShowError("connection lost", 0);
    }

    if (fd->revents & POLLIN) {
      addr = (void*)((char*)addr + curPtr);
      curPtr = read(sockFd, addr, size);
      if (curPtr == -1) {
        return -1;
      }
      // printf("%d\n", curPtr);
      size -= curPtr;
    } else {
      ShowError("data expected but no data obtained", 0);
      return -1;
    }
  }

  return 0;
}

// reads a string containing hexadecimal characters and convertes it to an
// integer value
void
ReadHex(char* hex, VA* value)
{
  int len = strlen(hex), i = 0;
  char c;
  VA v;

  v = 0;
  for (; i < len; i++) {
    c = hex[i];
    if ((c >= '0') && (c <= '9'))
      c -= '0';
    else if ((c >= 'a') && (c <= 'f'))
      c -= 'a' - 10;
    else if ((c >= 'A') && (c <= 'F'))
      c -= 'A' - 10;
    else
      break;

    v = v * 16 + c;
  }

  *value = v;
}