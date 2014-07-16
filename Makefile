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
.phony: all clean
#
SRC=fauxcon.c
EXEC=fauxcon
#
all: $(EXEC)

$(EXEC): $(SRC)
	$(CC) $(CSTDCFLAGS) $(CFLAGS) $(EXTFLAGS) $(LDFLAGS) -c $< -o $@.o $(LIBS)
	$(CC) $(CSTDCFLAGS) $(CFLAGS) $(EXTFLAGS) $(LDFLAGS) $@.o -o $@ $(LIBS)
	#sudo chown root:root $(EXEC)
	#sudo chmod 4755 $(EXEC)

clean:
	rm -f *.o $(EXEC)
