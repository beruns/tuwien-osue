#
# @file Makefile
# @brief makefile for websh 
# @author Georg Hubinger 9947673 <georg.hubinger@tuwien.ac.at>
# @date 2013-11-18
#
CC=gcc
CFLAGS=-std=c99 -pedantic -Wall -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -g

#name of executable
EXEC=websh

# .c files
CFILES=websh.c fork_function.c 

# required header file
HFILES=fork_function.h
OFILES=$(CFILES:.c=.o)

all: $(EXEC)

$(EXEC): $(OFILES)
	$(CC) $(OFILES) -o $@

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -o $*.o -c $*.c

clean:
	rm -f $(EXEC) $(OFILES)

.PHONY: clean all
