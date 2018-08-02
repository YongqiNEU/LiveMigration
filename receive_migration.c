#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "migration_header.h"

short
Poll(int, int);

void
ReadPagesContext(int, int*, struct memorySection*);

ucontext_t context;

int
main(int argc, char* argv[])
{
  int sockFd, numReadPages;

  in_addr_t ipAddr;
  in_port_t portNo;

  struct sockaddr_in clientAddr;
  socklen_t clientAddrLen;

  struct memorySection* readMemorySections;

  if (argc < 3) {
    fprintf(stderr, "IP Address and Port number required \n");
    exit(1);
  }

  ipAddr = inet_addr(argv[1]);
  portNo = atoi(argv[2]);

  sockFd = WaitForMigration(
    ipAddr, portNo, (struct sockaddr*)&clientAddr, &clientAddrLen);

  ReadPagesContext(sockFd, &numReadPages, readMemorySections);
}

void
ReadPagesContext(int sockFd,
                 int* numReadPages,
                 struct memorySection* readMemorySections)
{
  int i, ret;
  short events;

  for (i = 0; i < 3; i++) {
    events = Poll(sockFd, -1);
    if (events == -1) {
      ShowError("", errno);
    }

    if (events & (POLLHUP | POLLRDHUP)) {
      ShowError("connection lost", 0);
    }

    if (events & POLLIN) {
      // read data
      if (i == 0) {
        ret = read(sockFd, (void*)numReadPages, sizeof(int));
        if (ret == -1) {
          ShowError("", errno);
        }
      } else if (i == 1) {
        readMemorySections =
          malloc(sizeof(struct memorySection) * *numReadPages);

        ret = read(sockFd,
                   (void*)readMemorySections,
                   sizeof(struct memorySection) * *numReadPages);
        if (ret == -1) {
          ShowError("", errno);
        }
      } else {
        ret = read(sockFd, (void*)&context, sizeof(ucontext_t));
        if (ret == -1) {
          ShowError("", errno);
        }
      }
    } else {
      ShowError("data expected but no data obtained", 0);
    }
  }
}

short
Poll(int sockFd, int timeout)
{
  struct pollfd fd;
  int ret;

  fd.fd = sockFd;
  fd.events = POLLIN | POLLRDHUP | POLLHUP;

  ret = poll(&fd, 1, timeout);
  if (ret == -1) {
    return -1;
  }

  return fd.revents;
}