#
# Makefile for midi-utils
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
# Makefile for for midi-utils.
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
	$(RM) *~ contrib/*~

install:
	@mkdir -p $(CONFDIR)
	@cp -v src/midi2midi $(BINDIR)/.
	@cp -v contrib/*.m2m $(CONFDIR)/.

uninstall:
	$(RM) $(BINDIR)/note2note
	$(RM) $(BINDIR)/note2jacktransport
	$(RM) $(BINDIR)/midi2midi
	$(RM) -r  $(CONFDIR)

