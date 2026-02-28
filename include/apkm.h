#ifndef APKM_H
#define APKM_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define APKM_VERSION "2.0.0"
#define APKM_CODENAME "Quantum Leopard"

// Chemins système
#define APKM_DB_PATH "/var/lib/apkm"
#define APKM_REFS_PATH "/var/lib/apkm/refs"
#define APKM_CACHE_PATH "/var/cache/apkm"
#define APKM_SANDBOX_PATH "/tmp/apkm_sandbox"
#define APKM_CONFIG_PATH "/etc/apkm/config.toml"
#define ALPINE_DB_PATH "/lib/apk/db/installed"

#define REPO_OWNER "gopu-inc"
#define REPO_NAME "apkm-gest"
#define REPO_RAW "https://raw.githubusercontent.com/gopu-inc/apkm-gest/main"
#define REPO_API "https://api.github.com/repos/gopu-inc/apkm-gest"
#define REPO_RELEASES "https://api.github.com/repos/gopu-inc/apkm-gest/releases"
// Formats de sortie
typedef enum {
    OUTPUT_TEXT,
    OUTPUT_JSON,
    OUTPUT_TOML,
    OUTPUT_YAML,
    OUTPUT_CSV,
    OUTPUT_HTML,
    OUTPUT_MARKDOWN,
    OUTPUT_PROTOBUF
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
    char architecture[32];
    char maintainer[256];
    char description[1024];
    char license[64];
    char homepage[256];
    char repository[256];
    char checksum[128];
    char signature[512];
    uint64_t size;
    uint64_t installed_size;
    time_t build_date;
    time_t install_date;
    package_state_t state;
    char** dependencies;
    int dep_count;
    char** conflicts;
    int conflict_count;
    char** provides;
    int provides_count;
    char** recommends;
    int recommends_count;
} package_t;

// Référence de snapshot
typedef struct {
    char id[64];
    time_t timestamp;
    char description[256];
    char checksum[128];
    int package_count;
    char** packages;
} ref_t;

// Métriques système
typedef struct {
    uint64_t total_packages;
    uint64_t total_size;
    uint64_t cache_size;
    uint64_t refs_count;
    time_t last_sync;
    time_t last_audit;
    uint32_t vulnerabilities_found;
    uint32_t security_score;
} system_metrics_t;

// Callbacks pour les événements
typedef void (*progress_callback_t)(const char* operation, int percent);
typedef void (*error_callback_t)(const char* error, int code);
typedef void (*log_callback_t)(const char* message, int level);

// Initialisation et configuration
int apkm_init(security_level_t security, progress_callback_t progress_cb, error_callback_t error_cb);
int apkm_shutdown(void);
int apkm_configure(const char* config_file);

// Gestion des paquets
int apkm_install(const char* package, bool force, bool no_deps);
int apkm_install_local(const char* filepath, bool verify_sig);
int apkm_remove(const char* package, bool purge);
int apkm_update(const char* package);
int apkm_upgrade_all(bool check_deps);
int apkm_downgrade(const char* package, const char* version);

// Recherche et information
package_t* apkm_search(const char* pattern, output_format_t format);
package_t* apkm_info(const char* package, bool full_details);
char** apkm_list_installed(bool include_deps);
char** apkm_list_available(const char* repo);

// Dépendances et résolution
char** apkm_resolve_deps(const char* package, bool recursive);
int apkm_check_conflicts(const char* package);
int apkm_fix_broken(void);
int apkm_autoremove(void);

// Gestion des refs (snapshots)
ref_t* apkm_ref_create(const char* description);
int apkm_ref_restore(const char* ref_id);
int apkm_ref_delete(const char* ref_id);
ref_t** apkm_ref_list(int* count);
int apkm_ref_compare(const char* ref1, const char* ref2);

// Sécurité et audit
int apkm_audit(security_level_t level);
int apkm_audit_package(const char* package);
int apkm_verify_integrity(const char* package);
int apkm_scan_vulnerabilities(void);
int apkm_check_signatures(void);
int apkm_encrypt_config(bool enable);

// Cache et nettoyage
int apkm_clean_cache(int days);
int apkm_clean_orphans(void);
uint64_t apkm_get_cache_size(void);

// Métriques et statistiques
system_metrics_t apkm_get_metrics(void);
char* apkm_generate_report(output_format_t format);

// Fonctions avancées
int apkm_batch_install(char** packages, int count, bool parallel);
int apkm_download(const char* package, const char* dest);
int apkm_export(const char* package, const char* dest);
int apkm_import(const char* filepath);
int apkm_verify_database(void);
int apkm_rebuild_database(void);

// Gestion des dépôts
int apkm_repo_add(const char* name, const char* url, bool verify);
int apkm_repo_remove(const char* name);
int apkm_repo_update(const char* name);
char** apkm_repo_list(void);

// Cryptographie et authentification
char* apkm_token_load(void);
int apkm_token_save(const char* token);
int apkm_token_encrypt(const char* token);
int apkm_token_validate(const char* token);
void apkm_crypto_process(char* data, size_t len, bool encrypt);

// Sandbox et isolation - CORRIGÉ pour correspondre à sandbox.c
int apkm_sandbox_init(const char *target_path);
int apkm_sandbox_create(const char* path, int enable_network, int enable_mount);
int apkm_sandbox_lockdown(void);

// Threading et parallélisation
int apkm_set_threads(int count);
int apkm_get_threads(void);
bool apkm_is_parallel_safe(const char* operation);

// Debug et logging
void apkm_set_log_level(int level);
void apkm_set_log_file(const char* path);
void apkm_enable_trace(bool enable);
void apkm_dump_state(const char* file);

// Fonctions Alpine spécifiques
void sync_alpine_db(output_format_t format);
void resolve_dependencies(const char *staging_path);

#endif // APKM_H
