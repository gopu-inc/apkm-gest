cmake_minimum_required(VERSION 3.20)
project(apkm VERSION 2.0.0 LANGUAGES C)

# ============================================================================
# SILENCE ! (supprimer les warnings inutiles)
# ============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_FLAGS_RELEASE "-O3 -flto -DNDEBUG")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")  # Désactive tous les warnings

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
# FLAGS ULTRA PUISSANTS ⚡
# ============================================================================
# Optimisations agressives
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -funroll-loops -ffast-math -fomit-frame-pointer")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ftree-vectorize -fvect-cost-model=unlimited")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto -fwhole-program -fprofile-generate")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -falign-functions=64 -falign-loops=64")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprefetch-loop-arrays -fgcse-sm -fgcse-las")

# Optimisations par architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native -mtune=native -mfpmath=sse")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.2 -maes -mpclmul -mavx -mavx2 -mbmi2")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i.86")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=pentium4 -mtune=generic -msse3")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8.2-a+crypto+dotprod -mtune=cortex-a76")
endif()

# Flags de linkage ultra-optimisés
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-O3 -Wl,--as-needed -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro -Wl,-z,now")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-O3 -Wl,--as-needed")

# ============================================================================
# DÉFINITIONS GLOBALES
# ============================================================================
add_definitions(-D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE)
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
# INCLUDE DIRECTORIES
# ============================================================================
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# ============================================================================
# RECHERCHE DES BIBLIOTHÈQUES
# ============================================================================
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

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
    src/utils.c
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
    src/bools/selp_compress.c
    src/bools/selp_decompress.c
    src/bools/selp_crypto.c
    src/bools/selp_archive.c
    src/bools/selp_verify.c
    src/bools/selp_directory.c
    src/bools/selp_multi.c
    src/bools/selp_list.c
)

# ============================================================================
# BIBLIOTHÈQUES STATIQUES
# ============================================================================
add_library(apkm_static STATIC ${CORE_SOURCES})
set_target_properties(apkm_static PROPERTIES OUTPUT_NAME "apkm" PREFIX "lib")

add_library(apsm_static STATIC ${APSM_SOURCES})
set_target_properties(apsm_static PROPERTIES OUTPUT_NAME "apsm" PREFIX "lib")

add_library(bool_static STATIC ${BOOL_SOURCES})
set_target_properties(bool_static PROPERTIES OUTPUT_NAME "bool" PREFIX "lib")

# ============================================================================
# BIBLIOTHÈQUES PARTAGÉES
# ============================================================================
add_library(apkm_shared SHARED ${CORE_SOURCES})
set_target_properties(apkm_shared PROPERTIES 
    OUTPUT_NAME "apkm" PREFIX "lib"
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_library(apsm_shared SHARED ${APSM_SOURCES})
set_target_properties(apsm_shared PROPERTIES 
    OUTPUT_NAME "apsm" PREFIX "lib"
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_library(bool_shared SHARED ${BOOL_SOURCES})
set_target_properties(bool_shared PROPERTIES 
    OUTPUT_NAME "bool" PREFIX "lib"
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ============================================================================
# BINAIRES
# ============================================================================
add_executable(apkm ${APKM_SOURCES})
target_link_libraries(apkm apkm_shared)
set_target_properties(apkm PROPERTIES OUTPUT_NAME "apkm")

add_executable(apsm ${APSM_SOURCES})
target_link_libraries(apsm apsm_shared)
set_target_properties(apsm PROPERTIES OUTPUT_NAME "apsm")

add_executable(bool ${BOOL_SOURCES})
target_link_libraries(bool bool_shared)
set_target_properties(bool PROPERTIES OUTPUT_NAME "bool")

# ============================================================================
# LINK LIBRARIES (pour tous les targets)
# ============================================================================
foreach(target 
    apkm_static apkm_shared apsm_static apsm_shared 
    bool_static bool_shared apkm apsm bool
)
    target_link_libraries(${target}
        OpenSSL::Crypto
        ZLIB::ZLIB
        CURL::libcurl
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
# INSTALLATION
# ============================================================================
install(TARGETS apkm_static apkm_shared apsm_static apsm_shared bool_static bool_shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(TARGETS apkm apsm bool
    RUNTIME DESTINATION bin
)

install(FILES 
    ${CMAKE_SOURCE_DIR}/include/apkm.h
    ${CMAKE_SOURCE_DIR}/include/core.h
    ${CMAKE_SOURCE_DIR}/include/security.h
    ${CMAKE_SOURCE_DIR}/include/sandbox.h
    ${CMAKE_SOURCE_DIR}/include/zarch.h
    ${CMAKE_SOURCE_DIR}/src/bools/bool.h
    DESTINATION include/apkm
)

# ============================================================================
# TESTS (optionnels)
# ============================================================================
enable_testing()
add_test(NAME apkm_version COMMAND apkm --version)
add_test(NAME bool_help COMMAND bool --help)

# ============================================================================
# PACKAGING CPACK
# ============================================================================
set(CPACK_PACKAGE_NAME "apkm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Gopu.inc")
set(CPACK_PACKAGE_CONTACT "ceoseshell@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION "APKM - Ultra Optimized Package Manager")
set(CPACK_GENERATOR TGZ TBZ2 TXZ TZ ZIP DEB RPM)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libcurl4, libssl3, libsqlite3-0")
set(CPACK_RPM_PACKAGE_REQUIRES "curl, openssl, sqlite")

include(CPack)

# ============================================================================
# MESSAGE UNIQUE DE FIN
# ============================================================================
message(STATUS "✅ APKM v${PROJECT_VERSION} - Build ultra-optimisé prêt ⚡")
