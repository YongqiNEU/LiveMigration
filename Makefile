
CC=gcc
CFLAGS=-g -Wall -fPIC -O0 -fno-stack-check -fno-stack-protector -fPIC -D_FORTIFY_SOURCES= -z execstack

%.o: %.c
	${CC} ${CFLAGS} $< -c -o $@

sender: send_migration.o send_migration_helper.o util.o

receiver: receive_migration.o wait_for_migration.o util.o
	${CC} ${CFLAGS} -static \
	-Wl,-Ttext-segment=5000000 -Wl,-Tdata=5100000 -Wl,-Tbss=5200000 \
	-o $@ $^

# set IPADDR and PORTNO variables for this call
start_receiver: receiver
	./receiver ${IPADDR} ${PORTNO}

clean:
	rm -f *.o receiver