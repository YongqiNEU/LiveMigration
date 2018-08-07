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
#include <poll.h>
#include <signal.h>
#include <sys/sendfile.h>

#include <sys/types.h>
#include <linux/userfaultfd.h>
#define PORT 5000

void
signalHandler(int signal);
void
savingCheckPointImage();
void
writeMemoryStructureToImage(int fd, struct memorySection* section);
void
sendReadOnly(int sock);
void
sendMemorySection(int sock, char* startAddress);
int
buildConnection();
struct memorySection*
findMemorySection(char* start, struct memorySection* section);
void
sendingPagesOndemand(int sock, struct memorySection* listofsections);

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
  //int pid = getpid();  // not
  char line[256];
  
  // variable that indicate wheather it should start migration
  int migrated = 1;
  struct memorySection* listofsections;

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
         struct memorySection* templist = listofsections;
         listofsections = &section;
         listofsections->next = templist;
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
  if (migrated == 1) {
    // read checkpoint image file
    checkpoint_image_fd = open("readonly", O_RDWR);
    lseek(checkpoint_image_fd, 0, SEEK_END);

    int context_ret = write(checkpoint_image_fd, &context, sizeof(context));
    if (context_ret != sizeof(context)) {
      printf("context failed to write\n");
    }

    close(checkpoint_image_fd);

    // send readonly file
    sendReadOnly(sock); // include non-read-only pages addresses.

    // send other memory page on demand
    sendingPagesOndemand(sock, listofsections);

    printf("sending process ended");
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

  int sockfd = 0; //, n = 0; not used
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

  //int offset = 0; not used
  int remain_data = file_stat.st_size;
  ssize_t sent_bytes;
  /* Sending file data */
  while (((sent_bytes = sendfile(sock, fd, NULL, BUFSIZ)) > 0) &&
         (remain_data > 0)) {
    remain_data -= sent_bytes;
  }
}

void
sendMemorySection(int sock, char* startAddress)
{
  // sending
  struct stat file_stat;
  int fd = open(startAddress, O_RDONLY);

  // Get file stats
  if (fstat(fd, &file_stat) < 0) {
    fprintf(stderr, "Error fstat --> %s", strerror(errno));
  }

  //int offset = 0; not used
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

struct memorySection*
findMemorySection(char* start, struct memorySection* section)
{
  struct memorySection* prev = NULL;
  struct memorySection* ret = NULL;
 
  while(section != NULL && strcmp(section->start, start) != 0){
 	prev = section;
 	section = section->next;
  }  
  
  ret = section;
  if(prev != NULL && ret != NULL) prev->next = ret->next;

  return ret;
}


void
sendingPagesOndemand(int sock, struct memorySection* listofsections)
{

    struct pollfd pollfd;
    pollfd.fd = sock;
    pollfd.events = POLLIN;
    
    while(listofsections != NULL){
        if(poll(&pollfd, 1, -1) <= 0) continue;

	char* startAddress; // data read from userfault fd   
        struct memorySection* sendingSection;
	ssize_t nread = read(pollfd.fd, &startAddress, 1024); 
	if(nread <= 0){
		printf("nread faliure");
		exit(EXIT_FAILURE);
	}

  	if(strcmp(listofsections->start, startAddress) == 0){
		sendingSection = listofsections;
 		listofsections = listofsections->next;
	} 
  	else{
		sendingSection = findMemorySection(startAddress, listofsections);
	}
	
	int section_image_fd = open(startAddress, O_CREAT | O_RDWR, S_IRWXU);
	writeToImage(section_image_fd, sendingSection);
	close(section_image_fd);
	sendMemorySection(sock, startAddress);
   }
}

