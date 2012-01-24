#!/usr/bin/env make
#
# Makefile for src-folder for midi-utils
#

CFLAGS=-pedantic -Wall -std=c99 -g

all: note2note

note2note: quit.c note2note.c
	gcc -o $@ note2note.c quit.c -lasound $(CFLAGS)

clean:
	rm -rf *~ note2note
