#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>

#include <sys/socket.h>
#include "migration_header.h"
#define PORT 5000

int main(int argc, char const *argv[]) {
  struct sockaddr_in address;
  int sock = 0, valread;
  struct sockaddr_in serv_addr;

  char buffer[1024] = {0};
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    printf("\n Socket creation error \n");
    return -1;
  }

  memset(&serv_addr, '0', sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  
  if(inet_pton(AF_INET, "10.110.176.160", &serv_addr.sin_addr)<=0) {
    printf("\nInvalid address/ Address not supported \n");
    return -1;
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
    printf("\nConnection Failed \n");
    return -1;
  }


  // sending 
  struct stat file_stat;
  int fd = open("readonly", O_RDONLY);
  while (fd == -1){
    fd = open("readonly", O_RDONLY);
  }
  //Get file stats
  if (fstat(fd, &file_stat) < 0){
      fprintf(stderr, "Error fstat --> %s", strerror(errno));
  }

  int offset = 0;
  int remain_data = file_stat.st_size;
  /* Sending file data */
  while (((sent_bytes = sendfile(sock,fd , &offset, BUFSIZ)) > 0) && (remain_data > 0))
  {
      remain_data -= sent_bytes;
  }

  return 0;
}
