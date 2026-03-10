cmake_minimum_required(VERSION 3.20)
project(apkm VERSION 2.0.0 LANGUAGES C)

# ============================================================================
# CONFIGURATION GLOBALE
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Dossiers de sortie pour tous les types de fichiers
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)

# ============================================================================
# DÉFINITIONS GLOBALES
# ============================================================================
add_definitions(-D_GNU_SOURCE)
add_definitions(-DAPKM_VERSION=\"${PROJECT_VERSION}\")
add_definitions(-D_FILE_OFFSET_BITS=64)
add_definitions(-D_LARGEFILE_SOURCE)
add_definitions(-D_LARGEFILE64_SOURCE)

# URLs Zarch Hub
set(ZARCH_HUB_URL "https://gsql-badge.onrender.com")
set(ZARCH_API_URL "${ZARCH_HUB_URL}/v5.2")
set(ZARCH_PACKAGE_URL "${ZARCH_HUB_URL}/package/download")

add_definitions(-DZARCH_HUB_URL=\"${ZARCH_HUB_URL}\")
add_definitions(-DZARCH_API_URL=\"${ZARCH_API_URL}\")
add_definitions(-DZARCH_PACKAGE_URL=\"${ZARCH_PACKAGE_URL}\")

# ============================================================================
# RECHERCHE DES BIBLIOTHÈQUES
# ============================================================================
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(JSONC REQUIRED json-c)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)
pkg_check_modules(LIBARCHIVE REQUIRED libarchive)
pkg_check_modules(LZ4 REQUIRED liblz4)
pkg_check_modules(ZSTD REQUIRED libzstd)
pkg_check_modules(YAML REQUIRED yaml-0.1)
pkg_check_modules(JANSSON REQUIRED jansson)
pkg_check_modules(ARGON2 REQUIRED libargon2)
pkg_check_modules(SECCOMP REQUIRED libseccomp)
pkg_check_modules(CAP_NG REQUIRED libcap-ng)
pkg_check_modules(BLAKE3 REQUIRED libblake3)

# ============================================================================
# LISTE DES SOURCES
# ============================================================================
set(CORE_SOURCES
    src/auth.c
    src/crypto.c
    src/db.c
    src/parser.c
    src/resolver.c
    src/sandbox.c
    src/security.c
    src/zarch.c
)

set(APKM_SOURCES
    src/main.c
    src/core.c
    src/download.c
    ${CORE_SOURCES}
)

set(APSM_SOURCES
    src/aps/apsm.c
    src/aps/auth.c
    src/aps/security.c
)

set(BOOL_SOURCES
    src/bools/bool.c
)

# ============================================================================
# BIBLIOTHÈQUES DE BASE (STATIQUES ET DYNAMIQUES)
# ============================================================================

# ----------------------------------------------------------------------------
# LIBAPKM (bibliothèque principale)
# ----------------------------------------------------------------------------
# Version statique
add_library(apkm_static STATIC ${CORE_SOURCES})
set_target_properties(apkm_static PROPERTIES 
    OUTPUT_NAME "apkm"
    PREFIX "lib"
    CLEAN_DIRECT_OUTPUT 1
)

# Version dynamique
add_library(apkm_shared SHARED ${CORE_SOURCES})
set_target_properties(apkm_shared PROPERTIES 
    OUTPUT_NAME "apkm"
    PREFIX "lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Alias pour faciliter l'utilisation
add_library(apkm::apkm ALIAS apkm_shared)

# ----------------------------------------------------------------------------
# LIBAPSM
# ----------------------------------------------------------------------------
add_library(apsm_static STATIC ${APSM_SOURCES})
set_target_properties(apsm_static PROPERTIES 
    OUTPUT_NAME "apsm"
    PREFIX "lib"
)

add_library(apsm_shared SHARED ${APSM_SOURCES})
set_target_properties(apsm_shared PROPERTIES 
    OUTPUT_NAME "apsm"
    PREFIX "lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ----------------------------------------------------------------------------
# LIBBOOL
# ----------------------------------------------------------------------------
add_library(bool_static STATIC ${BOOL_SOURCES})
set_target_properties(bool_static PROPERTIES 
    OUTPUT_NAME "bool"
    PREFIX "lib"
)

add_library(bool_shared SHARED ${BOOL_SOURCES})
set_target_properties(bool_shared PROPERTIES 
    OUTPUT_NAME "bool"
    PREFIX "lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ============================================================================
# BINAIRES EXÉCUTABLES
# ============================================================================

# APKM (lié à la bibliothèque partagée)
add_executable(apkm_bin ${APKM_SOURCES})
target_link_libraries(apkm_bin apkm_shared)
set_target_properties(apkm_bin PROPERTIES OUTPUT_NAME "apkm")

# APSM
add_executable(apsm_bin ${APSM_SOURCES})
target_link_libraries(apsm_bin apsm_shared)
set_target_properties(apsm_bin PROPERTIES OUTPUT_NAME "apsm")

# BOOL
add_executable(bool_bin ${BOOL_SOURCES})
target_link_libraries(bool_bin bool_shared)
set_target_properties(bool_bin PROPERTIES OUTPUT_NAME "bool")

# ============================================================================
# LIEN AVEC LES BIBLIOTHÈQUES EXTERNES
# ============================================================================
foreach(lib_target 
    apkm_static apkm_shared 
    apsm_static apsm_shared 
    bool_static bool_shared
)
    target_link_libraries(${lib_target}
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        SQLite::SQLite3
        ${JSONC_LDFLAGS}
        ${LIBSODIUM_LDFLAGS}
        ${LIBARCHIVE_LDFLAGS}
        ${LZ4_LDFLAGS}
        ${ZSTD_LDFLAGS}
        ${YAML_LDFLAGS}
        ${JANSSON_LDFLAGS}
        ${ARGON2_LDFLAGS}
        ${SECCOMP_LDFLAGS}
        ${CAP_NG_LDFLAGS}
        ${BLAKE3_LDFLAGS}
        Threads::Threads
        rt
        dl
        m
    )
    
    target_include_directories(${lib_target} PRIVATE
        ${JSONC_INCLUDE_DIRS}
        ${LIBSODIUM_INCLUDE_DIRS}
        ${LIBARCHIVE_INCLUDE_DIRS}
        ${LZ4_INCLUDE_DIRS}
        ${ZSTD_INCLUDE_DIRS}
        ${YAML_INCLUDE_DIRS}
        ${JANSSON_INCLUDE_DIRS}
        ${ARGON2_INCLUDE_DIRS}
        ${SECCOMP_INCLUDE_DIRS}
        ${CAP_NG_INCLUDE_DIRS}
        ${BLAKE3_INCLUDE_DIRS}
    )
endforeach()

# ============================================================================
# GÉNÉRATION DES FICHIERS PC (PKG-CONFIG)
# ============================================================================
set(PC_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig")

# Template pour apkm.pc
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/apkm.pc.in"
    "${CMAKE_BINARY_DIR}/apkm.pc"
    @ONLY
)

# Template pour apsm.pc
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/apsm.pc.in"
    "${CMAKE_BINARY_DIR}/apsm.pc"
    @ONLY
)

# Template pour bool.pc
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/bool.pc.in"
    "${CMAKE_BINARY_DIR}/bool.pc"
    @ONLY
)

# ============================================================================
# CONFIGURATION POUR CPACK (PACKAGING MULTI-PLATEFORME)
# ============================================================================
set(CPACK_PACKAGE_NAME "apkm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Gopu.inc")
set(CPACK_PACKAGE_CONTACT "ceoseshell@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION "APKM - Advanced Package Manager for Alpine Linux")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gopu-inc/apkm-gest")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# ----------------------------------------------------------------------------
# ARCHITECTURES SUPPORTÉES
# ----------------------------------------------------------------------------
set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

# Liste de toutes les architectures cibles
set(APKM_ARCHITECTURES
    x86_64
    i386
    armv7
    arm64
    aarch64
    ppc64le
    s390x
    riscv64
    mips
    mips64
    sparc64
)

# ----------------------------------------------------------------------------
# GÉNÉRATEURS PACKAGING (TOUS LES FORMATS)
# ----------------------------------------------------------------------------
set(CPACK_GENERATOR
    TGZ                # .tar.gz universel
    TBZ2               # .tar.bz2
    TXZ                # .tar.xz
    TZ                 # .tar.Z
    ZIP                # .zip pour Windows
    STGZ               # Auto-extractible script
    DEB                # Debian/Ubuntu
    RPM                # Red Hat/Fedora/CentOS
    NSIS               # Windows installer
    DragNDrop          # macOS DMG
    Bundle             # macOS Bundle
    CygwinBinary       # Cygwin packages
    WIX                # Windows Installer XML
    IFW                # Qt Installer Framework
    PKG                # macOS PKG
    NuGet              # NuGet packages
    productbuild       # macOS productbuild
)

# ----------------------------------------------------------------------------
# CONFIGURATION PAR PLATEFORME
# ----------------------------------------------------------------------------

# Linux
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libcurl4, libssl3, libsqlite3-0, libsodium23, libarchive13, liblz4-1, libzstd1, libyaml-0-2, libjansson4, libargon2-1, libseccomp2, libcap2")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME "apkm-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}.deb")

set(CPACK_RPM_PACKAGE_REQUIRES "curl, openssl, sqlite, libsodium, libarchive, lz4, zstd, libyaml, jansson, argon2, libseccomp, libcap")
set(CPACK_RPM_FILE_NAME "apkm-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}.rpm")

# Windows
set(CPACK_NSIS_DISPLAY_NAME "APKM Package Manager")
set(CPACK_NSIS_HELP_LINK "https://github.com/gopu-inc/apkm-gest")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/gopu-inc/apkm-gest")
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

# macOS
set(CPACK_BUNDLE_NAME "APKM")
set(CPACK_BUNDLE_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist")
set(CPACK_DMG_VOLUME_NAME "APKM-${PROJECT_VERSION}")

# ----------------------------------------------------------------------------
# COMPOSANTS
# ----------------------------------------------------------------------------
set(CPACK_COMPONENTS_ALL 
    libraries
    binaries
    headers
    pkgconfig
    documentation
    examples
    tests
)

# Définition des composants
cpack_add_component(libraries
    DISPLAY_NAME "Libraries"
    DESCRIPTION "Shared and static libraries"
    GROUP "Runtime"
)

cpack_add_component(binaries
    DISPLAY_NAME "Binaries"
    DESCRIPTION "Executable programs"
    GROUP "Runtime"
    DEPENDS libraries
)

cpack_add_component(headers
    DISPLAY_NAME "Headers"
    DESCRIPTION "Development headers"
    GROUP "Development"
)

cpack_add_component(pkgconfig
    DISPLAY_NAME "pkg-config files"
    DESCRIPTION "pkg-config configuration files"
    GROUP "Development"
)

# ----------------------------------------------------------------------------
# ARCHIVES MULTI-ARCHITECTURES
# ----------------------------------------------------------------------------
foreach(arch ${APKM_ARCHITECTURES})
    set(CPACK_SYSTEM_NAME "Linux-${arch}")
    set(CPACK_PACKAGE_FILE_NAME "apkm-${PROJECT_VERSION}-Linux-${arch}")
    
    # Configurations spécifiques à l'architecture
    if(arch STREQUAL "x86_64")
        set(CPACK_DEBIAN_ARCHITECTURE "amd64")
        set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
    elseif(arch STREQUAL "i386")
        set(CPACK_DEBIAN_ARCHITECTURE "i386")
        set(CPACK_RPM_PACKAGE_ARCHITECTURE "i386")
    elseif(arch STREQUAL "arm64" OR arch STREQUAL "aarch64")
        set(CPACK_DEBIAN_ARCHITECTURE "arm64")
        set(CPACK_RPM_PACKAGE_ARCHITECTURE "aarch64")
    elseif(arch STREQUAL "armv7")
        set(CPACK_DEBIAN_ARCHITECTURE "armhf")
        set(CPACK_RPM_PACKAGE_ARCHITECTURE "armv7hl")
    endif()
endforeach()

# ============================================================================
# INSTALLATION
# ============================================================================

# Bibliothèques
install(TARGETS apkm_static apkm_shared
    LIBRARY DESTINATION lib COMPONENT libraries
    ARCHIVE DESTINATION lib COMPONENT libraries
    RUNTIME DESTINATION bin COMPONENT binaries
)

install(TARGETS apsm_static apsm_shared
    LIBRARY DESTINATION lib COMPONENT libraries
    ARCHIVE DESTINATION lib COMPONENT libraries
)

install(TARGETS bool_static bool_shared
    LIBRARY DESTINATION lib COMPONENT libraries
    ARCHIVE DESTINATION lib COMPONENT libraries
)

# Binaires
install(TARGETS apkm_bin apsm_bin bool_bin
    RUNTIME DESTINATION bin COMPONENT binaries
)

# Headers
install(FILES 
    ${CMAKE_SOURCE_DIR}/include/apkm.h
    ${CMAKE_SOURCE_DIR}/include/core.h
    ${CMAKE_SOURCE_DIR}/include/security.h
    ${CMAKE_SOURCE_DIR}/include/sandbox.h
    ${CMAKE_SOURCE_DIR}/include/zarch.h
    DESTINATION include/apkm
    COMPONENT headers
)

# Fichiers pkg-config
install(FILES 
    ${CMAKE_BINARY_DIR}/apkm.pc
    ${CMAKE_BINARY_DIR}/apsm.pc
    ${CMAKE_BINARY_DIR}/bool.pc
    DESTINATION lib/pkgconfig
    COMPONENT pkgconfig
)

# Documentation
install(FILES 
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/LICENSE
    DESTINATION share/doc/apkm
    COMPONENT documentation
)

# Scripts d'installation
install(FILES 
    ${CMAKE_SOURCE_DIR}/install.sh
    DESTINATION share/apkm/scripts
    PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_READ WORLD_READ
    COMPONENT binaries
)

# ============================================================================
# TESTS
# ============================================================================
enable_testing()

add_test(NAME apkm_version COMMAND apkm_bin --version)
add_test(NAME apkm_help COMMAND apkm_bin --help)
add_test(NAME apsm_help COMMAND apsm_bin help)
add_test(NAME bool_help COMMAND bool_bin --help)

# ============================================================================
# INCLUDE CPACK
# ============================================================================
include(CPack)

# ============================================================================
# MESSAGES DE FIN
# ============================================================================
message(STATUS "")
message(STATUS "==================================================")
message(STATUS "🚀 APKM v${PROJECT_VERSION} - Configuration complète")
message(STATUS "==================================================")
message(STATUS "📦 Zarch Hub: ${ZARCH_HUB_URL}")
message(STATUS "")
message(STATUS "📚 Bibliothèques générées:")
message(STATUS "   • libapkm.so (shared)")
message(STATUS "   • libapkm.a (static)")
message(STATUS "   • libapsm.so (shared)")
message(STATUS "   • libapsm.a (static)")
message(STATUS "   • libbool.so (shared)")
message(STATUS "   • libbool.a (static)")
message(STATUS "")
message(STATUS "📦 Formats de packaging:")
message(STATUS "   • TGZ, TBZ2, TXZ, TZ (archives)")
message(STATUS "   • DEB, RPM (Linux packages)")
message(STATUS "   • NSIS, WIX (Windows installers)")
message(STATUS "   • DragNDrop, Bundle, PKG (macOS)")
message(STATUS "   • ZIP (cross-platform)")
message(STATUS "")
message(STATUS "🔧 Architectures ciblées:")
foreach(arch ${APKM_ARCHITECTURES})
    message(STATUS "   • ${arch}")
endforeach()
message(STATUS "")
message(STATUS "📁 Fichiers générés dans build/")
message(STATUS "==================================================")
