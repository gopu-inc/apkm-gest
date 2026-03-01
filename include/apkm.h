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
#define ZARCH_HUB_URL "https://zenv-hub.onrender.com"
#define ZARCH_API_URL ZARCH_HUB_URL "/api"
#define ZARCH_PACKAGE_URL ZARCH_HUB_URL "/package/download"

// GitHub uniquement pour DATA.db (liste des dépôts)
#define GITHUB_REPO_OWNER "Mauricio-100"
#define GITHUB_REPO_NAME "apkm-gest"
#define GITHUB_RAW_URL "https://raw.githubusercontent.com/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME "/main"
#define GITHUB_API_URL "https://api.github.com/repos/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME

// Formats de sortie
typedef enum {
    OUTPUT_TEXT,
    OUTPUT_JSON,
    OUTPUT_TOML,
    OUTPUT_YAML,
    OUTPUT_CSV,
    OUTPUT_HTML,
    OUTPUT_MARKDOWN
} output_format_t;

// Niveaux de sécurité
typedef enum {
    SECURITY_LOW,
    SECURITY_MEDIUM,
    SECURITY_HIGH,
    SECURITY_PARANOID
} security_level_t;

// États d'un paquet
typedef enum {
    PKG_STATE_UNKNOWN,
    PKG_STATE_INSTALLED,
    PKG_STATE_PENDING,
    PKG_STATE_FAILED,
    PKG_STATE_BROKEN,
    PKG_STATE_LOCKED,
    PKG_STATE_OBSOLETE
} package_state_t;

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
    package_state_t state;
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
    char download_url[512];
} zarch_package_t;

// Callbacks
typedef void (*progress_callback_t)(const char* operation, int percent);
typedef void (*error_callback_t)(const char* error, int code);

// Prototypes
int apkm_init(security_level_t security, progress_callback_t progress_cb, error_callback_t error_cb);
int apkm_install(const char* source);
int apkm_install_local(const char* filepath);
int apkm_remove(const char* package);
int apkm_list(void);
int apkm_search(const char* pattern, output_format_t format);
int apkm_info(const char* package);
int apkm_update(void);

// Zarch functions
int zarch_download(const char* name, const char* version, const char* arch, const char* output_path);
int zarch_search(const char* query, zarch_package_t* results, int max_results);
int zarch_list_repos(output_format_t format);

// GitHub functions (uniquement pour DATA.db)
int github_fetch_database(char* buffer, size_t buffer_size);
int github_update_database(const char* token, const char* entry);

// Crypto et sécurité
char* load_token_from_home(void);
void btscrypt_process(char *data, int encrypt);
int calculate_sha256(const char *filepath, char *output);

#endif
