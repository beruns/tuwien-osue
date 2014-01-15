CC=gcc
#CFLAGS=-std=c99 -pedantic -Wall -DENDEBUG -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -g
CFLAGS=-std=c99 -pedantic -Wall -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -g

SERVER=server
CLIENT=client

CFILES=server.c client.c
OBJ=$(CFILES:.c=.o)

all: server client

$(SERVER): $(SERVER).o
	$(CC) $< -o $@

$(CLIENT): $(CLIENT).o
	$(CC) $< -o$@

%.o: %.c
	$(CC) $(CFLAGS) -o $*.o -c $*.c

clean:
	rm -f $(SERVER) $(CLIENT) $(OBJ)

.PHONY: clean all
