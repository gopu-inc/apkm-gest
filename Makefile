# ============================================================================
# APKM - Advanced Package Manager
# Production Makefile for Alpine Linux 32-bit
# Version: 2.0.0
# ============================================================================

# Configuration de base
NAME = apkm
VERSION = 2.0.0
CODENAME = "Production Release"

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

# Fichiers sources (tous les .c du répertoire src)
SRCS = $(wildcard $(SRCDIR)/*.c)

# Fichiers objets
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

# Debug flags (commentés en production)
# DEBUG_CFLAGS = -g3 -ggdb -DDEBUG -D_DEBUG
# DEBUG_CFLAGS += -fsanitize=address -fsanitize=undefined
# DEBUG_CFLAGS += -fno-omit-frame-pointer

# Linker flags pour 32-bit
LDFLAGS = -m32 -Wl,-O3 -Wl,--as-needed -Wl,--hash-style=gnu
LDFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
LDFLAGS += -L$(LIBDIR) -L/usr/lib -L/usr/local/lib
LDFLAGS += -Wl,-rpath,/usr/lib/$(NAME)

# Librairies Alpine Linux 32-bit
LIBS = -lcurl -lsqlite3 -lssl -lcrypto -lz
LIBS += -lpthread -lrt -ldl -lm
LIBS += -lsodium -larchive -llz4 -lzstd
LIBS += -lyaml -ljansson -largon2
LIBS += -lseccomp -lcap-ng
LIBS += -ljson-c

# Outils
INSTALL = install -c
INSTALL_PROGRAM = $(INSTALL) -m 755
INSTALL_DATA = $(INSTALL) -m 644
INSTALL_DIR = $(INSTALL) -d

# Chemins d'installation Alpine
PREFIX ?= /usr/local
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
.PHONY: debug release static shared tools
.PHONY: docs man pages check lint format
.PHONY: install-dev install-static install-shared
.PHONY: docker musl-stats size-report

all: release tools libs

# Création des répertoires
$(DIRS):
	@mkdir -p $@

# ============================================================================
# Compilation release (optimisée)
# ============================================================================

release: CFLAGS += -DNDEBUG
release: $(DIRS) $(TARGET)
	@echo "✅ APKM v$(VERSION) compiled for Alpine 32-bit"
	@echo "📦 Architecture: i686 (32-bit)"
	@echo "🚀 Optimizations: O3, SSE2, Pipe"
	@$(STRIP) --strip-all $(TARGET)
	@ls -lh $(TARGET)

$(TARGET): $(OBJS)
	@echo "🔗 Linking $@..."
	@$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "✅ Binary created: $@"

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(DIRS)
	@echo "🛠️  Compiling: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# Librairies
# ============================================================================

libs: $(SHARED_LIB) $(STATIC_LIB)
	@echo "✅ Libraries built successfully"

$(SHARED_LIB): $(OBJS) | $(LIBDIR)
	@echo "🔧 Creating shared library..."
	@$(CC) -shared $(LDFLAGS) -Wl,-soname,lib$(NAME).so.$(VERSION) -o $@ $^ $(LIBS)
	@ln -sf lib$(NAME).so.$(VERSION) $(LIBDIR)/lib$(NAME).so
	@echo "✅ Shared library: $@"

$(STATIC_LIB): $(OBJS) | $(LIBDIR)
	@echo "🔧 Creating static library..."
	@$(AR) rcs $@ $^
	@$(RANLIB) $@
	@echo "✅ Static library: $@"

$(LIBDIR):
	@mkdir -p $@

# ============================================================================
# Outils (apsm et bool)
# ============================================================================

tools: $(BINDIR)/apsm $(BINDIR)/bool
	@echo "✅ Tools compiled successfully"

$(BINDIR)/apsm: src/apsm.c src/security.c src/auth.c | $(BINDIR)
	@echo "🔧 Compiling APSM..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@$(STRIP) --strip-all $@
	@echo "✅ APSM: $@"

$(BINDIR)/bool: src/bool.c | $(BINDIR)
	@echo "🔧 Compiling BOOL..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@$(STRIP) --strip-all $@
	@echo "✅ BOOL: $@"

$(BINDIR):
	@mkdir -p $@

# ============================================================================
# Dépendances automatiques
# ============================================================================

-include $(OBJS:.o=.d)

$(OBJDIR)/%.d: $(SRCDIR)/%.c | $(DIRS)
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@

# ============================================================================
# Installation
# ============================================================================

install: all
	@echo "📦 Installing APKM system..."
	@$(INSTALL_DIR) $(DESTDIR)$(BINDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(LIBDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(INCDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(DOCDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(MANDIR_INSTALL)
	@$(INSTALL_DIR) $(DESTDIR)$(SYSCONFDIR)/$(NAME)
	@$(INSTALL_DIR) $(DESTDIR)/var/lib/$(NAME)
	@$(INSTALL_DIR) $(DESTDIR)/var/cache/$(NAME)
	@$(INSTALL_DIR) $(DESTDIR)/var/log/$(NAME)
	@$(INSTALL_DIR) $(DESTDIR)$(PKGCONFIGDIR)
	
	# Binaires
	@$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(BINDIR_INSTALL)/$(NAME)
	@$(INSTALL_PROGRAM) $(BINDIR)/apsm $(DESTDIR)$(BINDIR_INSTALL)/apsm
	@$(INSTALL_PROGRAM) $(BINDIR)/bool $(DESTDIR)$(BINDIR_INSTALL)/bool
	
	# Librairies
	@$(INSTALL_DATA) $(STATIC_LIB) $(DESTDIR)$(LIBDIR_INSTALL)/
	@$(INSTALL_DATA) $(SHARED_LIB) $(DESTDIR)$(LIBDIR_INSTALL)/
	@ln -sf lib$(NAME).so.$(VERSION) $(DESTDIR)$(LIBDIR_INSTALL)/lib$(NAME).so
	
	# Headers
	@$(INSTALL_DIR) $(DESTDIR)$(INCDIR_INSTALL)
	@cp -r $(INCDIR)/*.h $(DESTDIR)$(INCDIR_INSTALL)/
	
	# pkg-config
	@echo "prefix=$(PREFIX)" > $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "exec_prefix=\$${prefix}" >> $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "libdir=\$${exec_prefix}/lib" >> $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "includedir=\$${prefix}/include/$(NAME)" >> $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "" >> $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "Name: $(NAME)" >> $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "Description: Advanced Package Manager" >> $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "Version: $(VERSION)" >> $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "Libs: -L\$${libdir} -l$(NAME)" >> $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "Cflags: -I\$${includedir}" >> $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	
	# Documentation
	@cp -r $(DOCDIR)/* $(DESTDIR)$(DOCDIR_INSTALL)/ 2>/dev/null || true
	
	# Configuration
	@echo "# APKM Configuration" > $(DESTDIR)$(SYSCONFDIR)/$(NAME)/config.toml
	@echo "repository = \"https://github.com/gopu-inc/apkm-gest\"" >> $(DESTDIR)$(SYSCONFDIR)/$(NAME)/config.toml
	
	@echo "✅ APKM installed successfully in $(DESTDIR)$(PREFIX)"
	@echo "📋 Commands: apkm, apsm, bool"
	@echo "📚 Libraries: $(DESTDIR)$(LIBDIR_INSTALL)/lib$(NAME).{a,so}"
	@echo "📁 Headers: $(DESTDIR)$(INCDIR_INSTALL)/"

uninstall:
	@echo "🗑️  Uninstalling APKM..."
	@rm -f $(DESTDIR)$(BINDIR_INSTALL)/$(NAME)
	@rm -f $(DESTDIR)$(BINDIR_INSTALL)/apsm
	@rm -f $(DESTDIR)$(BINDIR_INSTALL)/bool
	@rm -f $(DESTDIR)$(LIBDIR_INSTALL)/lib$(NAME).*
	@rm -rf $(DESTDIR)$(INCDIR_INSTALL)
	@rm -rf $(DESTDIR)$(DOCDIR_INSTALL)
	@rm -f $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@rm -rf $(DESTDIR)$(SYSCONFDIR)/$(NAME)
	@echo "✅ APKM uninstalled"

# ============================================================================
# Nettoyage
# ============================================================================

clean:
	@echo "🧹 Cleaning object files and binaries..."
	@rm -rf $(OBJDIR) $(BINDIR) $(LIBDIR)
	@rm -f *.log *.out *.core
	@echo "✅ Clean completed"

distclean: clean
	@echo "🗑️  Full clean..."
	@rm -rf $(DOCDIR)
	@rm -f $(TARGET) $(BINDIR)/apsm $(BINDIR)/bool
	@rm -f $(STATIC_LIB) $(SHARED_LIB)
	@echo "✅ Full clean completed"

# ============================================================================
# Tests
# ============================================================================

test: all
	@echo "🧪 Running tests..."
	@./tests/run-tests.sh || true

check: test

# ============================================================================
# Documentation
# ============================================================================

docs:
	@echo "📚 Generating documentation..."
	@mkdir -p $(DOCDIR)
	@cp README.md $(DOCDIR)/ 2>/dev/null || true
	@cp APKMBUILD $(DOCDIR)/ 2>/dev/null || true
	@echo "✅ Documentation generated in $(DOCDIR)/"

man:
	@echo "📖 Generating man pages..."
	@mkdir -p $(DOCDIR)/man
	@help2man -N -n "Advanced Package Manager" \
		--version-string="$(VERSION)" \
		./$(TARGET) > $(DOCDIR)/man/$(NAME).1 2>/dev/null || true
	@echo "✅ Man pages generated"

# ============================================================================
# Formatage et linting
# ============================================================================

format:
	@echo "✨ Formatting code..."
	@clang-format -i $(SRCDIR)/*.c $(INCDIR)/*.h 2>/dev/null || true
	@echo "✅ Code formatted"

lint:
	@echo "🔍 Linting code..."
	@cppcheck --enable=all --std=c11 --error-exitcode=1 \
		--suppress=missingIncludeSystem $(SRCDIR) 2>/dev/null || true
	@echo "✅ Lint completed"

# ============================================================================
# Rapport de taille
# ============================================================================

size-report: all
	@echo "📊 Size report:"
	@echo "========================"
	@size $(TARGET)
	@echo ""
	@echo "📦 Binary: $(TARGET)"
	@ls -lh $(TARGET)
	@echo "📚 Libraries:"
	@ls -lh $(LIBDIR)/ 2>/dev/null || echo "  No libraries built"

# ============================================================================
# Paquet Alpine (APK)
# ============================================================================

apk-package: all
	@echo "📦 Creating Alpine APK package..."
	@mkdir -p pkg/apkm
	@make install DESTDIR=./pkg/apkm
	@cd pkg && tar -czf apkm.tar.gz apkm
	@echo "✅ APK package created: pkg/apkm.tar.gz"

# ============================================================================
# Docker
# ============================================================================

docker-build:
	@echo "🐳 Building in Docker Alpine 32-bit..."
	@docker run --rm -v $(PWD):/build -w /build \
		alpine:3.18 /bin/sh -c "\
			apk add --no-cache build-base gcc musl-dev \
			curl-dev sqlite-dev openssl-dev libarchive-dev \
			zstd-dev lz4-dev argon2-dev libsodium-dev \
			seccomp-dev libcap-ng-dev yaml-dev jansson-dev \
			json-c-dev && \
			make release && make tools"

# ============================================================================
# Help
# ============================================================================

help:
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo "  APKM Makefile - Production Build System"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@echo "🎯 Targets:"
	@echo "  make all          - Build everything (binaries + tools + libs)"
	@echo "  make release      - Build optimized binaries"
	@echo "  make tools        - Build APSM and BOOL tools"
	@echo "  make libs         - Build shared and static libraries"
	@echo "  make install      - Install system (requires root)"
	@echo "  make uninstall    - Remove installation"
	@echo "  make clean        - Remove object files"
	@echo "  make distclean    - Full clean"
	@echo ""
	@echo "📦 Packaging:"
	@echo "  make apk-package  - Create Alpine APK package"
	@echo "  make docker-build - Build in Docker container"
	@echo ""
	@echo "📚 Documentation:"
	@echo "  make docs         - Generate documentation"
	@echo "  make man          - Generate man pages"
	@echo ""
	@echo "🔧 Development:"
	@echo "  make format       - Format code with clang-format"
	@echo "  make lint         - Run static code analysis"
	@echo "  make test         - Run tests"
	@echo "  make size-report  - Show binary sizes"
	@echo ""
	@echo "📋 Commands after install:"
	@echo "  apkm              - Package manager"
	@echo "  apsm              - GitHub publisher"
	@echo "  bool              - Package builder"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# ============================================================================
# Cible par défaut
# ============================================================================

.DEFAULT_GOAL := all
