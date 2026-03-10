cmake_minimum_required(VERSION 3.20)
project(apkm VERSION 2.0.0 LANGUAGES C)

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

# ============================================================================
# FLAGS DE COMPILATION ULTRA-OPTIMISÉS
# ============================================================================
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -funroll-loops -ffast-math -fomit-frame-pointer")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ftree-vectorize -fvect-cost-model=unlimited")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto -fwhole-program -fprofile-generate")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -falign-functions=64 -falign-loops=64")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprefetch-loop-arrays -fgcse-sm -fgcse-las")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong -D_FORTIFY_SOURCE=2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fpie -pie")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")  # Désactive tous les warnings

# Flags de linkage
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-O3 -Wl,--as-needed -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-O3 -Wl,--as-needed")

# ============================================================================
# DÉTECTION ARCHITECTURE
# ============================================================================
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)")
    set(APKM_ARCH "x86_64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64 -msse4.2 -maes -mpclmul -mavx -mavx2 -mbmi2")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)")
    set(APKM_ARCH "i386")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -msse2")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)")
    set(APKM_ARCH "arm64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8.2-a+crypto+dotprod -mtune=cortex-a76")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
    set(APKM_ARCH "armv7")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfpu=neon -mfloat-abi=hard")
else()
    set(APKM_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# ============================================================================
# OPTIONS DE BUILD
# ============================================================================
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_APSM "Build APSM tool" ON)
option(BUILD_BOOL "Build BOOL tool" ON)
option(BUILD_TESTS "Build tests" ON)
option(ENABLE_LTO "Enable Link Time Optimization" ON)

# ============================================================================
# INCLUDE DIRECTORIES
# ============================================================================
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_BINARY_DIR}/include)

# Chemins TOML (installation manuelle)
set(TOML_INCLUDE_DIR "/usr/local/include")
set(TOML_LIBRARY "/usr/local/lib/libtoml.so")
include_directories(${TOML_INCLUDE_DIR})

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

# TOML (recherche manuelle)
if(EXISTS ${TOML_LIBRARY})
    message(STATUS "Found TOML: ${TOML_LIBRARY}")
else()
    message(FATAL_ERROR "TOML library not found at ${TOML_LIBRARY}")
endif()

# ============================================================================
# LISTE DES SOURCES
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
)

# Sources BOOL (SELP)
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
# BIBLIOTHÈQUES STATIQUES
# ============================================================================
add_library(apkm_static STATIC ${CORE_SOURCES})
set_target_properties(apkm_static PROPERTIES 
    OUTPUT_NAME "apkm" PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)

add_library(apsm_static STATIC ${APSM_SOURCES})
set_target_properties(apsm_static PROPERTIES 
    OUTPUT_NAME "apsm" PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)

add_library(bool_static STATIC ${BOOL_SOURCES})
set_target_properties(bool_static PROPERTIES 
    OUTPUT_NAME "bool" PREFIX "lib"
    POSITION_INDEPENDENT_CODE ON
)

# ============================================================================
# BIBLIOTHÈQUES PARTAGÉES
# ============================================================================
add_library(apkm_shared SHARED ${CORE_SOURCES})
set_target_properties(apkm_shared PROPERTIES 
    OUTPUT_NAME "apkm" PREFIX "lib"
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

add_library(apsm_shared SHARED ${APSM_SOURCES})
set_target_properties(apsm_shared PROPERTIES 
    OUTPUT_NAME "apsm" PREFIX "lib"
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

add_library(bool_shared SHARED ${BOOL_SOURCES})
set_target_properties(bool_shared PROPERTIES 
    OUTPUT_NAME "bool" PREFIX "lib"
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
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
# LINK LIBRARIES (pour tous les targets)
# ============================================================================
foreach(target 
    apkm_static apkm_shared 
    apsm_static apsm_shared 
    bool_static bool_shared
    apkm_bin apsm_bin bool_bin
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
        ${TOML_LIBRARY}
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
        ${TOML_INCLUDE_DIR}
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

install(TARGETS apkm_bin apsm_bin bool_bin
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
set(CPACK_PACKAGE_DESCRIPTION "APKM - Advanced Package Manager with SELP compression")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gopu-inc/apkm-gest")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# Générateurs
set(CPACK_GENERATOR TGZ TBZ2 TXZ TZ ZIP DEB RPM)

# Configurations Debian
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libcurl4, libssl3, libsqlite3-0, libsodium23, libarchive13, liblz4-1, libzstd1, libyaml-0-2, libjansson4, libargon2-1, libseccomp2, libcap2, zlib1g, libtoml1")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME "apkm-${PROJECT_VERSION}-${APKM_ARCH}.deb")

# Configurations RPM
set(CPACK_RPM_PACKAGE_REQUIRES "curl, openssl, sqlite, libsodium, libarchive, lz4, zstd, libyaml, jansson, argon2, libseccomp, libcap, zlib, toml")
set(CPACK_RPM_FILE_NAME "apkm-${PROJECT_VERSION}-${APKM_ARCH}.rpm")

# Windows NSIS
set(CPACK_NSIS_DISPLAY_NAME "APKM Package Manager")
set(CPACK_NSIS_HELP_LINK "https://github.com/gopu-inc/apkm-gest")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/gopu-inc/apkm-gest")
set(CPACK_NSIS_MODIFY_PATH ON)

# macOS
set(CPACK_BUNDLE_NAME "APKM")
set(CPACK_DMG_VOLUME_NAME "APKM-${PROJECT_VERSION}")

include(CPack)

# ============================================================================
# MESSAGE DE FIN
# ============================================================================
message(STATUS "")
message(STATUS "==================================================")
message(STATUS "✅ APKM v${PROJECT_VERSION} - Build ultra-optimisé prêt ⚡")
message(STATUS "==================================================")
message(STATUS "📦 Architecture: ${APKM_ARCH}")
message(STATUS "📦 Zarch Hub: ${ZARCH_HUB_URL}")
message(STATUS "")
message(STATUS "📚 Bibliothèques:")
message(STATUS "   • libapkm.a + libapkm.so")
message(STATUS "   • libapsm.a + libapsm.so")
message(STATUS "   • libbool.a + libbool.so")
message(STATUS "")
message(STATUS "🔧 TOML: ${TOML_LIBRARY}")
message(STATUS "📦 Multi-repo: ✅")
message(STATUS "📦 SELP compression: ✅")
message(STATUS "")
message(STATUS "📦 Formats de packaging:")
foreach(gen ${CPACK_GENERATOR})
    message(STATUS "   • ${gen}")
endforeach()
message(STATUS "==================================================")
