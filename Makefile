
CC=gcc
CFLAGS=-g -Wall -fPIC -O0

%.o: %.c
	${CC} ${CFLAGS} $< -c -o $@

sender: send_migration.o

receiver: receive_migration.o wait_for_migration.o util.o

clean:
	rm -f *.o