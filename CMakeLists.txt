cmake_minimum_required(VERSION 3.16)
project(apkm VERSION 2.0.0 LANGUAGES C)

# ============================================================================
# CONFIGURATION
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# OPTIONS
# ============================================================================
option(BUILD_APSM "Build APSM" ON)
option(BUILD_BOOL "Build BOOL" ON)
option(BUILD_TESTS "Build tests" ON)

# ============================================================================
# RECHERCHE BIBLIOTHÈQUES
# ============================================================================
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

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
    Threads::Threads
    m dl rt
)

if(BUILD_APSM)
    add_executable(apsm ${APSM_SOURCES})
    target_link_libraries(apsm
        OpenSSL::Crypto
        CURL::libcurl
        Threads::Threads
        m dl rt
    )
endif()

if(BUILD_BOOL AND EXISTS ${CMAKE_SOURCE_DIR}/src/bools/selp_compress.c)
    add_executable(bool ${BOOL_SOURCES})
    target_link_libraries(bool
        OpenSSL::Crypto
        ZLIB::ZLIB
        Threads::Threads
        m
    )
endif()

# ============================================================================
# TESTS
# ============================================================================
if(BUILD_TESTS)
    enable_testing()
    add_test(NAME apkm_version COMMAND apkm --version)
    add_test(NAME apkm_help COMMAND apkm --help)
endif()

# ============================================================================
# MESSAGE
# ============================================================================
message(STATUS "✅ Configuration réussie!")
