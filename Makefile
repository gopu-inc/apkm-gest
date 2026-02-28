# Makefile pour APKM
CC = gcc
CFLAGS = -m32 -Iinclude -I/usr/local/include -Wall -Wextra -O2
LDFLAGS = -m32 -L/usr/local/lib
# Bibliothèques de base disponibles sur Alpine
LIBS = -lcurl -lsqlite3 -lssl -lcrypto -lz -lpthread -lrt -ldl -lm
LIBS += -lsodium -larchive -llz4 -lzstd -lyaml -ljansson -largon2
LIBS += -lblake3  # Ajout de BLAKE3

SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Exclure les fichiers avec main() et les fichiers problématiques
EXCLUDE = apsm.c bool.c monitoring.c parallel.c
SRCS = $(filter-out $(addprefix $(SRCDIR)/,$(EXCLUDE)), $(wildcard $(SRCDIR)/*.c))
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
TARGET = $(BINDIR)/apkm

$(shell mkdir -p $(OBJDIR) $(BINDIR))

.PHONY: all clean tools

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "🔗 Linkage de $@..."
	@$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "✅ APKM compilé avec succès !"
	@ls -lh $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "🛠️  Compilation: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compiler les outils séparément
tools:
	@echo "🔧 Compilation des outils..."
	$(CC) $(CFLAGS) -o $(BINDIR)/apsm src/security.c src/auth.c src/apsm.c $(LIBS)
	$(CC) $(CFLAGS) -o $(BINDIR)/bool src/bool.c $(LIBS)
	@echo "✅ Outils compilés dans $(BINDIR)/"

clean:
	@rm -f $(OBJDIR)/*.o $(TARGET) $(BINDIR)/apsm $(BINDIR)/bool
	@echo "🧹 Nettoyage terminé"
# En haut du Makefile, après les définitions
LIB_NAME = apkm
LIB_VERSION = 2.0.0
LIB_SO = $(LIBDIR)/lib$(LIB_NAME).so.$(LIB_VERSION)
LIB_A = $(LIBDIR)/lib$(LIB_NAME).a

# Ajouter les cibles pour les librairies
libs: $(LIB_SO) $(LIB_A)

$(LIB_SO): $(OBJS)
	@echo "🔧 Creating shared library..."
	@$(CC) -shared $(LDFLAGS) -Wl,-soname,lib$(LIB_NAME).so.$(LIB_VERSION) -o $@ $^ $(LIBS)
	@ln -sf lib$(LIB_NAME).so.$(LIB_VERSION) $(LIBDIR)/lib$(LIB_NAME).so
	@echo "✅ Shared library created: $@"

$(LIB_A): $(OBJS)
	@echo "🔧 Creating static library..."
	@$(AR) rcs $@ $^
	@$(RANLIB) $@
	@echo "✅ Static library created: $@"

# Modifier la cible all pour inclure les librairies
all: $(TARGET) libs

# Ajouter une cible pour installer les librairies
install-libs:
	@mkdir -p $(DESTDIR)$(PREFIX)/lib
	@mkdir -p $(DESTDIR)$(PREFIX)/include/$(LIB_NAME)
	@cp $(LIB_SO) $(DESTDIR)$(PREFIX)/lib/
	@cp $(LIB_A) $(DESTDIR)$(PREFIX)/lib/
	@cp -r include/* $(DESTDIR)$(PREFIX)/include/$(LIB_NAME)/
	@echo "✅ Libraries installed in $(DESTDIR)$(PREFIX)/lib/"
# Installer les dépendances
deps:
	@echo "📦 Installation des dépendances..."
	sudo apk add --no-cache \
		build-base \
		gcc \
		musl-dev \
		linux-headers \
		curl-dev \
		sqlite-dev \
		openssl-dev \
		libarchive-dev \
		lz4-dev \
		zstd-dev \
		argon2-dev \
		libsodium-dev \
		yaml-dev \
		jansson-dev \
		libseccomp-dev \
		libcap-ng-dev

# Afficher les fichiers compilés
list:
	@echo "Fichiers compilés :"
	@for f in $(SRCS); do echo "  $$f"; done

