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
	$(RM) *~

install:
	@mkdir -p $CONFDIR
	@cp -v src/midi2midi $(BINDIR)/.
	@cp -v contrib/*.m2m $(CONFDIR)/.

uninstall:
	$(RM) -r $(BINDIR)/note2note $(BINDIR)/note2jacktransport $(BINDIR)/midi2midi $(CONFDIR)

