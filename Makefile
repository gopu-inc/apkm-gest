# Makefile simple pour APKM
CC = gcc
CFLAGS = -m32 -Iinclude -Wall -O2 -D_GNU_SOURCE
LDFLAGS = -m32
LIBS = -lcurl -lsqlite3 -lssl -lcrypto -lz -lpthread -lrt -ldl -lm
LIBS += -lsodium -larchive -llz4 -lzstd -lyaml -ljansson -largon2
LIBS += -lblake3 -lseccomp -lcap-ng -ljson-c

SRCDIR = src
BINDIR = bin

# Sources communes
COMMON_SRCS = $(SRCDIR)/auth.c \
              $(SRCDIR)/core.c \
              $(SRCDIR)/crypto.c \
              $(SRCDIR)/db.c \
              $(SRCDIR)/download.c \
              $(SRCDIR)/parser.c \
              $(SRCDIR)/resolver.c \
              $(SRCDIR)/sandbox.c \
              $(SRCDIR)/security.c \
              $(SRCDIR)/zarch.c

.PHONY: all clean

all: $(BINDIR)/apkm $(BINDIR)/apsm $(BINDIR)/bool
	@echo "✅ Build complete"
	@ls -la $(BINDIR)/

$(BINDIR)/apkm: $(SRCDIR)/main.c $(COMMON_SRCS)
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

$(BINDIR)/apsm: $(SRCDIR)/apsm.c $(SRCDIR)/auth.c $(SRCDIR)/security.c $(SRCDIR)/zarch.c
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

$(BINDIR)/bool: $(SRCDIR)/bool.c
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(BINDIR)

install: all
	cp $(BINDIR)/* /usr/local/bin/

.PHONY: all clean install
