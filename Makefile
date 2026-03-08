CC = gcc
CFLAGS = -m32 -Iinclude -Wall -O2
LDFLAGS = -m32
LIBS = -lcurl -lssl -lcrypto -lpthread -lm -ljson-c

SRCDIR = src
BINDIR = bin

.PHONY: all clean

all: $(BINDIR)/apkm $(BINDIR)/apsm $(BINDIR)/bool
	@echo "✅ Build complete"

$(BINDIR)/apkm: $(SRCDIR)/main.c $(filter-out $(SRCDIR)/aps/apsm.c $(SRCDIR)/bools/bool.c, $(wildcard $(SRCDIR)/*.c))
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

$(BINDIR)/apsm: $(SRCDIR)/aps/apsm.c $(SRCDIR)/aps/auth.c $(SRCDIR)/aps/security.c
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

$(BINDIR)/bool: $(SRCDIR)/bools/bool.c
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(BINDIR)
