/* 
package from gopu inc. as tittle indicative open source

 this is lisense is MIT (c) 2026 gopu .inc. 
new production by gopu inc.
team created new release logiciel 
*/

#ifndef APKM_H
#define APKM_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define APKM_VERSION "2.0.0"
#define APKM_CODENAME "Zarch Edition"

// Zarch Hub Configuration
#define ZARCH_HUB_URL "https://gsql-badge.onrender.com"
#define ZARCH_API_URL ZARCH_HUB_URL "/api"
#define ZARCH_PACKAGE_URL ZARCH_HUB_URL "/package/download"

// GitHub uniquement pour DATA.db
#define GITHUB_REPO_OWNER "Mauricio-100"
#define GITHUB_REPO_NAME "apkm-gest"
#define GITHUB_RAW_URL "https://raw.githubusercontent.com/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME "/main"

// Alpine DB Path
#define ALPINE_DB_PATH "/lib/apk/db/installed"
#define APKM_DB_PATH "/var/lib/apkm"
#define APKM_SANDBOX_PATH "/tmp/apkm_sandbox"

// Formats de sortie
typedef enum {
    OUTPUT_TEXT,
    OUTPUT_JSON,
    OUTPUT_TOML,
    OUTPUT_YAML,
    OUTPUT_CSV
} output_format_t;

// Niveaux de sécurité
typedef enum {
    SECURITY_LOW,
    SECURITY_MEDIUM,
    SECURITY_HIGH,
    SECURITY_PARANOID
} security_level_t;

// Structure du token de sécurité
typedef struct {
    char token[512];
    char sha256[128];
    time_t last_update;
    int validated;
} security_token_t;

// Structure d'un paquet
typedef struct {
    char name[128];
    char version[64];
    char release[16];
    char architecture[32];
    char maintainer[256];
    char description[1024];
    char license[64];
    char url[256];
    char sha256[128];
    uint64_t size;
    time_t build_date;
    time_t install_date;
    int state;
    char** dependencies;
    int dep_count;
} package_t;

// Métadonnées Zarch
typedef struct {
    char name[128];
    char version[64];
    char release[16];
    char architecture[32];
    char author[256];
    char sha256[128];
    uint64_t size;
    int downloads;
    char updated_at[32];
} zarch_package_t;

// Callbacks
typedef void (*progress_callback_t)(const char* operation, int percent);
typedef void (*error_callback_t)(const char* error, int code);
typedef void (*log_callback_t)(const char* message, int level);

// Prototypes des fonctions principales
int apkm_init(security_level_t security, progress_callback_t progress_cb, error_callback_t error_cb);
int apkm_install(const char* source);
int apkm_install_local(const char* filepath);
int apkm_list(void);
int apkm_search(const char* query, output_format_t format);
int apkm_repos(output_format_t format);
int apkm_update(output_format_t format);

// Zarch functions
int zarch_download(const char* name, const char* version, const char* arch, const char* output_path);
int zarch_search(const char* query, zarch_package_t* results, int max_results);
int zarch_list_repos(output_format_t format);
int zarch_login(const char *username, const char *password, char *token, size_t token_size);

// GitHub functions
int github_fetch_database(char* buffer, size_t buffer_size);

// Crypto et sécurité
char* load_token_from_home(void);
void btscrypt_process(char *data, int encrypt);
int calculate_sha256(const char *filepath, char *output);
int security_init(void);
int security_get_token(char *token_buffer, size_t buffer_size);
int security_save_token(const security_token_t *token);

// Alpine functions
void sync_alpine_db(output_format_t format);
void resolve_dependencies(const char *staging_path);

// Sandbox functions
int apkm_sandbox_init(const char *target_path);
int apkm_sandbox_create(const char* path, int enable_network, int enable_mount);
int apkm_sandbox_lockdown(void);

#endif
