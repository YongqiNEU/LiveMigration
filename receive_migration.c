
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "migration_header.h"

int
main(int argc, char* argv[])
{
  int sockFd, clientAddrLen;
  struct sockaddr_in clientAddr;
  in_addr_t ipAddr;
  in_port_t portNo;

  if (argc < 3) {
    fprintf(stderr, "IP Address and Port number required \n");
    exit(1);
  }

  ipAddr = inet_addr(argv[1]);
  portNo = atoi(argv[2]);

  sockFd = WaitForMigration(
    ipAddr, portNo, (struct sockaddr*)&clientAddr, &clientAddrLen);
}