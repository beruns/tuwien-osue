CC=gcc
CFLAGS=-std=c99 -pedantic -Wall -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -g
FILES=mydiff.c
PGNAME=mydiff

all: $(PGNAME)

$(PGNAME): $(FILES)
	$(CC) $(CFLAGS) -o $(PGNAME) $(FILES)

clean:
	rm -f $(PGNAME)
 
.PHONY: clean all
