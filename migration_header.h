
#ifndef HEADER_FILE
#define HEADER_FILE

#include <arpa/inet.h>
#include <linux/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>

typedef __u64 VA;

struct memorySection
{
  char* start;
  char* end;
  char permissions[4];
  unsigned int offset;
  struct memorySection* next;
};

int
WaitForMigration(in_addr_t, in_port_t, struct sockaddr*, socklen_t*);

void
ShowError(char*, int);

void
parseSectionHeader(char*, struct memorySection*);

int
readLine(int, char*);

char*
getNameFromSectionLine(char*);

void
copyMemorySection(struct memorySection*, struct memorySection*);

void
writeToImage(int, struct memorySection*);

void
makeUserfault(struct memorySection*, int, int);

int
ReadUsingPoll(int, int, void*, int);

void
ReadHex(char*, VA*);

#endif