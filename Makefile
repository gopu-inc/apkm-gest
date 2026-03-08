CC = gcc
CFLAGS = -m32 -Iinclude -Wall -O2
LDFLAGS = -m32
LIBS = -lcurl -lssl -lcrypto -lpthread -lm -ljson-c

SRCDIR = src
BINDIR = bin

.PHONY: all clean

all: $(BINDIR)/apkm $(BINDIR)/apsm
	@echo "✅ Build complete"

$(BINDIR)/apkm: $(SRCDIR)/main.c
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

$(BINDIR)/apsm: $(SRCDIR)/apsm.c $(SRCDIR)/auth.c $(SRCDIR)/security.c
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(BINDIR)
