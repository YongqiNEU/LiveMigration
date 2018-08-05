
CC=gcc
CFLAGS=-g -Wall -fPIC -O0 -fno-stack-check -fno-stack-protector -fPIC -D_FORTIFY_SOURCES= -z execstack

%.o: %.c
	${CC} ${CFLAGS} $< -c -o $@

sender.so: send_migration.o util.o
	${CC} ${CFLAGS} -shared -o $@ $^

receiver: receive_migration.o wait_for_migration.o util.o
	${CC} ${CFLAGS} -static \
	-Wl,-Ttext-segment=5000000 -Wl,-Tdata=5100000 -Wl,-Tbss=5200000 \
	-o $@ $^


# set IPADDR and PORTNO variables for this call
start_receiver: receiver
	./receiver ${IPADDR} ${PORTNO}

start_send: sender.so hello
	  (sleep 3 && kill -12 `pgrep -n hello`) &
	  LD_PRELOAD=`pwd`/sender.so ./hello

clean:
	rm -f *.o receiver sender.so send_migration.o hello hello.o readonly process_checkpoint.ckpt
