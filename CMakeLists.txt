cmake_minimum_required(VERSION 3.20)
project(apkm VERSION 2.0.0 LANGUAGES C)

# ============================================================================
# CONFIGURATION GLOBALE AVEC FLAGS AVANCÉS
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Dossiers de sortie
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# CFLAGS AVANCÉS (Optimisations, sécurité, warnings)
# ============================================================================
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow -Wformat=2 -Wconversion")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes -Wmissing-prototypes")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror=implicit-function-declaration")

# Optimisations
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -march=native -mtune=native -flto")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -O2 -g -flto")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g3 -ggdb -fno-omit-frame-pointer -fsanitize=address,undefined")

# Sécurité
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fpie -pie")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack")

# ============================================================================
# LDFLAGS AVANCÉS
# ============================================================================
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--as-needed -Wl,--no-undefined")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--as-needed -Wl,--no-undefined")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-O1 -Wl,--sort-common -Wl,--hash-style=gnu")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-O1 -Wl,--sort-common -Wl,--hash-style=gnu")

# ============================================================================
# DÉTECTION ARCHITECTURE
# ============================================================================
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)")
    set(APKM_ARCH "x86_64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64 -msse4.2 -maes")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)")
    set(APKM_ARCH "i386")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -msse2")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
    set(APKM_ARCH "arm64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a+crypto")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
    set(APKM_ARCH "armv7")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfpu=neon")
else()
    set(APKM_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# ============================================================================
# INCLUDE DIRECTORIES
# ============================================================================
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)

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
# BIBLIOTHÈQUES
# ============================================================================

# LIBAPKM
add_library(apkm_static STATIC ${CORE_SOURCES})
set_target_properties(apkm_static PROPERTIES 
    OUTPUT_NAME "apkm"
    PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)

add_library(apkm_shared SHARED ${CORE_SOURCES})
set_target_properties(apkm_shared PROPERTIES 
    OUTPUT_NAME "apkm"
    PREFIX "lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

# LIBAPSM
add_library(apsm_static STATIC ${APSM_SOURCES})
set_target_properties(apsm_static PROPERTIES 
    OUTPUT_NAME "apsm"
    PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)

add_library(apsm_shared SHARED ${APSM_SOURCES})
set_target_properties(apsm_shared PROPERTIES 
    OUTPUT_NAME "apsm"
    PREFIX "lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

# LIBBOOL
add_library(bool_static STATIC ${BOOL_SOURCES})
set_target_properties(bool_static PROPERTIES 
    OUTPUT_NAME "bool"
    PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)

add_library(bool_shared SHARED ${BOOL_SOURCES})
set_target_properties(bool_shared PROPERTIES 
    OUTPUT_NAME "bool"
    PREFIX "lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

# ============================================================================
# BINAIRES
# ============================================================================
add_executable(apkm_bin ${APKM_SOURCES})
target_link_libraries(apkm_bin apkm_shared)
set_target_properties(apkm_bin PROPERTIES OUTPUT_NAME "apkm")

add_executable(apsm_bin ${APSM_SOURCES})
target_link_libraries(apsm_bin apsm_shared)
set_target_properties(apsm_bin PROPERTIES OUTPUT_NAME "apsm")

add_executable(bool_bin ${BOOL_SOURCES})
target_link_libraries(bool_bin bool_shared)
set_target_properties(bool_bin PROPERTIES OUTPUT_NAME "bool")

# ============================================================================
# LINK LIBRARIES (avec flags avancés)
# ============================================================================
foreach(lib_target 
    apkm_static apkm_shared 
    apsm_static apsm_shared 
    bool_static bool_shared
    apkm_bin apsm_bin bool_bin
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
    
    # Ajouter les flags de compilation
    target_compile_options(${lib_target} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wformat=2 -Wconversion
        -Wstrict-prototypes -Wmissing-prototypes
    )
endforeach()

# ============================================================================
# GÉNÉRATION FICHIERS PC
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
install(TARGETS apkm_static apkm_shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(TARGETS apsm_static apsm_shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(TARGETS bool_static bool_shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(TARGETS apkm_bin apsm_bin bool_bin
    RUNTIME DESTINATION bin
)

install(FILES 
    ${CMAKE_SOURCE_DIR}/include/apkm.h
    ${CMAKE_SOURCE_DIR}/include/core.h
    ${CMAKE_SOURCE_DIR}/include/security.h
    ${CMAKE_SOURCE_DIR}/include/sandbox.h
    ${CMAKE_SOURCE_DIR}/include/zarch.h
    DESTINATION include/apkm
)

install(FILES 
    ${CMAKE_BINARY_DIR}/apkm.pc
    ${CMAKE_BINARY_DIR}/apsm.pc
    ${CMAKE_BINARY_DIR}/bool.pc
    DESTINATION lib/pkgconfig
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
# CPACK PACKAGING (MAINTENANT CORRECT)
# ============================================================================
set(CPACK_PACKAGE_NAME "apkm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Gopu.inc")
set(CPACK_PACKAGE_CONTACT "ceoseshell@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION "APKM - Advanced Package Manager")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gopu-inc/apkm-gest")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# Générateurs pour toutes les plateformes
set(CPACK_GENERATOR
    TGZ                        # .tar.gz
    TBZ2                       # .tar.bz2
    TXZ                        # .tar.xz
    TZ                         # .tar.Z
    ZIP                        # .zip
    STGZ                       # Auto-extractible
    DEB                        # Debian/Ubuntu
    RPM                        # Red Hat/Fedora
    NSIS                       # Windows
    DragNDrop                  # macOS DMG
    Bundle                     # macOS Bundle
    productbuild               # macOS PKG
)

# Configurations Debian
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libcurl4, libssl3, libsqlite3-0, libsodium23, libarchive13, liblz4-1, libzstd1, libyaml-0-2, libjansson4, libargon2-1, libseccomp2, libcap2")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME "apkm-${PROJECT_VERSION}-${APKM_ARCH}.deb")

# Configurations RPM
set(CPACK_RPM_PACKAGE_REQUIRES "curl, openssl, sqlite, libsodium, libarchive, lz4, zstd, libyaml, jansson, argon2, libseccomp, libcap")
set(CPACK_RPM_FILE_NAME "apkm-${PROJECT_VERSION}-${APKM_ARCH}.rpm")

# Windows
set(CPACK_NSIS_DISPLAY_NAME "APKM Package Manager")
set(CPACK_NSIS_HELP_LINK "https://github.com/gopu-inc/apkm-gest")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/gopu-inc/apkm-gest")
set(CPACK_NSIS_MODIFY_PATH ON)

# macOS
set(CPACK_BUNDLE_NAME "APKM")
set(CPACK_DMG_VOLUME_NAME "APKM-${PROJECT_VERSION}")

# Composants (sans utiliser cpack_add_component)
set(CPACK_COMPONENTS_ALL 
    libraries
    binaries
    headers
    pkgconfig
)

# Noms des composants
set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Libraries")
set(CPACK_COMPONENT_LIBRARIES_DESCRIPTION "Shared and static libraries")
set(CPACK_COMPONENT_BINARIES_DISPLAY_NAME "Binaries")
set(CPACK_COMPONENT_BINARIES_DESCRIPTION "Executable programs")
set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "Headers")
set(CPACK_COMPONENT_HEADERS_DESCRIPTION "Development headers")
set(CPACK_COMPONENT_PKGCONFIG_DISPLAY_NAME "pkg-config files")
set(CPACK_COMPONENT_PKGCONFIG_DESCRIPTION "pkg-config configuration files")

# Inclure CPack à la fin
include(CPack)

# ============================================================================
# MESSAGES DE FIN
# ============================================================================
message(STATUS "")
message(STATUS "==================================================")
message(STATUS "🚀 APKM v${PROJECT_VERSION} - Configuration réussie!")
message(STATUS "==================================================")
message(STATUS "📦 Architecture détectée: ${APKM_ARCH}")
message(STATUS "📦 Zarch Hub: ${ZARCH_HUB_URL}")
message(STATUS "")
message(STATUS "📚 Bibliothèques générées:")
message(STATUS "   • libapkm.so (shared) + libapkm.a (static)")
message(STATUS "   • libapsm.so (shared) + libapsm.a (static)")
message(STATUS "   • libbool.so (shared) + libbool.a (static)")
message(STATUS "")
message(STATUS "🔧 Flags d'optimisation:")
message(STATUS "   • CFLAGS: ${CMAKE_C_FLAGS}")
message(STATUS "   • LDFLAGS: ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "")
message(STATUS "📦 Formats de packaging disponibles:")
foreach(gen ${CPACK_GENERATOR})
    message(STATUS "   • ${gen}")
endforeach()
message(STATUS "==================================================")
