#
# Makefile for midi-utils
#
# This file will recursively build all tools and provide installation means
#

PREFIX=
BINDIR=$(PREFIX)/usr/bin
CONFDIR=$(PREFIX)/etc/midi-utils

all:
	@cd src && make
	@cd ..

clean:
	@cd src && make clean
	@cd ..
	rm *~

install:
	@mkdir -p $CONFDIR
	@cp -v src/note2note src/note2jacktransport $(BINDIR)/.
	@cp -v contrib/*.n2n $(CONFDIR)/.

uninstall:
	rm -rf $(BINDIR)/note2note $(BINDIR)/note2jacktransport $(CONFDIR)

