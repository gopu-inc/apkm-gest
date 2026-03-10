cmake_minimum_required(VERSION 3.16)
project(apkm VERSION 2.0.0 LANGUAGES C)

# ============================================================================
# CONFIGURATION
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Chemins absolus (CORRIGÉS)
set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})
set(INCLUDE_DIR ${PROJECT_ROOT}/include)
set(SRC_DIR ${PROJECT_ROOT}/src)

message(STATUS "Project root: ${PROJECT_ROOT}")
message(STATUS "Include dir: ${INCLUDE_DIR}")
message(STATUS "Source dir: ${SRC_DIR}")

# Include directories
include_directories(${INCLUDE_DIR})
include_directories(${SRC_DIR})

# ============================================================================
# RECHERCHE BIBLIOTHÈQUES
# ============================================================================
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# ============================================================================
# SOURCES APKM
# ============================================================================
set(APKM_SOURCES
    ${SRC_DIR}/main.c
    ${SRC_DIR}/core.c
    ${SRC_DIR}/download.c
    ${SRC_DIR}/auth.c
    ${SRC_DIR}/crypto.c
    ${SRC_DIR}/db.c
    ${SRC_DIR}/parser.c
    ${SRC_DIR}/resolver.c
    ${SRC_DIR}/sandbox.c
    ${SRC_DIR}/security.c
    ${SRC_DIR}/zarch.c
    ${SRC_DIR}/utils.c
)

# ============================================================================
# SOURCES APSM
# ============================================================================
set(APSM_SOURCES
    ${SRC_DIR}/aps/apsm.c
    ${SRC_DIR}/aps/auth.c
    ${SRC_DIR}/aps/security.c
)

# ============================================================================
# SOURCES BOOL
# ============================================================================
set(BOOL_SOURCES
    ${SRC_DIR}/bools/bool.c
    ${SRC_DIR}/bools/selp_compress.c
    ${SRC_DIR}/bools/selp_decompress.c
    ${SRC_DIR}/bools/selp_crypto.c
    ${SRC_DIR}/bools/selp_archive.c
)

# Vérifier que les fichiers existent
foreach(src ${APKM_SOURCES})
    if(NOT EXISTS ${src})
        message(WARNING "APKM source missing: ${src}")
    endif()
endforeach()

foreach(src ${APSM_SOURCES})
    if(NOT EXISTS ${src})
        message(WARNING "APSM source missing: ${src}")
    endif()
endforeach()

foreach(src ${BOOL_SOURCES})
    if(NOT EXISTS ${src})
        message(WARNING "BOOL source missing: ${src}")
    endif()
endforeach()

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

add_executable(apsm ${APSM_SOURCES})
target_link_libraries(apsm
    OpenSSL::Crypto
    CURL::libcurl
    Threads::Threads
    m dl rt
)

add_executable(bool ${BOOL_SOURCES})
target_link_libraries(bool
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
    m
)

# ============================================================================
# TESTS
# ============================================================================
enable_testing()
add_test(NAME apkm_version COMMAND apkm --version)
add_test(NAME apkm_help COMMAND apkm --help)
add_test(NAME apsm_help COMMAND apsm help)
add_test(NAME bool_version COMMAND bool --version)

message(STATUS "✅ Configuration réussie!")
