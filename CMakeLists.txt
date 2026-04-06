cmake_minimum_required(VERSION 3.20)
project(apkm VERSION 2.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -fPIC -Wall")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--as-needed -Wl,--gc-sections")

# Détection architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
    set(APKM_ARCH "arm64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8.2-a+crypto")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)")
    set(APKM_ARCH "x86_64")
endif()

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_APSM "Build APSM tool" ON)
option(BUILD_BOOL "Build BOOL tool" ON)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(/usr/local/include)

set(TOML_LIBRARY "/usr/local/lib/libtoml.so")

add_definitions(-D_GNU_SOURCE)
add_definitions(-DAPKM_VERSION=\"${PROJECT_VERSION}\")
add_definitions(-D_FILE_OFFSET_BITS=64)

# URLs
set(ZARCH_HUB_URL "https://gsql-badge.onrender.com")
set(ZARCH_API_URL "${ZARCH_HUB_URL}/v5.2")
add_definitions(-DZARCH_HUB_URL=\"${ZARCH_HUB_URL}\")
add_definitions(-DZARCH_API_URL=\"${ZARCH_API_URL}\")

# Bibliothèques
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

find_library(LIBCAP_LIBRARY NAMES cap)
find_library(BLAKE3_LIBRARY NAMES blake3)

# Sources communes (sans main)
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

# Sources principales (avec main)
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

# BINAIRES UNIQUEMENT (pas de bibliothèques partagées problématiques)
add_executable(apkm ${APKM_SOURCES})
add_executable(apsm ${APSM_SOURCES})
add_executable(bool ${BOOL_SOURCES})

# Macro de linkage
macro(link_package target)
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
        ${TOML_LIBRARY}
        Threads::Threads
        rt dl m
    )
    
    if(LIBCAP_LIBRARY)
        target_link_libraries(${target} ${LIBCAP_LIBRARY})
    endif()
    if(BLAKE3_LIBRARY)
        target_link_libraries(${target} ${BLAKE3_LIBRARY})
    endif()
    
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
        /usr/local/include
    )
endmacro()

link_package(apkm)
link_package(apsm)
link_package(bool)

# Installation
install(TARGETS apkm apsm bool
    RUNTIME DESTINATION bin
)

message(STATUS "")
message(STATUS "==================================================")
message(STATUS "✅ APKM v${PROJECT_VERSION} - Configuration OK")
message(STATUS "==================================================")
message(STATUS "📦 Architecture: ${APKM_ARCH}")
message(STATUS "📦 TOML: ${TOML_LIBRARY}")
message(STATUS "==================================================")
