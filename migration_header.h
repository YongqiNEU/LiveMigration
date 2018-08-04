
#ifndef HEADER_FILE
#define HEADER_FILE

typedef unsigned long int VA;

struct memorySection
{
  char* start;
  char* end;
  char permissions[4];
  unsigned int offset;
};

int
WaitForMigration(in_addr_t, in_port_t, struct sockaddr*, socklen_t*);

void
ShowError(char*, int);

void
parseSectionHeader(char*, struct memorySection*);

int
readLine(int, char);

char*
getNameFromSectionLine(char*);

void
copyMemorySection(struct memorySection*, struct memorySection*);

void
writeToImage(int, struct memorySection*);

#endif