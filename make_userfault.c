
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "migration_header.h"

struct Fds
{
  int userftfd, sockfd;
};

void
readFaults(void*);

void
makeUserfault(struct memorySection* sections, int numSections, int sockfd)
{
  int fd, i;
  struct uffdio_api api = {.api = UFFD_API };
  struct uffdio_register* reg;
  pthread_t thread;
  struct Fds* fds;

  fd = syscall(SYS_userfaultfd, O_NONBLOCK);
  if (fd == -1) {
    ShowError("", errno);
  }

  if (ioctl(fd, UFFDIO_API, &api)) {
    ShowError("ioctl UFFDIO_API failed", 0);
  }

  if (api.api != UFFD_API) {
    ShowError("unexpected UFFD api version");
  }

  for (i = 0; i < numSections; i++) {
    reg = malloc(sizeof(struct uffdio_register));
    reg->mode = UFFDIO_REGISTER_MODE_MISSING;
    reg->range.start = sections[i].start;
    if (ioctl(fd, UFFDIO_REGISTER, reg)) {
      ShowError("ioctl(fd, UFFDIO_REGISTER, ...) failed", 0);
    }

    if (reg->ioctls != UFFD_API_RANGE_IOCTLS) {
      ShowError("Unexpected UFFD ioctls", 0);
    }
  }

  fds = malloc(sizeof(struct Fds));
  fds->userftfd = fd;
  fds->sockfd = sockfd;

  pthread_create(&thread, NULL, readFaults, fds);
}

void
readFaults(void* arg)
{
  int userftfd, ret, sockfd;
  struct uffd_msg* faultMsg;
  struct uffdio_copy* copy;

  struct Fds fds = (struct Fds*)arg;
  void* pageInfo;

  userftfd = fds->userftfd;
  sockfd = fds->sockfd;

  struct pollfd pfd = {.fd = userftfd, .events = POLLIN };

  while (true) {
    ret = poll(&pfd, 1, -1);
    if (ret == -1) {
      ShowError("", errno);
    }

    if (pfd.revents & POLLERR) {
      ShowError("POLLERR occurred", 0);
    } else if (pfd.revents & POLLHUP) {
      ShowError("POLLHUP occurred", 0);
    }

    faultMsg = malloc(sizeof(struct uffd_msg));
    ret = read(userftfd, faultMsg, sizeof(struct uffd_msg));
    if (ret != sizeof(struct uffd_msg)) {
      ShowError("reading fault msg failed", 0);
    }

    if (faultMsg->event != UFFD_EVENT_PAGEFAULT) {
      ShowError("unexpected pagefault"), 0;
    }

    void* addr = (void*)faultMsg->arg.remap.from;
    ret = write(sockfd, &addr, sizeof(void*));
    if (ret == -1) {
      ShowError("", errno);
    }

    pageInfo = malloc(faultMsg->arg.remap.len);

    ret = ReadUsingPoll(sockfd, -1, pageInfo, faultMsg->arg.remap.len);
    if (ret == -1) {
      ShowError("", errno);
    }

    copy = malloc(sizeof(struct uffdio_copy));
    copy->dst = addr;
    copy->src = pageInfo;
    copy->len = faultMsg->arg.remap.len;

    if (ioctl(userftfd, UFFDIO_COPY, copy)) {
      ShowError("error while UFFDIO_COPY", 0);
    }
  }
}