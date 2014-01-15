CC=gcc
CFLAGS=-std=c99 -pedantic -Wall -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -g
LIBS=-lsem182

READIN=readin
CHSTAT=chstat

all: readin chstat

$(READIN): $(CHSTAT).c
	$(CC) $(CFLAGS) -D_BUILD_READIN $< -o $@ $(LFLAGS) $(LIBS)

$(CHSTAT): $(CHSTAT).c
	$(CC) $(CFLAGS) $< -o $@ $(LFLAGS) $(LIBS)

clean:
	rm -f $(READIN) $(CHSTAT)

.PHONY: clean all
