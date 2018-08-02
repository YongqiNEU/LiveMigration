
#ifndef HEADER_FILE
#define HEADER_FILE

int
WaitForMigration(in_addr_t, in_port_t, struct sockaddr*, socklen_t*);

void
ShowError(char*, int);

struct memorySection
{
  char* start;
  char* end;
  char permissions[4];
  unsigned int offset;
};

#endif