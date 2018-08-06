#include "migration_header.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
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
  if (sock == -1)
    printf("Sending : connection failed to build/n/n/n/n");
  // printf("Sending : connection failed to build/n/n/n/n");
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
    if (section.permissions[0] == 'r'&&
        strstr(line, "vvar") == NULL && strstr(line, "vdso") == NULL &&
        strstr(line, "vsyscall") == NULL) {
      
      if(section.permissions[1] != 'w'){
      	 writeToImage(checkpoint_image_fd, &section);
      }
      else{
    	 writeMemoryStructureToImage(checkpoint_image_fd,&section);
      }
      counter++;
    }
    
  }
  // save counter
  lseek(checkpoint_image_fd, 0, SEEK_SET);
  write(checkpoint_image_fd, &counter, sizeof(int));

  close(memory_layout_fd);
  close(checkpoint_image_fd);

  getcontext(&context);

  // send readonly file
  // sendReadOnly(sock);

  // save context
  if (pid == getpid()) {
    // read checkpoint image file
    checkpoint_image_fd = open("readonly", O_RDWR);
    lseek(checkpoint_image_fd, 0, SEEK_END);

    int context_ret = write(checkpoint_image_fd, &context, sizeof(context));
    if (context_ret != sizeof(context)) {
      printf("context failed to write\n");
    }

    close(checkpoint_image_fd);

    // send readonly file
    sendReadOnly(sock);

    // send other memory page on demand

  } else {
    printf("Program is restored\n");

    ////receive  rest of files;
    ///////
  }
}

/////*****************************************************************/////
////////////////////////// helper functions //////////////////////////////

int
buildConnection()
{

  int sockfd = 0, n = 0;
  char recvBuff[1024];
  struct sockaddr_in serv_addr;

  memset(recvBuff, '0', sizeof(recvBuff));
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("\n Error : Could not create socket \n");
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(5000);
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    printf("\n Error : Connect Failed \n");
    return -1;
  }

  return sockfd;
}

void
sendReadOnly(int sock)
{
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
  while (((sent_bytes = sendfile(sock, fd, NULL, BUFSIZ)) > 0) &&
         (remain_data > 0)) {
    remain_data -= sent_bytes;
  }
}

// write non read only memory sections' structure to destination fd;
void
writeMemoryStructureToImage(int fd, struct memorySection* section)
{
  ssize_t ret = write(fd, section, sizeof(struct memorySection));
  if (ret != sizeof(struct memorySection)) {
    printf("Section write failed \n");
  }
}

