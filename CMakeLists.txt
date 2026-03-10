# ============================================================================
# PROJET APKM - Package Manager avec BOOL/SELP
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(apkm 
    VERSION 2.0.0 
    LANGUAGES C 
    DESCRIPTION "APKM - Advanced Package Manager with SELP compression"
    HOMEPAGE_URL "https://github.com/gopu-inc/apkm-gest"
)

# ============================================================================
# CONFIGURATION GLOBALE
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Dossiers de sortie
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Dossiers d'installation
set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation prefix")

# ============================================================================
# DÉTECTION ARCHITECTURE ET OPTIMISATIONS
# ============================================================================
include(TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)")
    set(APKM_ARCH "x86_64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64 -msse4.2 -maes -mpclmul -mavx -mavx2")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)")
    set(APKM_ARCH "i386")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -msse2")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
    set(APKM_ARCH "arm64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8.2-a+crypto -mfpu=neon")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
    set(APKM_ARCH "armv7")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfpu=neon -mfloat-abi=hard")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ppc64|powerpc64)")
    set(APKM_ARCH "ppc64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=power8 -mtune=power8 -maltivec")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(s390x)")
    set(APKM_ARCH "s390x")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=zEC12 -mzarch")
else()
    set(APKM_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# ============================================================================
# OPTIONS DE BUILD
# ============================================================================
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_APSM "Build APSM tool" ON)
option(BUILD_BOOL "Build BOOL tool with SELP" ON)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_DOCS "Build documentation" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(ENABLE_LTO "Enable Link Time Optimization" ON)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
option(USE_STATIC_LIBS "Use static libraries" OFF)
option(ENABLE_DEBUG_SYMBOLS "Enable debug symbols" OFF)

# ============================================================================
# FLAGS DE COMPILATION AVANCÉS
# ============================================================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -ggdb -fno-omit-frame-pointer -DDEBUG")
    if(ENABLE_ASAN)
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address,undefined -fno-omit-frame-pointer")
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -flto=auto")
    if(ENABLE_LTO)
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto=auto")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto=auto")
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG -flto=auto")
endif()

# Flags de sécurité
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fpie -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes -Wmissing-prototypes -Werror=implicit-function-declaration")

# Flags de lien
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--as-needed -Wl,--no-undefined")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-O1 -Wl,--sort-common -Wl,--hash-style=gnu")

# ============================================================================
# INCLUDE DIRECTORIES
# ============================================================================
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# ============================================================================
# DÉFINITIONS GLOBALES
# ============================================================================
add_definitions(-D_GNU_SOURCE)
add_definitions(-DAPKM_VERSION=\"${PROJECT_VERSION}\")
add_definitions(-DAPKM_ARCH=\"${APKM_ARCH}\")
add_definitions(-D_FILE_OFFSET_BITS=64)
add_definitions(-D_LARGEFILE_SOURCE)
add_definitions(-D_LARGEFILE64_SOURCE)
add_definitions(-D_POSIX_C_SOURCE=200809L)
add_definitions(-D_XOPEN_SOURCE=700)

if(IS_BIG_ENDIAN)
    add_definitions(-DWORDS_BIGENDIAN)
endif()

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
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# Bibliothèques optionnelles
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
# COLLECTE DES SOURCES
# ============================================================================

# Sources communes
set(CORE_SOURCES
    src/auth.c
    src/crypto.c
    src/db.c
    src/parser.c
    src/resolver.c
    src/sandbox.c
    src/security.c
    src/zarch.c
    src/utils.c
)

# Sources APKM
set(APKM_SOURCES
    src/main.c
    src/core.c
    src/download.c
    ${CORE_SOURCES}
)

# Sources APSM
set(APSM_SOURCES
    src/aps/apsm.c
    src/aps/auth.c
    src/aps/security.c
    src/aps/publish.c
    src/aps/token.c
)

# ============================================================================
# SOURCES BOOL AVEC FORMAT SELP
# ============================================================================
set(BOOL_SOURCES
    src/bools/bool.c
    src/bools/selp.h
    src/bools/selp_compress.c
    src/bools/selp_decompress.c
    src/bools/selp_crypto.c
    src/bools/selp_archive.c
    src/bools/selp_verify.c
    src/bools/selp_utils.c
    src/bools/selp_header.c
    src/bools/selp_checksum.c
    src/bools/selp_hash.c
)

# ============================================================================
# CRÉATION DES BIBLIOTHÈQUES
# ============================================================================

# ----------------------------------------------------------------------------
# Bibliothèque CORE (statique)
# ----------------------------------------------------------------------------
add_library(apkm_core_static STATIC ${CORE_SOURCES})
set_target_properties(apkm_core_static PROPERTIES 
    OUTPUT_NAME "apkm_core"
    PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)

# ----------------------------------------------------------------------------
# Bibliothèque CORE (partagée)
# ----------------------------------------------------------------------------
add_library(apkm_core_shared SHARED ${CORE_SOURCES})
set_target_properties(apkm_core_shared PROPERTIES 
    OUTPUT_NAME "apkm_core"
    PREFIX "lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

# ----------------------------------------------------------------------------
# Bibliothèque BOOL/SELP (statique)
# ----------------------------------------------------------------------------
add_library(bool_static STATIC ${BOOL_SOURCES})
set_target_properties(bool_static PROPERTIES 
    OUTPUT_NAME "bool"
    PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)

# ----------------------------------------------------------------------------
# Bibliothèque BOOL/SELP (partagée)
# ----------------------------------------------------------------------------
add_library(bool_shared SHARED ${BOOL_SOURCES})
set_target_properties(bool_shared PROPERTIES 
    OUTPUT_NAME "bool"
    PREFIX "lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

# ============================================================================
# CRÉATION DES EXÉCUTABLES
# ============================================================================

# APKM
add_executable(apkm ${APKM_SOURCES})
target_link_libraries(apkm
    apkm_core_shared
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
    rt dl m
)
set_target_properties(apkm PROPERTIES 
    OUTPUT_NAME "apkm"
    PREFIX ""
)

# APSM
if(BUILD_APSM)
    add_executable(apsm ${APSM_SOURCES})
    target_link_libraries(apsm
        apkm_core_shared
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        SQLite::SQLite3
        ${JSONC_LDFLAGS}
        pthread rt dl m
    )
    set_target_properties(apsm PROPERTIES 
        OUTPUT_NAME "apsm"
        PREFIX ""
    )
endif()

# BOOL (SELP)
if(BUILD_BOOL)
    add_executable(bool ${BOOL_SOURCES})
    target_link_libraries(bool
        bool_shared
        ZLIB::ZLIB
        OpenSSL::Crypto
        ${CMAKE_THREAD_LIBS_INIT}
        m
    )
    target_include_directories(bool PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${ZLIB_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
    )
    set_target_properties(bool PROPERTIES 
        OUTPUT_NAME "bool"
        PREFIX ""
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endif()

# ============================================================================
# LIEN AVEC LES BIBLIOTHÈQUES EXTERNES (POUR TOUS)
# ============================================================================
foreach(target 
    apkm_core_static apkm_core_shared 
    bool_static bool_shared
    apkm apsm bool
)
    if(TARGET ${target})
        target_include_directories(${target} PRIVATE
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
            ${ZLIB_INCLUDE_DIRS}
            ${OPENSSL_INCLUDE_DIR}
        )
    endif()
endforeach()

# ============================================================================
# GÉNÉRATION DES FICHIERS PC (PKG-CONFIG)
# ============================================================================
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/apkm.pc.in"
    "${CMAKE_BINARY_DIR}/apkm.pc"
    @ONLY
)

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/apsm.pc.in"
    "${CMAKE_BINARY_DIR}/apsm.pc"
    @ONLY
)

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/bool.pc.in"
    "${CMAKE_BINARY_DIR}/bool.pc"
    @ONLY
)

# ============================================================================
# INSTALLATION
# ============================================================================

# Bibliothèques
install(TARGETS apkm_core_static apkm_core_shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    COMPONENT libraries
)

install(TARGETS bool_static bool_shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT libraries
)

# Binaires
install(TARGETS apkm
    RUNTIME DESTINATION bin
    COMPONENT binaries
)

if(BUILD_APSM)
    install(TARGETS apsm
        RUNTIME DESTINATION bin
        COMPONENT binaries
    )
endif()

if(BUILD_BOOL)
    install(TARGETS bool
        RUNTIME DESTINATION bin
        COMPONENT binaries
    )
endif()

# Headers
install(FILES 
    ${CMAKE_SOURCE_DIR}/include/apkm.h
    ${CMAKE_SOURCE_DIR}/include/core.h
    ${CMAKE_SOURCE_DIR}/include/security.h
    ${CMAKE_SOURCE_DIR}/include/sandbox.h
    ${CMAKE_SOURCE_DIR}/include/zarch.h
    ${CMAKE_SOURCE_DIR}/src/bools/selp.h
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
if(BUILD_DOCS)
    install(FILES 
        ${CMAKE_SOURCE_DIR}/README.md
        ${CMAKE_SOURCE_DIR}/LICENSE
        ${CMAKE_SOURCE_DIR}/docs/BOOL.md
        ${CMAKE_SOURCE_DIR}/docs/SELP.md
        DESTINATION share/doc/apkm
        COMPONENT documentation
    )
endif()

# Scripts
install(FILES 
    ${CMAKE_SOURCE_DIR}/install.sh
    DESTINATION share/apkm/scripts
    PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_READ WORLD_READ
    COMPONENT scripts
)

# ============================================================================
# TESTS
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    
    add_test(NAME apkm_version COMMAND apkm --version)
    add_test(NAME apkm_help COMMAND apkm --help)
    
    if(BUILD_APSM)
        add_test(NAME apsm_help COMMAND apsm help)
    endif()
    
    if(BUILD_BOOL)
        add_test(NAME bool_version COMMAND bool --version)
        add_test(NAME bool_help COMMAND bool --help)
        
        # Test de compression SELP
        add_test(NAME bool_compress 
            COMMAND sh -c "echo 'test' > /tmp/test.txt && bool -c /tmp/test.txt && bool -x /tmp/test.txt.selp.bool"
        )
    endif()
endif()

# ============================================================================
# PACKAGING CPACK MULTI-PLATEFORME
# ============================================================================
set(CPACK_PACKAGE_NAME "apkm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Gopu.inc")
set(CPACK_PACKAGE_CONTACT "ceoseshell@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION "APKM - Advanced Package Manager with SELP compression")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gopu-inc/apkm-gest")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/images/icon.png")

# Générateurs pour toutes les plateformes
set(CPACK_GENERATOR
    TGZ                         # .tar.gz universel
    TBZ2                        # .tar.bz2
    TXZ                         # .tar.xz
    TZ                          # .tar.Z
    ZIP                         # .zip pour Windows
    STGZ                        # Auto-extractible
    DEB                         # Debian/Ubuntu
    RPM                         # Red Hat/Fedora/CentOS
    NSIS                        # Windows installer
    DragNDrop                   # macOS DMG
    productbuild                # macOS PKG
    IFW                         # Qt Installer Framework
)

# Configurations Debian/Ubuntu
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libcurl4, libssl3, libsqlite3-0, libsodium23, libarchive13, liblz4-1, libzstd1, libyaml-0-2, libjansson4, libargon2-1, libseccomp2, libcap2, zlib1g")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME "apkm-${PROJECT_VERSION}-${APKM_ARCH}.deb")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)

# Configurations RPM
set(CPACK_RPM_PACKAGE_REQUIRES "curl, openssl, sqlite, libsodium, libarchive, lz4, zstd, libyaml, jansson, argon2, libseccomp, libcap, zlib")
set(CPACK_RPM_FILE_NAME "apkm-${PROJECT_VERSION}-${APKM_ARCH}.rpm")
set(CPACK_RPM_PACKAGE_DEBUG ON)
set(CPACK_RPM_PACKAGE_AUTOREQ ON)

# Windows NSIS
set(CPACK_NSIS_DISPLAY_NAME "APKM Package Manager")
set(CPACK_NSIS_HELP_LINK "https://github.com/gopu-inc/apkm-gest")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/gopu-inc/apkm-gest")
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/images/icon.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/images/icon.ico")

# macOS
set(CPACK_BUNDLE_NAME "APKM")
set(CPACK_DMG_VOLUME_NAME "APKM-${PROJECT_VERSION}")
set(CPACK_DMG_FORMAT "UDBZ")
set(CPACK_DMG_DS_STORE "${CMAKE_SOURCE_DIR}/cmake/DS_Store")

# Composants
set(CPACK_COMPONENTS_ALL 
    libraries
    binaries
    headers
    pkgconfig
    documentation
    scripts
)

set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Libraries")
set(CPACK_COMPONENT_LIBRARIES_DESCRIPTION "Shared and static libraries including BOOL/SELP")
set(CPACK_COMPONENT_BINARIES_DISPLAY_NAME "Binaries")
set(CPACK_COMPONENT_BINARIES_DESCRIPTION "APKM, APSM and BOOL executables")
set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "Headers")
set(CPACK_COMPONENT_HEADERS_DESCRIPTION "Development headers for APKM and BOOL")
set(CPACK_COMPONENT_PKGCONFIG_DISPLAY_NAME "pkg-config files")
set(CPACK_COMPONENT_PKGCONFIG_DESCRIPTION "pkg-config configuration files")
set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "Documentation")
set(CPACK_COMPONENT_DOCUMENTATION_DESCRIPTION "User and developer documentation")
set(CPACK_COMPONENT_SCRIPTS_DISPLAY_NAME "Scripts")
set(CPACK_COMPONENT_SCRIPTS_DESCRIPTION "Helper scripts for installation")

# Inclure CPack
include(CPack)

# ============================================================================
# CIBLES PERSONNALISÉES
# ============================================================================

# Cible pour créer les paquets pour toutes les architectures
add_custom_target(packages-all
    COMMAND ${CMAKE_COMMAND} --build . --target package
    COMMAND ${CMAKE_COMMAND} -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchain-x86_64.cmake .. && make && cpack
    COMMAND ${CMAKE_COMMAND} -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchain-i386.cmake .. && make && cpack
    COMMAND ${CMAKE_COMMAND} -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchain-arm64.cmake .. && make && cpack
    COMMAND ${CMAKE_COMMAND} -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchain-armv7.cmake .. && make && cpack
    COMMENT "Building packages for all architectures"
)

# Cible pour nettoyer complètement
add_custom_target(distclean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning build directory"
)

# Cible pour installer tous les composants
add_custom_target(install-all
    COMMAND ${CMAKE_COMMAND} -DCMAKE_INSTALL_COMPONENT=libraries -P cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -DCMAKE_INSTALL_COMPONENT=binaries -P cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -DCMAKE_INSTALL_COMPONENT=headers -P cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -DCMAKE_INSTALL_COMPONENT=pkgconfig -P cmake_install.cmake
    COMMENT "Installing all components"
)

# ============================================================================
# MESSAGES DE FIN
# ============================================================================
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════════╗")
message(STATUS "║     APKM v${PROJECT_VERSION} - Configuration réussie !         ║")
message(STATUS "╚══════════════════════════════════════════════════════════╝")
message(STATUS "")
message(STATUS "📦 Architecture détectée: ${APKM_ARCH}")
message(STATUS "🌐 Zarch Hub: ${ZARCH_HUB_URL}")
message(STATUS "")
message(STATUS "📚 Composants:")
message(STATUS "   • APKM Core: ${CORE_SOURCES}")
message(STATUS "   • APSM Publisher: ${BUILD_APSM}")
message(STATUS "   • BOOL/SELP: ${BUILD_BOOL}")
message(STATUS "")
message(STATUS "🔧 Options de compilation:")
message(STATUS "   • Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "   • LTO: ${ENABLE_LTO}")
message(STATUS "   • Tests: ${BUILD_TESTS}")
message(STATUS "   • Documentation: ${BUILD_DOCS}")
message(STATUS "")
message(STATUS "📦 Formats de packaging disponibles:")
foreach(gen ${CPACK_GENERATOR})
    message(STATUS "   • ${gen}")
endforeach()
message(STATUS "")
message(STATUS "📁 Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "⚙️  Compilateur: ${CMAKE_C_COMPILER}")
message(STATUS "🚀 Tapez 'make' pour compiler")
message(STATUS "📦 Tapez 'cpack' pour générer les paquets")
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════════╗")
message(STATUS "║     BOOL/SELP: Compression 100Mo → 25Mo (75%)           ║")
message(STATUS "║     Signature: [2.0]: SELP bool (c) 2026 003x2022 223222x22 ║")
message(STATUS "╚══════════════════════════════════════════════════════════╝")
