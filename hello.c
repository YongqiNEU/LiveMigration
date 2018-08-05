#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
	int n = 1;

  	while (1)
  	{
        printf("%d.", n++);
        fflush(stdout);
        sleep(1);
  	}
  	return 0;
}


