#
# Makefile for system-register-tools
#

CC       = gcc -Wall
CFLAGS   = -g -O0 -fomit-frame-pointer -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -I ./
LDFLAGS  =
BIN	= rdasr wrasr
sbindir = /usr/sbin

RSRC = rdasr.c
RSRC += aarch64_rdasr.c
WSRC = wrasr.c
WSRC += aarch64_wrasr.c

.PHONY: all install clean

all:
	$(CC) $(CFLAGS) $(RSRC) -o rdasr
	$(CC) $(CFLAGS) $(WSRC) -o wrasr

install:
	cp registersv8.table ~/.regstable
	install -m 755 $(BIN) $(sbindir)
	@echo "system-register-tools have been installed successfully!"

clean:
	rm -f *.o $(BIN)
