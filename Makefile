# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#  APKM Ecosystem Makefile - Gopu.inc Proprietary
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -D_GNU_SOURCE
LDFLAGS = -lsqlite3 -lcurl

# Dossiers
SRC_DIR = src
OBJ_DIR = obj

# Objets communs (Sécurité, Auth, Database)
COMMON_OBJS = $(OBJ_DIR)/auth.o \
              $(OBJ_DIR)/db.o \
              $(OBJ_DIR)/parser.o \
              $(OBJ_DIR)/sandbox.o \
              $(OBJ_DIR)/resolver.o \
              $(OBJ_DIR)/download.o

# Cibles finales
all: apkm bool apsm

# 1. APKM : Le gestionnaire principal (Installateur / Débooleur)
apkm: $(OBJ_DIR)/main.o $(COMMON_OBJS)
	@echo "🔗 Liage de APKM..."
	$(CC) -o $@ $^ $(LDFLAGS)

# 2. BOOL : Le builder de paquets (Créateur de .tar.bool)
bool: $(OBJ_DIR)/bool.o
	@echo "🔗 Liage de BOOL..."
	$(CC) -o $@ $^

# 3. APSM : Le Storage Manager (Auth / Push / Registry)
apsm: $(OBJ_DIR)/apsm.o $(OBJ_DIR)/auth.o
	@echo "🔗 Liage de APSM..."
	$(CC) -o $@ $^ $(LDFLAGS)

# Règle de compilation pour tous les fichiers .c
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "🔨 Compilation de $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Nettoyage du projet
clean:
	@echo "🧹 Nettoyage des objets et binaires..."
	rm -f $(OBJ_DIR)/*.o apkm bool apsm
	rm -rf build/

# Initialisation de l'environnement
setup:
	@mkdir -p build
	@echo "📂 Dossier build prêt."

.PHONY: all clean setup
