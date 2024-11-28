CC=gcc
CFLAGS=-Wall -g
LDFLAGS=-lpthread

all: wgetX

wgetX: wgetX.o url.o
	$(CC) -o wgetX wgetX.o url.o $(LDFLAGS)

wgetX.o: wgetX.c wgetX.h url.h
	$(CC) $(CFLAGS) -c wgetX.c

url.o: url.c url.h
	$(CC) $(CFLAGS) -c url.c

clean:
	rm -f *.o wgetX