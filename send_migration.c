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
#include <netinet/in.h>
#include <errno.h>
#include <sys/socket.h>
#include "migration_header.h"

#define PORT 5000

void
signalHandler(int signal);

void
savingCheckPointImage();

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

  // variable that indicate wheather it should start migration
  int migrated = 0;
  int sock = buildConnection();
  if(sock == -1) printf("Sending : connection failed to build");

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

  //send readonly file
  //sendReadOnly(sock);

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

    //send readonly file
    sendReadOnly(sock);

    //send other memory page on demand

  } else {
    printf("Program is restored\n");

    ////receive  rest of files;
    ///////
  }
}

/////*****************************************************************/////
////////////////////////// helper functions //////////////////////////////


int buildConnection(){
  struct sockaddr_in address;
  int sock = 0, valread;
  struct sockaddr_in serv_addr;

  char buffer[1024] = { 0 };
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("\n Socket creation error \n");
    return -1;
  }

  memset(&serv_addr, '0', sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);

  if (inet_pton(AF_INET, "10.110.176.160", &serv_addr.sin_addr) <= 0) {
    printf("\nInvalid address/ Address not supported \n");
    return -1;
  }

  if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    printf("\nConnection Failed \n");
    return -1;
  }

  return sock;
}

void sendReadOnly(int sock){
  // sending
  struct stat file_stat;
  int fd = open("readonly", O_RDONLY);
  while (fd == -1) {
    fd = open("readonly", O_RDONLY);
  }
  // Get file stats
  if (fstat(fd, &file_stat) < 0) {
    fprintf(stderr, "Error fstat --> %s", strerror(errno));

  }

  int offset = 0;
  int remain_data = file_stat.st_size;
  ssize_t sent_bytes;
  /* Sending file data */
  while (((sent_bytes = sendfile(sock, fd, &offset, BUFSIZ)) > 0) &&
         (remain_data > 0)) {
    remain_data -= sent_bytes;
  }

}
