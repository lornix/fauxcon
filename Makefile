#
# Generic compiles-it-all Makefile
#  Loni Nix <lornix@lornix.com>
#    Last modified frequently
#
# adhere to a higher standard
CSTDCFLAGS=-std=gnu99
#
# save any options preset on cmd line
EXTFLAGS:=$(CFLAGS)
CFLAGS=
#
# lots of debugging stuff included
CFLAGS+=-ggdb3
#
# but no optimization by default
CFLAGS+=-O0
#
# warn about lots of things
CFLAGS+=-Wall -Wextra -Wunused
#
# but only include libs as needed
# CFLAGS+=-Wl,--as-needed
#
# sometimes we want to see it all
#CFLAGS+=-save-temps -fverbose-asm
#
# build VERSION string
VERSION:=$(shell git describe --tags --long)
CFLAGS+=-D'VERSION="$(VERSION)"'
#
# don't really use LDFLAGS much
#LDFLAGS+=-rdynamic
#
# but need to list libraries needed
#LIBS+=
#
CC:=gcc
#
.SUFFIXES:
.SUFFIXES: .c .o
#
SHELL=/bin/bash -x
export PS4=\e[32;40m[\e[35;40m$@\e[32;40m]\e[36;40m 
#
.phony: all clean
#
SRC=fauxcon.c
EXEC=fauxcon
#
all: $(EXEC)

$(EXEC): $(SRC)
	@$(CC) $(CSTDCFLAGS) $(CFLAGS) $(EXTFLAGS) $(LDFLAGS) -c $< -o $@.o $(LIBS)
	@$(CC) $(CSTDCFLAGS) $(CFLAGS) $(EXTFLAGS) $(LDFLAGS) $@.o -o $@ $(LIBS)
	@# uncomment for setuid mode
	@#sudo chown root:root $(EXEC)
	@#sudo chmod 4755 $(EXEC)

clean:
	@rm -f *.o $(EXEC)

# now you will be able to install fauxcon with make install
PREFIX = /usr/local

.fauxcon: install
install: fauxcon
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $< $(DESTDIR)$(PREFIX)/bin/fauxcon

.fauxcon: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/fauxcon
