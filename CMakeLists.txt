cmake_minimum_required(VERSION 3.20)
project(apkm VERSION 2.0.0 LANGUAGES C)

# ============================================================================
# CONFIGURATION DE BASE
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
# FLAGS ULTRA-OPTIMISÉS (STYLE -O999 -DPOSSI99)
# ============================================================================
# Optimisations agressives (équivalent -O999)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -funroll-loops -ffast-math -fomit-frame-pointer")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ftree-vectorize -fvect-cost-model=unlimited")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto -fwhole-program -fprofile-generate")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -falign-functions=64 -falign-loops=64 -falign-jumps=64")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprefetch-loop-arrays -fgcse-sm -fgcse-las")

# Optimisations avancées (POSSI99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPOSSI99=1 -D_GNU_SOURCE=1")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L")

# Optimisations par architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native -mtune=native -mfpmath=sse -msse4.2 -maes -mpclmul")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx -mavx2 -mbmi -mbmi2 -mlzcnt -mmovbe -mabm")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i.86")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=pentium4 -mtune=generic -mfpmath=sse -msse3")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8.2-a+crypto+dotprod -mtune=cortex-a76")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")
endif()

# Flags de linkage ultra-optimisés
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-O3 -Wl,--as-needed -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--sort-common -Wl,--hash-style=gnu -Wl,--build-id")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-O3 -Wl,--as-needed -Wl,--gc-sections")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack")

# ============================================================================
# INCLUDE DIRECTORIES
# ============================================================================
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/include)

# ============================================================================
# DÉFINITIONS GLOBALES
# ============================================================================
add_definitions(-DAPKM_VERSION=\"${PROJECT_VERSION}\")
add_definitions(-DPOSSI99=1 -DULTRA_OPTIMIZED=1)

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
)

add_library(apkm_shared SHARED ${CORE_SOURCES})
set_target_properties(apkm_shared PROPERTIES 
    OUTPUT_NAME "apkm"
    PREFIX "lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# LIBAPSM
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

# LIBBOOL
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
# LIENS AVEC BIBLIOTHÈQUES EXTERNES
# ============================================================================
foreach(target 
    apkm_static apkm_shared apsm_static apsm_shared bool_static bool_shared
    apkm_bin apsm_bin bool_bin
)
    target_link_libraries(${target}
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
    )
endforeach()

# ============================================================================
# FICHIERS PKG-CONFIG
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
# PACKAGING CPACK
# ============================================================================
set(CPACK_PACKAGE_NAME "apkm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Gopu.inc")
set(CPACK_PACKAGE_CONTACT "ceoseshell@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION "APKM - Ultra Optimized Package Manager")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gopu-inc/apkm-gest")

# Licence (optionnel - commenter si pas de fichier)
# set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# Générateurs
set(CPACK_GENERATOR TGZ TBZ2 TXZ TZ ZIP DEB RPM)

# Configurations Debian
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libcurl4, libssl3, libsqlite3-0")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# Configurations RPM
set(CPACK_RPM_PACKAGE_REQUIRES "curl, openssl, sqlite")

include(CPack)

# ============================================================================
# MESSAGE DE FIN (MINIMAL)
# ============================================================================
message(STATUS "✅ APKM v${PROJECT_VERSION} - Ultra Optimized Build")
message(STATUS "   Arch: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "   Flags: -O3 -DPOSSI99 -march=native")
