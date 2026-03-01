# ============================================================================
# APKM - Advanced Package Manager
# Professional CMake Configuration
# Version: 2.0.0
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(apkm VERSION 2.0.0 LANGUAGES C)

# ============================================================================
# Project Information
# ============================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Package information
set(PACKAGE_NAME "apkm")
set(PACKAGE_VENDOR "Gopu.inc")
set(PACKAGE_DESCRIPTION "Advanced Package Manager for Alpine Linux")
set(PACKAGE_HOMEPAGE_URL "https://github.com/gopu-inc/apkm-gest")

# ============================================================================
# Build Types
# ============================================================================

# Release build (default)
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

# Optimized release flags
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -m32 -march=i686 -mtune=generic")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fomit-frame-pointer -funroll-loops")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto -fvisibility=hidden")

# Debug build flags
set(CMAKE_C_FLAGS_DEBUG "-g3 -ggdb -DDEBUG -D_DEBUG -m32")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer")

# Common flags for all builds
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow -Wformat=2 -Wconversion")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes -Wmissing-prototypes")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=2 -fstack-protector-strong")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -fPIE")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPKM_VERSION=\\\"${PROJECT_VERSION}\\\"")

# Architecture-specific optimizations
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2 -mfpmath=sse -ffast-math")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -falign-functions=16 -falign-loops=16")

# ============================================================================
# Directories
# ============================================================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/apkm
)

# ============================================================================
# Find Required Packages
# ============================================================================

# Required libraries
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)

# Libraries via pkg-config
pkg_check_modules(LIBSODIUM REQUIRED libsodium)
pkg_check_modules(LIBARCHIVE REQUIRED libarchive)
pkg_check_modules(LZ4 REQUIRED liblz4)
pkg_check_modules(ZSTD REQUIRED libzstd)
pkg_check_modules(YAML REQUIRED yaml-0.1)
pkg_check_modules(JANSSON REQUIRED jansson)
pkg_check_modules(ARGON2 REQUIRED libargon2)
pkg_check_modules(SECCOMP REQUIRED libseccomp)
pkg_check_modules(CAP_NG REQUIRED libcap-ng)
pkg_check_modules(JSON_C REQUIRED json-c)

# ============================================================================
# Source Files
# ============================================================================

# Core library sources
set(LIB_SOURCES
    src/auth.c
    src/core.c
    src/crypto.c
    src/db.c
    src/download.c
    src/parser.c
    src/resolver.c
    src/sandbox.c
    src/security.c
)

# Main executable sources (exclude tools with their own main)
set(APKM_SOURCES
    ${LIB_SOURCES}
    src/main.c
)

# Tool sources
set(APSM_SOURCES
    src/apsm.c
    src/security.c
    src/auth.c
)

set(BOOL_SOURCES
    src/bool.c
)

# Public headers for installation
set(PUBLIC_HEADERS
    include/apkm.h
    include/sandbox.h
    include/security.h
)

# ============================================================================
# Library Definitions
# ============================================================================

# Static library
add_library(apkm_static STATIC ${LIB_SOURCES})
set_target_properties(apkm_static PROPERTIES
    OUTPUT_NAME apkm
    CLEAN_DIRECT_OUTPUT 1
)

# Shared library
add_library(apkm_shared SHARED ${LIB_SOURCES})
set_target_properties(apkm_shared PROPERTIES
    OUTPUT_NAME apkm
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CLEAN_DIRECT_OUTPUT 1
)

# Link libraries for both
target_link_libraries(apkm_static
    CURL::libcurl
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    ${LIBSODIUM_LDFLAGS}
    ${LIBARCHIVE_LDFLAGS}
    ${LZ4_LDFLAGS}
    ${ZSTD_LDFLAGS}
    ${YAML_LDFLAGS}
    ${JANSSON_LDFLAGS}
    ${ARGON2_LDFLAGS}
    ${SECCOMP_LDFLAGS}
    ${CAP_NG_LDFLAGS}
    ${JSON_C_LDFLAGS}
    pthread
    rt
    dl
    m
)

target_link_libraries(apkm_shared
    CURL::libcurl
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    ${LIBSODIUM_LDFLAGS}
    ${LIBARCHIVE_LDFLAGS}
    ${LZ4_LDFLAGS}
    ${ZSTD_LDFLAGS}
    ${YAML_LDFLAGS}
    ${JANSSON_LDFLAGS}
    ${ARGON2_LDFLAGS}
    ${SECCOMP_LDFLAGS}
    ${CAP_NG_LDFLAGS}
    ${JSON_C_LDFLAGS}
    pthread
    rt
    dl
    m
)

# Add include directories for libraries
target_include_directories(apkm_static PRIVATE
    ${LIBSODIUM_INCLUDE_DIRS}
    ${LIBARCHIVE_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${YAML_INCLUDE_DIRS}
    ${JANSSON_INCLUDE_DIRS}
    ${ARGON2_INCLUDE_DIRS}
    ${SECCOMP_INCLUDE_DIRS}
    ${CAP_NG_INCLUDE_DIRS}
    ${JSON_C_INCLUDE_DIRS}
)

target_include_directories(apkm_shared PRIVATE
    ${LIBSODIUM_INCLUDE_DIRS}
    ${LIBARCHIVE_INCLUDE_DIRS}
    ${LZ4_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${YAML_INCLUDE_DIRS}
    ${JANSSON_INCLUDE_DIRS}
    ${ARGON2_INCLUDE_DIRS}
    ${SECCOMP_INCLUDE_DIRS}
    ${CAP_NG_INCLUDE_DIRS}
    ${JSON_C_INCLUDE_DIRS}
)

# ============================================================================
# Executable Definitions
# ============================================================================

# Main APKM executable
add_executable(apkm ${APKM_SOURCES})
target_link_libraries(apkm apkm_static)
set_target_properties(apkm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# APSM tool
add_executable(apsm ${APSM_SOURCES})
target_link_libraries(apsm apkm_static)
set_target_properties(apsm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# BOOL tool
add_executable(bool ${BOOL_SOURCES})
target_link_libraries(bool apkm_static)
set_target_properties(bool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# Installation
# ============================================================================

# Install binaries
install(TARGETS apkm apsm bool
    RUNTIME DESTINATION bin
    COMPONENT applications
)

# Install libraries
install(TARGETS apkm_static apkm_shared
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    COMPONENT libraries
)

# Install headers
install(FILES ${PUBLIC_HEADERS}
    DESTINATION include/apkm
    COMPONENT development
)

# Install pkg-config file
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/apkm.pc.in
    ${CMAKE_BINARY_DIR}/apkm.pc
    @ONLY
)

install(FILES ${CMAKE_BINARY_DIR}/apkm.pc
    DESTINATION lib/pkgconfig
    COMPONENT development
)

# Install documentation
install(FILES README.md
    DESTINATION share/doc/apkm
    COMPONENT documentation
)

# ============================================================================
# Testing
# ============================================================================

enable_testing()

add_test(NAME version COMMAND apkm --version)
add_test(NAME help COMMAND apkm --help)
add_test(NAME apsm_help COMMAND apsm --help)
add_test(NAME bool_help COMMAND bool --help)

# ============================================================================
# Packaging
# ============================================================================

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR ${PACKAGE_VENDOR})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION ${PACKAGE_DESCRIPTION})
set(CPACK_PACKAGE_HOMEPAGE_URL ${PACKAGE_HOMEPAGE_URL})
set(CPACK_PACKAGE_CONTACT "Mauricio <mauricio@email.com>")

set(CPACK_GENERATOR "TGZ;DEB;RPM")
set(CPACK_SOURCE_GENERATOR "TGZ")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Mauricio")
set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libcurl4, libssl3, libsqlite3-0")

set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")

include(CPack)

# ============================================================================
# Custom Targets
# ============================================================================

# Create documentation
add_custom_target(docs
    COMMAND ${CMAKE_COMMAND} -E echo "Generating documentation..."
    COMMAND doxygen Doxyfile 2>/dev/null || true
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating API documentation"
)

# Clean everything
add_custom_target(distclean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Removing build directory"
)

# ============================================================================
# Print summary
# ============================================================================

message(STATUS "========================================")
message(STATUS "APKM v${PROJECT_VERSION} Configuration")
message(STATUS "========================================")
message(STATUS "Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Compiler:      ${CMAKE_C_COMPILER}")
message(STATUS "Flags:         ${CMAKE_C_FLAGS}")
message(STATUS "========================================")
