#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "migration_header.h"

char CHECKPOINT_PATH[256] = "process_checkpoint.ckpt";

short
Poll(int, int);

void
ReadPagesContext(int, int*);

void*
MapStack(void*, size_t);

struct memorySection*
GetStackMemorySection();

void
RestoreMemory(int);

void* CreateMmap(struct memorySection);

ucontext_t context;

struct memorySection stackMemoryRegion;

int
main(int argc, char* argv[])
{
  int sockFd, numReadPages;

  in_addr_t ipAddr;
  in_port_t portNo;

  struct sockaddr_in clientAddr;
  socklen_t clientAddrLen;

  struct memorySection* temp;

  VA stackStartAddr = 0x5300000;
  void *stackEndAddr, *stackAssignedAddr;
  size_t stackSize;

  if (argc < 3) {
    fprintf(stderr, "IP Address and Port number required \n");
    exit(1);
  }

  ipAddr = inet_addr(argv[1]);
  portNo = atoi(argv[2]);

  sockFd = WaitForMigration(
    ipAddr, portNo, (struct sockaddr*)&clientAddr, &clientAddrLen);

  // TODO get address of canCheckpoint variable also
  ReadPagesContext(sockFd, &numReadPages);

  temp = GetStackMemorySection();
  if (temp) {
    stackSize = temp->end - temp->start;
    stackMemoryRegion = *temp;
  } else {
    ShowError("No original stack (Unknown error)", 0);
  }

  stackAssignedAddr = MapStack((void*)stackStartAddr, stackSize);
  if (stackAssignedAddr) {
    stackEndAddr = stackAssignedAddr + stackSize;

    asm volatile("mov %0,%%rsp" : : "g"(stackEndAddr) : "memory");
    RestoreMemory(numReadPages);
  }

  return 0;
}

void
ReadPagesContext(int sockFd, int* numReadPages)
{
  int i, ret;
  short events;
  size_t tempSize;

  struct memorySection* temp;
  void* buf;

  int fd = open(CHECKPOINT_PATH, O_RDWR | O_CREAT);

  events = Poll(sockFd, -1);
  if (events == -1) {
    ShowError("", errno);
  }

  if (events & (POLLHUP | POLLRDHUP)) {
    ShowError("connection lost", 0);
  }

  ret = read(sockFd, (void*)numReadPages, sizeof(int));
  if (ret == -1) {
    ShowError("", errno);
  }

  for (i = 0; i <= *numReadPages; i++) {
    events = Poll(sockFd, -1);
    if (events == -1) {
      ShowError("", errno);
    }

    if (events & (POLLHUP | POLLRDHUP)) {
      ShowError("connection lost", 0);
    }

    if (events & POLLIN) {
      // read data
      if (i < *numReadPages) {
        if (i % 2 == 0) {
          temp = malloc(sizeof(struct memorySection));

          ret = read(sockFd, (void*)temp, sizeof(struct memorySection));
          if (ret == -1) {
            ShowError("", errno);
          }

          ret = write(fd, (void*)temp, sizeof(struct memorySection));
          if (ret == -1) {
            ShowError("", errno);
          }
        } else {
          tempSize = temp->end - temp->start;
          buf = malloc(tempSize);

          ret = read(sockFd, buf, tempSize);
          if (ret == -1) {
            ShowError("", errno);
          }

          ret = write(fd, buf, tempSize);
          if (ret == -1) {
            ShowError("", errno);
          }
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

  close(fd);
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

// maps stack in the virtual memory at provided address using given stack size
// returns: the starting address of the mapped stack
void*
MapStack(void* startAddr, size_t stackSize)
{
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN;
  void* addr = mmap(startAddr, stackSize, prot, flags, -1, 0);

  if (addr == MAP_FAILED) {
    printf("Stack Map failed\n");
    return NULL;
  }

  return addr;
}

// reads memory maps of current process and
// returns: the MemoryRegion of the current stack or NULL if stack is not
// present in process maps
struct memorySection*
GetStackMemorySection()
{
  char fileName[21], line[256], *name;
  int pid, ret;
  char stackFound = 0;

  pid = getpid();
  sprintf(fileName, "/proc/%d/maps", pid);

  int fileReadFd = open(fileName, O_RDONLY);
  if (fileReadFd == -1) {
    ShowError("", errno);
  }

  struct memorySection* mem = malloc(sizeof(struct memorySection));
  while (1) {
    ret = readLine(fileReadFd, line);
    if (ret <= 0)
      break;

    parseSectionHeader(line, mem);
    name = getNameFromSectionLine(line);
    if (name && !strcmp(name, "[stack]")) {
      stackFound = 1;
      break;
    }
  }

  close(fileReadFd);
  return stackFound ? mem : NULL;
}

void
RestoreMemory(int numReadPages)
{
  int i;
  struct memorySection mem;

  void* mapped;

  int fd, ret;

  fd = open(CHECKPOINT_PATH, O_RDONLY);
  if (fd == -1) {
    ShowError("", errno);
  }

  // unmap old stack
  ret = munmap((void*)stackMemoryRegion.start,
               stackMemoryRegion.end - stackMemoryRegion.start);
  if (ret == -1) {
    ShowError("", errno);
  }

  for (i = 0; i < numReadPages; i++) {
    ret = read(fd, &mem, sizeof(mem));
    if (ret == -1) {
      ShowError("", errno);
    }

    // mapping memory region
    mapped = CreateMmap(mem);
    if (mapped == MAP_FAILED) {
      printf("map failed\n");
      continue;
    }

    // reading address block and writing to appropriate address
    ret = read(fd, mapped, mem.end - mem.start);
    if (ret == -1) {
      ShowError("", errno);
    }
  }

  close(fd);
}

// creates mapping in the virtual memory using mem struct and
// returns: the start address of the mapping
void*
CreateMmap(struct memorySection mem)
{
  int prot = 0, flags = 0;

  if (mem.permissions[0] == 'r')
    prot |= PROT_READ;

  // if (mem.isWriteable)
  prot |= PROT_WRITE;

  if (mem.permissions[2] == 'x')
    prot |= PROT_EXEC;

  if (mem.permissions[3] == 'p')
    flags = MAP_PRIVATE;
  else
    flags = MAP_SHARED;

  flags |= MAP_ANONYMOUS;

  return mmap((void*)(mem.start), mem.end - mem.start, prot, flags, -1, 0);
}