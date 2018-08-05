#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

int BACKLOG = 20;

int
WaitForMigration(in_addr_t ipAddr,
                 in_port_t portNo,
                 struct sockaddr* clientAddr,
                 socklen_t* clientAddrLen)
{
  int sockFd, newSockFd, ret;
  struct sockaddr_in serverAddr;

  sockFd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockFd == -1) {
    return -1;
  }

  memset(&serverAddr, '0', sizeof(struct sockaddr_in));

  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(5000);
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

  ret = bind(sockFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
  if (ret == -1) {
    return -1;
  }

  ret = listen(sockFd, BACKLOG);
  if (ret == -1) {
    return -1;
  }

  newSockFd = accept(sockFd, clientAddr, clientAddrLen);
  return newSockFd;
}