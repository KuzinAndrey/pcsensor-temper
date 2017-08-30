all:	pcsensor

CC = gcc
CFLAGS = -g -O2 -Wall -lusb-1.0 -I/usr/include/libusb-1.0

pcsensor:	pcsensor.c
	${CC} ${CFLAGS} -DUNIT_TEST -o $@ $^

clean:		
	rm -f pcsensor *.o

rules-install:			# must be superuser to do this
	cp 99-tempsensor.rules /etc/udev/rules.d
