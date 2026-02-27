# ============================================================================
# APKM - Advanced Package Manager
# Alpine Linux 32-bit Build System
# ============================================================================

# Configuration de base
NAME = apkm
VERSION = 2.0.0
CODENAME = "Quantum Leopard"

# Architecture (32-bit Alpine)
ARCH = x86
CC = gcc
AR = ar
RANLIB = ranlib
STRIP = strip

# Répertoires
SRCDIR = src
OBJDIR = obj
BINDIR = bin
INCDIR = include
LIBDIR = lib
DOCDIR = doc
CONFDIR = /etc/apkm
PKGCONFIGDIR = /usr/lib/pkgconfig

# Création des répertoires
DIRS = $(OBJDIR) $(BINDIR) $(LIBDIR) $(DOCDIR) \
       $(OBJDIR)/core $(OBJDIR)/crypto $(OBJDIR)/network \
       $(OBJDIR)/db $(OBJDIR)/sandbox $(OBJDIR)/utils

# Fichiers sources
SRCS = $(wildcard $(SRCDIR)/*.c) \
       $(wildcard $(SRCDIR)/core/*.c) \
       $(wildcard $(SRCDIR)/crypto/*.c) \
       $(wildcard $(SRCDIR)/network/*.c) \
       $(wildcard $(SRCDIR)/db/*.c) \
       $(wildcard $(SRCDIR)/sandbox/*.c) \
       $(wildcard $(SRCDIR)/utils/*.c)

# Fichiers objets (maintenir la structure)
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

# Binaires
TARGET = $(BINDIR)/$(NAME)
STATIC_LIB = $(LIBDIR)/lib$(NAME).a
SHARED_LIB = $(LIBDIR)/lib$(NAME).so.$(VERSION)

# Compilation flags pour Alpine 32-bit
CFLAGS = -m32 -march=i686 -mtune=generic
CFLAGS += -O3 -pipe -fomit-frame-pointer -funroll-loops
CFLAGS += -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion
CFLAGS += -Wstrict-prototypes -Wmissing-prototypes -Wredundant-decls
CFLAGS += -D_FORTIFY_SOURCE=2 -fstack-protector-strong
CFLAGS += -fPIC -fPIE -fvisibility=hidden
CFLAGS += -DAPKM_VERSION=\"$(VERSION)\" -DAPKM_CODENAME=\"$(CODENAME)\"
CFLAGS += -DARCH_X86 -DALPINE_LINUX
CFLAGS += -I$(INCDIR) -I/usr/include -I/usr/local/include

# Optimisations spécifiques 32-bit
CFLAGS += -msse2 -mfpmath=sse -ffast-math
CFLAGS += -falign-functions=16 -falign-loops=16
CFLAGS += -fweb -frename-registers -fivopts

# Debug flags
DEBUG_CFLAGS = -g3 -ggdb -DDEBUG -D_DEBUG
DEBUG_CFLAGS += -fsanitize=address -fsanitize=undefined
DEBUG_CFLAGS += -fno-omit-frame-pointer

# Linker flags pour 32-bit
LDFLAGS = -m32 -Wl,-O3 -Wl,--as-needed -Wl,--hash-style=gnu
LDFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
LDFLAGS += -L$(LIBDIR) -L/usr/lib -L/usr/local/lib
LDFLAGS += -Wl,-rpath,/usr/lib/$(NAME)

# Librairies Alpine Linux 32-bit
LIBS = -lcurl -lsqlite3 -lssl -lcrypto -lz
LIBS += -lpthread -lrt -ldl -lm
LIBS += -lsodium -lseccomp -lcap-ng
LIBS += -larchive -llz4 -lzstd -lb2
LIBS += -lyaml -ljansson -ltoml
LIBS += -lprom -lpromhttp -lmicrohttpd
LIBS += -lblake3 -largon2 -ltbb
LIBS += -lsystemd -lwebsockets

# Librairies statiques pour Alpine 32-bit
STATIC_LIBS = -Wl,-Bstatic -lseccomp -lcap-ng -lwebsockets
STATIC_LIBS += -Wl,-Bdynamic

# Flags de compatibilité Alpine
ALPINE_CFLAGS = -D_GNU_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64

# Outils
INSTALL = install -c
INSTALL_PROGRAM = $(INSTALL) -m 755
INSTALL_DATA = $(INSTALL) -m 644
INSTALL_DIR = $(INSTALL) -d

# Chemins d'installation Alpine
PREFIX ?= /usr
BINDIR_INSTALL = $(PREFIX)/bin
LIBDIR_INSTALL = $(PREFIX)/lib
INCDIR_INSTALL = $(PREFIX)/include/$(NAME)
DOCDIR_INSTALL = $(PREFIX)/share/doc/$(NAME)
MANDIR_INSTALL = $(PREFIX)/share/man/man1
SYSCONFDIR = /etc
SYSTEMDDIR = /usr/lib/systemd/system

# ============================================================================
# Règles principales
# ============================================================================

.PHONY: all clean distclean install uninstall
.PHONY: debug release static shared test benchmark
.PHONY: docs man pages check lint format
.PHONY: install-dev install-static install-shared
.PHONY: docker musl-stats size-report

all: release

# Création des répertoires
$(DIRS):
	@mkdir -p $@

# ============================================================================
# Compilation release (optimisée)
# ============================================================================

release: CFLAGS += $(ALPINE_CFLAGS)
release: $(DIRS) $(TARGET) $(STATIC_LIB) $(SHARED_LIB)
	@echo "✅ APKM v$(VERSION) compilé pour Alpine 32-bit"
	@echo "📦 Architecture: i686 (32-bit)"
	@echo "🚀 Optimisations: SSE2, O3, Pipe"
	@$(STRIP) --strip-all $(TARGET)
	@ls -lh $(TARGET)

# ============================================================================
# Compilation debug
# ============================================================================

debug: CFLAGS += $(DEBUG_CFLAGS) $(ALPINE_CFLAGS)
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug: $(DIRS) $(TARGET)-debug
	@echo "🐛 Version debug compilée avec sanitizers"

$(TARGET)-debug: $(OBJS)
	@echo "🔧 Linkage debug de $(NAME)..."
	@$(CC) $(LDFLAGS) -o $@ $^ $(LIBS) $(STATIC_LIBS)
	@echo "✅ Debug binaire créé: $@"

# ============================================================================
# Compilation statique (tout en un)
# ============================================================================

static: CFLAGS += -static $(ALPINE_CFLAGS)
static: LDFLAGS += -static
static: $(DIRS) $(BINDIR)/$(NAME)-static
	@echo "📦 Binaire statique créé (tout inclus)"
	@ls -lh $(BINDIR)/$(NAME)-static

$(BINDIR)/$(NAME)-static: $(OBJS)
	@echo "🔧 Linkage statique..."
	@$(CC) $(LDFLAGS) -o $@ $^ -Wl,-Bstatic $(LIBS) -Wl,-Bdynamic
	@$(STRIP) --strip-all $@

# ============================================================================
# Librairies partagées
# ============================================================================

$(SHARED_LIB): $(OBJS)
	@echo "🔧 Création de la librairie partagée..."
	@$(CC) -shared $(LDFLAGS) -Wl,-soname,lib$(NAME).so.$(VERSION) \
		-o $@ $^ $(LIBS)
	@ln -sf lib$(NAME).so.$(VERSION) $(LIBDIR)/lib$(NAME).so
	@echo "✅ Librairie partagée: $@"

$(STATIC_LIB): $(OBJS)
	@echo "🔧 Création de la librairie statique..."
	@$(AR) rcs $@ $^
	@$(RANLIB) $@
	@echo "✅ Librairie statique: $@"

# ============================================================================
# Compilation des objets
# ============================================================================

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "🛠️  Compilation: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# Binaire principal
# ============================================================================

$(TARGET): $(OBJS)
	@echo "🔧 Linkage final de $(NAME)..."
	@$(CC) $(LDFLAGS) -o $@ $^ $(LIBS) $(STATIC_LIBS)
	@echo "✅ Binaire créé: $@"

# ============================================================================
# Analyse de compatibilité Musl (Alpine)
# ============================================================================

musl-check:
	@echo "🔍 Vérification compatibilité Musl libc..."
	@for obj in $(OBJS); do \
		echo "Vérification de $$obj..."; \
		readelf -a $$obj | grep -E "GLIBC|GLIBCXX" || true; \
	done

musl-stats:
	@echo "📊 Statistiques Musl/Alpine..."
	@find $(SRCDIR) -name "*.c" -exec grep -l "gnu_source" {} \; | wc -l
	@nm $(TARGET) | grep -c "GLIBC_" || echo "Aucune dépendance GLIBC"

# ============================================================================
# Tests
# ============================================================================

test: debug
	@echo "🧪 Lancement des tests..."
	@LD_LIBRARY_PATH=$(LIBDIR) ./tests/run-tests.sh
	@valgrind --leak-check=full --show-leak-kinds=all \
		./$(BINDIR)/$(NAME)-debug --test

benchmark: release
	@echo "⚡ Lancement des benchmarks..."
	@./tests/benchmark.sh $(TARGET)
	@perf stat -e cycles,instructions,cache-misses $(TARGET) --bench

# ============================================================================
# Documentation
# ============================================================================

docs:
	@echo "📚 Génération de la documentation..."
	@doxygen Doxyfile
	@cd docs && make man
	@$(INSTALL_DIR) $(DOCDIR)
	@cp -r docs/html $(DOCDIR)/
	@cp README.md LICENSE $(DOCDIR)/

man:
	@echo "📖 Génération des pages man..."
	@help2man -N -n "Advanced Package Manager" \
		--version-string="$(VERSION)" \
		./$(TARGET) > $(DOCDIR)/$(NAME).1
	@gzip -9 $(DOCDIR)/$(NAME).1

# ============================================================================
# Nettoyage
# ============================================================================

clean:
	@echo "🧹 Nettoyage des fichiers objets..."
	@rm -rf $(OBJDIR)/*
	@rm -f $(TARGET) $(TARGET)-debug $(BINDIR)/*-static
	@rm -f $(STATIC_LIB) $(SHARED_LIB) $(LIBDIR)/lib$(NAME).so*
	@find . -name "*.gcno" -o -name "*.gcda" -delete

distclean: clean
	@echo "🗑️  Nettoyage complet..."
	@rm -rf $(OBJDIR) $(BINDIR) $(LIBDIR) $(DOCDIR)
	@rm -f *.log *.out *.core
	@rm -rf docs/html docs/latex

# ============================================================================
# Installation Alpine Linux
# ============================================================================

install: release
	@echo "📦 Installation sur Alpine Linux 32-bit..."
	@$(INSTALL_DIR) $(DESTDIR)$(BINDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(LIBDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(INCDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(DOCDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(MANDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(SYSCONFDIR)/$(NAME)
	@$(INSTALL_DIR) $(DESTDIR)/var/lib/$(NAME)
	@$(INSTALL_DIR) $(DESTDIR)/var/cache/$(NAME)
	@$(INSTALL_DIR) $(DESTDIR)/var/log/$(NAME)
	
	@$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(BINDIR_INSTALL)/$(NAME)
	@$(INSTALL_DATA) $(STATIC_LIB) $(DESTDIR)$(LIBDIR_INSTALL)/
	@$(INSTALL_DATA) $(SHARED_LIB) $(DESTDIR)$(LIBDIR_INSTALL)/
	@ln -sf lib$(NAME).so.$(VERSION) $(DESTDIR)$(LIBDIR_INSTALL)/lib$(NAME).so
	
	@cp -r $(INCDIR)/* $(DESTDIR)$(INCDIR_INSTALL)/
	@cp -r $(DOCDIR)/* $(DESTDIR)$(DOCDIR_INSTALL)/
	@cp docs/$(NAME).1.gz $(DESTDIR)$(MANDIR_INSTALL)/
	
	@cp config/$(NAME).conf $(DESTDIR)$(SYSCONFDIR)/$(NAME)/
	@cp config/$(NAME).service $(DESTDIR)$(SYSTEMDDIR)/
	
	@echo "✅ Installation terminée sur Alpine Linux"

uninstall:
	@echo "🗑️  Désinstallation..."
	@rm -f $(DESTDIR)$(BINDIR_INSTALL)/$(NAME)
	@rm -f $(DESTDIR)$(LIBDIR_INSTALL)/lib$(NAME)*
	@rm -rf $(DESTDIR)$(INCDIR_INSTALL)
	@rm -rf $(DESTDIR)$(DOCDIR_INSTALL)
	@rm -f $(DESTDIR)$(MANDIR_INSTALL)/$(NAME).1.gz
	@rm -rf $(DESTDIR)$(SYSCONFDIR)/$(NAME)
	@rm -rf $(DESTDIR)/var/lib/$(NAME)
	@rm -rf $(DESTDIR)/var/cache/$(NAME)
	@rm -f $(DESTDIR)$(SYSTEMDDIR)/$(NAME).service
	@echo "✅ Désinstallation terminée"

# ============================================================================
# Paquet Alpine (APK)
# ============================================================================

apk-package: release
	@echo "📦 Création du paquet Alpine APK..."
	@mkdir -p pkg/apkm
	@make install DESTDIR=./pkg/apkm
	@cd pkg && tar czf apkm.tar.gz apkm
	@abuild -r
	@echo "✅ Paquet APK créé"

# ============================================================================
# Vérifications
# ============================================================================

check: lint format
	@echo "🔍 Vérification du code..."
	@cppcheck --enable=all --std=c11 --error-exitcode=1 \
		--suppress=missingIncludeSystem $(SRCDIR)
	@scan-build make debug
	@flawfinder --minlevel=1 $(SRCDIR)

lint:
	@echo "🔍 Linting du code..."
	@splint +posixlib -weak $(SRCDIR)/*.c
	@clang-tidy $(SRCDIR)/*.c -- $(CFLAGS)

format:
	@echo "✨ Formatage du code..."
	@clang-format -i $(SRCDIR)/*.c $(INCDIR)/*.h
	@astyle --options=astyle.cfg $(SRCDIR)/*.c

# ============================================================================
# Rapport de taille
# ============================================================================

size-report:
	@echo "📊 Rapport de taille des binaires (32-bit):"
	@echo "========================================"
	@size $(TARGET)
	@echo ""
	@echo "📦 Analyse détaillée:"
	@bloaty $(TARGET) -d symbols -n 20 || true
	@echo ""
	@echo "🔍 Sections:"
	@readelf -S $(TARGET) | grep -E "\.(text|data|bss|rodata)" || true

# ============================================================================
# Docker Alpine 32-bit
# ============================================================================

docker-build:
	@echo "🐳 Build dans Docker Alpine 32-bit..."
	@docker run --rm -v $(PWD):/build -w /build \
		alpine:3.18 /bin/sh -c "\
			apk add --no-cache build-base gcc musl-dev \
			curl-dev sqlite-dev openssl-dev libarchive-dev \
			zstd-dev lz4-dev argon2-dev libsodium-dev \
			seccomp-dev libcap-ng-dev yaml-dev jansson-dev \
			toml-dev prometheus-cpp-dev libwebsockets-dev \
			systemd-dev && \
			make release"

# ============================================================================
# Help
# ============================================================================

help:
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo "  APKM Makefile - Alpine Linux 32-bit"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@echo "🎯 Cibles principales:"
	@echo "  make release      - Compilation optimisée (par défaut)"
	@echo "  make debug        - Compilation debug avec sanitizers"
	@echo "  make static       - Binaire statique (tout en un)"
	@echo "  make clean        - Nettoyage des fichiers objets"
	@echo "  make distclean    - Nettoyage complet"
	@echo ""
	@echo "📦 Installation:"
	@echo "  make install      - Installation sur Alpine"
	@echo "  make uninstall    - Désinstallation"
	@echo "  make apk-package  - Création d'un paquet APK"
	@echo ""
	@echo "🔧 Tests & Qualité:"
	@echo "  make test         - Lance les tests"
	@echo "  make benchmark    - Benchmarks de performance"
	@echo "  make check        - Vérification statique du code"
	@echo "  make musl-check   - Vérification compatibilité Musl"
	@echo ""
	@echo "📚 Documentation:"
	@echo "  make docs         - Génère la documentation"
	@echo "  make man          - Génère les pages man"
	@echo ""
	@echo "🐳 Docker:"
	@echo "  make docker-build - Build dans Docker Alpine 32-bit"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# ============================================================================
# Dépendances automatiques
# ============================================================================

-include $(OBJS:.o=.d)

$(OBJDIR)/%.d: $(SRCDIR)/%.c
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@

# ============================================================================
# Fin du Makefile
# ============================================================================
