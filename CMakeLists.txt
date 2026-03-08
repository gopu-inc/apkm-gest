cmake_minimum_required(VERSION 3.16)
project(apkm VERSION 2.0.0 LANGUAGES C)

# ============================================================================
# CONFIGURATION GÉNÉRALE
# ============================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Options de build
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_APSM "Build APSM tool" ON)
option(BUILD_BOOL "Build BOOL tool" ON)
option(BUILD_TESTS "Build tests" OFF)

# Dossiers de sortie
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Définitions globales
add_definitions(-D_GNU_SOURCE)
add_definitions(-DAPKM_VERSION=\"${PROJECT_VERSION}\")

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
)

# Sources pour APKM (inclut core.c qui a main)
set(APKM_SOURCES
    src/main.c
    src/core.c
    src/download.c
    ${CORE_SOURCES}
)

# Sources pour APSM
set(APSM_SOURCES
    src/aps/apsm.c
    src/aps/auth.c
    src/aps/security.c
)

# Sources pour BOOL
set(BOOL_SOURCES
    src/bools/bool.c
)

# ============================================================================
# BIBLIOTHÈQUE STATIQUE CORE
# ============================================================================

add_library(apkm_core STATIC ${CORE_SOURCES})

target_link_libraries(apkm_core
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
    pthread
    rt
    dl
    m
)

target_include_directories(apkm_core PRIVATE
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

# ============================================================================
# BINAIRES
# ============================================================================

# APKM - Package Manager principal
add_executable(apkm ${APKM_SOURCES})

target_link_libraries(apkm
    apkm_core
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite::SQLite3
    ${JSONC_LDFLAGS}
    pthread
    rt
    dl
    m
)

set_target_properties(apkm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# APSM - Publisher tool
if(BUILD_APSM)
    add_executable(apsm ${APSM_SOURCES})
    
    target_link_libraries(apsm
        apkm_core
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        SQLite::SQLite3
        ${JSONC_LDFLAGS}
        pthread
        rt
        dl
        m
    )
    
    set_target_properties(apsm PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# BOOL - Package builder
if(BUILD_BOOL)
    add_executable(bool ${BOOL_SOURCES})
    
    target_link_libraries(bool
        apkm_core
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        SQLite::SQLite3
        ${JSONC_LDFLAGS}
        pthread
        rt
        dl
        m
    )
    
    set_target_properties(bool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# ============================================================================
# INSTALLATION
# ============================================================================

install(TARGETS apkm
    RUNTIME DESTINATION bin
    COMPONENT applications
)

if(BUILD_APSM)
    install(TARGETS apsm RUNTIME DESTINATION bin)
endif()

if(BUILD_BOOL)
    install(TARGETS bool RUNTIME DESTINATION bin)
endif()

install(FILES 
    ${CMAKE_SOURCE_DIR}/include/apkm.h
    ${CMAKE_SOURCE_DIR}/include/security.h
    ${CMAKE_SOURCE_DIR}/include/sandbox.h
    DESTINATION include/apkm
    COMPONENT development
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
        add_test(NAME bool_help COMMAND bool --help)
    endif()
endif()

# ============================================================================
# PACKAGING
# ============================================================================

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Gopu.inc")
set(CPACK_PACKAGE_DESCRIPTION "APKM - Advanced Package Manager for Alpine Linux")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gopu-inc/apkm-gest")
set(CPACK_PACKAGE_CONTACT "ceoseshell@gmail.com")
set(CPACK_GENERATOR "TGZ;DEB;RPM")
set(CPACK_SOURCE_GENERATOR "TGZ")

include(CPack)

# ============================================================================
# MESSAGES DE FIN
# ============================================================================

message(STATUS "")
message(STATUS "========================================")
message(STATUS "🔧 APKM v${PROJECT_VERSION} Configuration")
message(STATUS "========================================")
message(STATUS "📦 Zarch Hub URL: ${ZARCH_HUB_URL}")
message(STATUS "🌐 API URL: ${ZARCH_API_URL}")
message(STATUS "")
message(STATUS "⚙️  Options:")
message(STATUS "   • Build APSM: ${BUILD_APSM}")
message(STATUS "   • Build BOOL: ${BUILD_BOOL}")
message(STATUS "   • Build Tests: ${BUILD_TESTS}")
message(STATUS "")
message(STATUS "📁 Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "🛠️  Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "")
message(STATUS "📚 Bibliothèques trouvées:")
message(STATUS "   • CURL: ${CURL_VERSION}")
message(STATUS "   • OpenSSL: ${OPENSSL_VERSION}")
message(STATUS "   • SQLite3: ${SQLite3_VERSION}")
message(STATUS "   • json-c: ${JSONC_VERSION}")
message(STATUS "   • libsodium: ${LIBSODIUM_VERSION}")
message(STATUS "   • libarchive: ${LIBARCHIVE_VERSION}")
message(STATUS "   • lz4: ${LZ4_VERSION}")
message(STATUS "   • zstd: ${ZSTD_VERSION}")
message(STATUS "   • yaml: ${YAML_VERSION}")
message(STATUS "   • jansson: ${JANSSON_VERSION}")
message(STATUS "   • argon2: ${ARGON2_VERSION}")
message(STATUS "   • seccomp: ${SECCOMP_VERSION}")
message(STATUS "   • cap-ng: ${CAP_NG_VERSION}")
message(STATUS "   • blake3: ${BLAKE3_VERSION}")
message(STATUS "========================================")
message(STATUS "")
