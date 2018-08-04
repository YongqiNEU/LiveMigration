#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#include "migration_header.h"

void
signalHandler(int signal);

void
savingCheckPointImage();

void
writeToImage(int fd, struct memorySection* section);

/////*****************************************************************/////

// constructor SIGUSR2 signal and signal handler
// this constructor can be called before main()
__attribute__((constructor)) void
myconstructor()
{
  signal(SIGUSR2, signalHandler);
}

// signal handler for signal SIGUSR2
// it will initiate saving a check point image file
void
signalHandler(int signal)
{
  if (signal == SIGUSR2) {
    savingCheckPointImage();
  }
}

/////*****************************************************************/////

// saving a checkpoint image file
void
savingCheckPointImage()
{
  int pid = getpid();
  char line[256];

  // context for saving and restoring registers
  ucontext_t context;

  // read memory layout
  int memory_layout_fd = open("/proc/self/maps", O_RDONLY);
  // create a new image file for saving the checkpoint
  int checkpoint_image_fd = open("readonly", O_CREAT | O_RDWR, S_IRWXU);

  // number of memory sections
  int counter = 0;
  write(checkpoint_image_fd, &counter, sizeof(int));

  // parse memory sections
  struct memorySection section;
  while (readLine(memory_layout_fd, line) > 0) {
    parseSectionHeader(line, &section);
    // skip vsyscall, vvar and vdso lines
    if (section.permissions[0] == 'r' && section.permissions[1] != 'w' &&
        strstr(line, "vvar") == NULL && strstr(line, "vdso") == NULL &&
        strstr(line, "vsyscall") == NULL) {

      writeToImage(checkpoint_image_fd, &section);

      counter++;
    }
  }
  // save counter
  lseek(checkpoint_image_fd, 0, SEEK_SET);
  write(checkpoint_image_fd, &counter, sizeof(int));

  close(memory_layout_fd);
  close(checkpoint_image_fd);

  getcontext(&context);
  // save context
  if (pid == getpid()) {
    // read checkpoint image file
    checkpoint_image_fd = open("myckpt", O_RDWR);
    lseek(checkpoint_image_fd, 0, SEEK_END);

    int context_ret = write(checkpoint_image_fd, &context, sizeof(context));
    if (context_ret != sizeof(context)) {
      printf("context failed to write\n");
    }

    close(checkpoint_image_fd);
  } else {
    printf("Program is restored\n");

    ////to implement userfaultfd
    ///////
  }
}

/////*****************************************************************/////
////////////////////////// helper functions //////////////////////////////

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
