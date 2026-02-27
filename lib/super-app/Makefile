CC = gcc
CFLAGS = -O2 -Wall
PREFIX = /usr

all: super-app

super-app: src/super-app.c
	$(CC) $(CFLAGS) -o super-app src/super-app.c

install: super-app
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp super-app $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/super-app

clean:
	rm -f super-app

test: super-app
	./super-app --version

.PHONY: all install clean test

