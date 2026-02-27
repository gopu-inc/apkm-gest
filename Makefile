CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LDFLAGS = -lsqlite3 -lcurl

# Objets communs nécessaires pour la sécurité/auth
COMMON_OBJS = src/auth.o

all: apkm bool apsm

apkm: src/main.o src/db.o src/parser.o src/sandbox.o $(COMMON_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

bool: src/bool.o
	$(CC) -o $@ $^

# ICI : On ajoute COMMON_OBJS (donc auth.o) pour que apsm trouve get_gopu_token
apsm: src/apsm.o $(COMMON_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o apkm bool apsm

