CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS = -lcap -lseccomp

SRCS = anv.c
OBJS = $(SRCS:.c=.o)
TARGET = anv

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c anv.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/
	chmod 755 /usr/local/bin/$(TARGET)
	mkdir -p /usr/local/share/man/man1
	cp anv.1 /usr/local/share/man/man1/

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	rm -f /usr/local/share/man/man1/anv.1

test: $(TARGET)
	./$(TARGET) list
