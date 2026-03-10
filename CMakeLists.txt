cmake_minimum_required(VERSION 3.20)
project(apkm VERSION 2.0.0 LANGUAGES C)

# ============================================================================
# CONFIGURATION
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Chemins importants
set(TOML_INCLUDE_DIR "/usr/local/include")
set(TOML_LIBRARY "/usr/local/lib/libtoml.so")

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${TOML_INCLUDE_DIR})

# ============================================================================
# RECHERCHE DES BIBLIOTHÈQUES
# ============================================================================
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(JSONC REQUIRED json-c)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)
pkg_check_modules(LIBARCHIVE REQUIRED libarchive)

# ============================================================================
# SOURCES
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
    src/bools/selp_info.c
)

# ============================================================================
# BINAIRES
# ============================================================================
add_executable(apkm ${APKM_SOURCES})
target_link_libraries(apkm
    OpenSSL::Crypto
    ZLIB::ZLIB
    CURL::libcurl
    SQLite::SQLite3
    ${JSONC_LDFLAGS}
    ${LIBSODIUM_LDFLAGS}
    ${LIBARCHIVE_LDFLAGS}
    ${TOML_LIBRARY}
    Threads::Threads
    rt dl m
)

add_executable(apsm ${APSM_SOURCES})
target_link_libraries(apsm
    OpenSSL::Crypto
    CURL::libcurl
    Threads::Threads
    rt dl m
)

add_executable(bool ${BOOL_SOURCES})
target_link_libraries(bool
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
    m
)

# ============================================================================
# INSTALLATION
# ============================================================================
install(TARGETS apkm apsm bool RUNTIME DESTINATION bin)

# ============================================================================
# MESSAGE
# ============================================================================
message(STATUS "✅ APKM v${PROJECT_VERSION} - TOML support activé")
