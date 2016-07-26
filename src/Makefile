#!/usr/bin/env make
#
# Copyright (C)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# About
# -----
#
# Author: AiO <aio at aio dot nu>
#
# Makefile for src-folder for midi-utils.
#

ifneq (${USE_JACK},)
  JACKFLAGS:=`pkg-config --cflags --libs jack`
endif
ALSAFLAGS:=`pkg-config --cflags --libs alsa`
CFLAGS=-pedantic -Wall -std=c99 -g -lm

SRCS=quit.c error.c debug.c sequencer.c midi2midi.c
ifneq (${USE_JACK},)
  SRCS=${SRCS} jack_transport.c
endif
OBJS=$(SRCS:.c=.o)

all: .depend midi2midi

%.o: %.c Makefile
	$(CC) -o $@ -c $< $(CFLAGS)

midi2midi: $(OBJS)
	$(CC) -o $@ $(OBJS) $(CFLAGS) $(JACKFLAGS) $(ALSAFLAGS)

.depend:
	$(CC) -MM $(SRCS) > .depend

clean:
	$(RM) *~ midi2midi $(OBJS) .depend
