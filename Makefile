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

# Fichiers sources
SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

# Binaires
TARGET = $(BINDIR)/$(NAME)
STATIC_LIB = $(LIBDIR)/lib$(NAME).a
SHARED_LIB = $(LIBDIR)/lib$(NAME).so.$(VERSION)

# Compilation flags
CFLAGS = -m32 -march=i686 -mtune=generic
CFLAGS += -O3 -pipe -fomit-frame-pointer -funroll-loops
CFLAGS += -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion
CFLAGS += -Wstrict-prototypes -Wmissing-prototypes -Wredundant-decls
CFLAGS += -D_FORTIFY_SOURCE=2 -fstack-protector-strong
CFLAGS += -fPIC -fPIE -fvisibility=hidden
CFLAGS += -DAPKM_VERSION=\"$(VERSION)\" -DAPKM_CODENAME=\"$(CODENAME)\"
CFLAGS += -DARCH_X86 -DALPINE_LINUX
CFLAGS += -I$(INCDIR) -I/usr/include -I/usr/local/include
CFLAGS += -msse2 -mfpmath=sse -ffast-math
CFLAGS += -falign-functions=16 -falign-loops=16

# Linker flags
LDFLAGS = -m32 -Wl,-O3 -Wl,--as-needed -Wl,--hash-style=gnu
LDFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
LDFLAGS += -L$(LIBDIR) -L/usr/lib -L/usr/local/lib

# Librairies
LIBS = -lcurl -lsqlite3 -lssl -lcrypto -lz
LIBS += -lpthread -lrt -ldl -lm
LIBS += -lsodium -larchive -llz4 -lzstd
LIBS += -lyaml -ljansson -largon2
LIBS += -lseccomp -lcap-ng
LIBS += -ljson-c

# Outils d'installation
INSTALL = install -c
INSTALL_PROGRAM = $(INSTALL) -m 755
INSTALL_DATA = $(INSTALL) -m 644
INSTALL_DIR = $(INSTALL) -d

# Chemins d'installation
PREFIX ?= /usr/local
BINDIR_INSTALL = $(PREFIX)/bin
LIBDIR_INSTALL = $(PREFIX)/lib
INCDIR_INSTALL = $(PREFIX)/include/$(NAME)
DOCDIR_INSTALL = $(PREFIX)/share/doc/$(NAME)

# ============================================================================
# Création des répertoires
# ============================================================================

$(shell mkdir -p $(OBJDIR) $(BINDIR) $(LIBDIR) $(DOCDIR))

# ============================================================================
# Règles principales
# ============================================================================

.PHONY: all clean distclean install uninstall
.PHONY: release static shared tools libs
.PHONY: docs help

all: release tools libs
	@echo "✅ APKM build completed successfully"

# ============================================================================
# Compilation du binaire principal
# ============================================================================

release: CFLAGS += -DNDEBUG
release: $(TARGET)
	@$(STRIP) --strip-all $(TARGET)
	@echo "✅ Binary compiled: $(TARGET)"

$(TARGET): $(OBJS) | $(BINDIR)
	@echo "🔗 Linking $@..."
	@$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
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

# ============================================================================
# Outils (apsm et bool)
# ============================================================================

tools: $(BINDIR)/apsm $(BINDIR)/bool
	@echo "✅ Tools compiled successfully"

$(BINDIR)/apsm: src/apsm.c src/security.c src/auth.c | $(BINDIR)
	@echo "🔧 Compiling APSM..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@$(STRIP) --strip-all $@

$(BINDIR)/bool: src/bool.c | $(BINDIR)
	@echo "🔧 Compiling BOOL..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
	@$(STRIP) --strip-all $@

# ============================================================================
# Dépendances automatiques
# ============================================================================

-include $(OBJS:.o=.d)

$(OBJDIR)/%.d: $(SRCDIR)/%.c | $(OBJDIR)
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
	@$(INSTALL_DIR) $(DESTDIR)/var/lib/$(NAME)
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
	
	@echo "✅ APKM installed successfully"

uninstall:
	@echo "🗑️  Uninstalling APKM..."
	@rm -f $(DESTDIR)$(BINDIR_INSTALL)/$(NAME)
	@rm -f $(DESTDIR)$(BINDIR_INSTALL)/apsm
	@rm -f $(DESTDIR)$(BINDIR_INSTALL)/bool
	@rm -f $(DESTDIR)$(LIBDIR_INSTALL)/lib$(NAME).*
	@rm -rf $(DESTDIR)$(INCDIR_INSTALL)
	@rm -f $(DESTDIR)$(PKGCONFIGDIR)/$(NAME).pc
	@echo "✅ APKM uninstalled"

# ============================================================================
# Nettoyage
# ============================================================================

clean:
	@echo "🧹 Cleaning..."
	@rm -rf $(OBJDIR) $(BINDIR) $(LIBDIR)
	@rm -f *.log *.out *.core
	@echo "✅ Clean completed"

distclean: clean
	@echo "🗑️  Full clean..."
	@rm -rf $(DOCDIR)
	@echo "✅ Full clean completed"

# ============================================================================
# Documentation
# ============================================================================

docs:
	@echo "📚 Generating documentation..."
	@mkdir -p $(DOCDIR)
	@cp README.md $(DOCDIR)/ 2>/dev/null || true
	@cp APKMBUILD $(DOCDIR)/ 2>/dev/null || true
	@echo "✅ Documentation generated"

# ============================================================================
# Help
# ============================================================================

help:
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo "  APKM Makefile - Production Build"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build everything"
	@echo "  make release  - Build binaries"
	@echo "  make tools    - Build APSM and BOOL"
	@echo "  make libs     - Build libraries"
	@echo "  make install  - Install system"
	@echo "  make uninstall- Remove installation"
	@echo "  make clean    - Clean build files"
	@echo "  make docs     - Generate docs"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
