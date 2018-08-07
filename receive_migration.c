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
#include <ucontext.h>
#include <unistd.h>

#include "migration_header.h"

char CHECKPOINT_PATH[256] = "process_checkpoint.ckpt";

int
Poll(int, int);

void
ReadPagesContext(int, int*);

void*
MapStack(void*, size_t);

struct memorySection*
GetStackMemorySection();

void
RestoreMemory(int, int);

void* CreateMmap(struct memorySection);

ucontext_t context;

struct memorySection stackMemoryRegion;

int
main(int argc, char* argv[])
{
  int sockFd, numPages;

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
  ReadPagesContext(sockFd, &numPages);

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
    RestoreMemory(numPages, sockFd);
  }

  return 0;
}

void
ReadPagesContext(int sockFd, int* numPages)
{
  int i, ret;
  short events;
  size_t tempSize;

  struct memorySection* temp;
  void *buf, *canCheckpointAddr;

  int fd = open(CHECKPOINT_PATH, O_RDWR | O_CREAT, S_IRWXU);

  ret = ReadUsingPoll(sockFd, -1, (void*)numPages, sizeof(int));
  if (ret == -1) {
    ShowError("", errno);
  }

  printf("%d\n", *numPages);

  for (i = 0; i < *numPages; i++) {
    temp = malloc(sizeof(struct memorySection));

    ret = ReadUsingPoll(sockFd, -1, (void*)temp, sizeof(struct memorySection));
    if (ret == -1) {
      ShowError("", errno);
    }

    ret = write(fd, (void*)temp, sizeof(struct memorySection));
    if (ret == -1) {
      ShowError("", errno);
    }

    if (temp->permissions[0] == 'r' && temp->permissions[1] != 'w') {
      tempSize = temp->end - temp->start;
      buf = malloc(tempSize);

      ret = ReadUsingPoll(sockFd, -1, buf, tempSize);
      if (ret == -1) {
        ShowError("", errno);
      }

      ret = write(fd, buf, tempSize);
      if (ret == -1) {
        ShowError("", errno);
      }
    }
  }

  ret = ReadUsingPoll(sockFd, -1, (void*)&context, sizeof(ucontext_t));
  if (ret == -1) {
    ShowError("", errno);
  }

  close(fd);
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
    if (strstr(line, "stack")) {
      stackFound = 1;
      break;
    }
  }

  close(fileReadFd);
  return stackFound ? mem : NULL;
}

void
RestoreMemory(int numPages, int sockFd)
{
  int i, numSections;
  struct memorySection mem, *sections;

  void *mapped, *canCheckpointAddr;

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

  numSections = 0;
  sections = malloc(sizeof(struct memorySection) * numPages);

  for (i = 0; i < numPages; i++) {
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

    if (mem.permissions[0] == 'r' && mem.permissions[1] != 'w') {
      // reading address block and writing to appropriate address
      ret = read(fd, mapped, mem.end - mem.start);
      if (ret == -1) {
        ShowError("", errno);
      }
    } else {
      // copy to register for userfaultfd
      copyMemorySection(sections + numSections, &mem);
      numSections++;
    }
  }

  close(fd);

  makeUserfault(sections, numSections, sockFd);
  // *((int*)canCheckpointAddr) = 0;
  setcontext(&context);
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