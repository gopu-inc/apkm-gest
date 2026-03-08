#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "apkm.h"
#include <pthread.h>
#include <semaphore.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/prctl.h>
#include <sys/xattr.h>
#include <signal.h>
#include <time.h>
#include <cap-ng.h>
#include <lz4.h>
#include <zstd.h>
#include <yaml.h>
#include <jansson.h>
#include <sqlite3.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <libgen.h>
#include <sys/stat.h>

// ============================================================================
// CONSTANTES
// ============================================================================

#define MAX_PACKAGES 1024
#define MAX_DEPENDENCIES 256
#define CACHE_TTL 3600 // 1 heure
#define GITHUB_API_TIMEOUT 30

#ifndef SIG_BLOCK
#define SIG_BLOCK 0
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#ifndef TFD_CLOEXEC
#define TFD_CLOEXEC 02000000
#endif

// ============================================================================
// STRUCTURES
// ============================================================================

typedef struct {
    void (*function)(void*);
    void* data;
    int subtasks;
    int completion_fd;
} work_item_t;

typedef struct {
    const char* dep_name;
    int* result;
} dep_resolve_arg_t;

typedef struct {
    char name[256];
    char version[64];
    char url[512];
    time_t last_update;
} repo_entry_t;

typedef struct {
    sqlite3* db;
    pthread_mutex_t db_mutex;
    pthread_rwlock_t cache_lock;
    sem_t worker_sem;
    int epoll_fd;
    int signal_fd;
    int timer_fd;
    int inotify_fd;
    int fanotify_fd;
    security_level_t security;
    progress_callback_t progress_cb;
    error_callback_t error_cb;
    log_callback_t log_cb;
    int thread_count;
    bool initialized;
    bool tracing_enabled;
    char* config_path;
    void* signing_key;
    void* cert;
    work_item_t work_queue[1024];
    int queue_size;
    pthread_mutex_t queue_mutex;
    repo_entry_t repositories[32];
    int repo_count;
} apkm_context_t;

// Structure pour la réponse curl
struct curl_response {
    char *data;
    size_t size;
};

static apkm_context_t ctx = {0};

// ============================================================================
// CALLBACKS CURL
// ============================================================================

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}

static size_t response_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct curl_response *resp = (struct curl_response *)userdata;
    size_t total = size * nmemb;
    
    resp->data = realloc(resp->data, resp->size + total + 1);
    if (!resp->data) return 0;
    
    memcpy(resp->data + resp->size, ptr, total);
    resp->size += total;
    resp->data[resp->size] = '\0';
    
    return total;
}

// ============================================================================
// FONCTIONS DE TÉLÉCHARGEMENT GITHUB
// ============================================================================

static int github_download_file(const char *path, const char *output_path) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", GITHUB_RAW_URL, path);
    
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, GITHUB_API_TIMEOUT);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    if (res != CURLE_OK) {
        unlink(output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    return (http_code == 200) ? 0 : -1;
}

static char* github_fetch_string(const char *path) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    
    struct curl_response resp = {0};
    
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", GITHUB_RAW_URL, path);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, GITHUB_API_TIMEOUT);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK && resp.data) {
        return resp.data;
    }
    
    free(resp.data);
    return NULL;
}

// ============================================================================
// BASE DE DONNÉES SQLITE DES PACKAGES
// ============================================================================

static int db_init(void) {
    mkdir(APKM_DB_PATH, 0755);
    
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/packages.db", APKM_DB_PATH);
    
    sqlite3 *db;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    // Table des packages installés
    const char *sql_installed = 
        "CREATE TABLE IF NOT EXISTS installed_packages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT UNIQUE NOT NULL,"
        "version TEXT NOT NULL,"
        "release TEXT,"
        "architecture TEXT,"
        "install_date INTEGER DEFAULT (strftime('%s','now')),"
        "binary_path TEXT,"
        "size INTEGER,"
        "sha256 TEXT,"
        "manifest TEXT"
        ");";
    
    // Table des packages disponibles (cache des dépôts)
    const char *sql_available = 
        "CREATE TABLE IF NOT EXISTS available_packages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "version TEXT NOT NULL,"
        "release TEXT,"
        "architecture TEXT,"
        "description TEXT,"
        "maintainer TEXT,"
        "license TEXT,"
        "url TEXT,"
        "sha256 TEXT,"
        "size INTEGER,"
        "download_url TEXT,"
        "repository TEXT,"
        "last_update INTEGER,"
        "UNIQUE(name, version, architecture)"
        ");";
    
    // Table des dépôts
    const char *sql_repos = 
        "CREATE TABLE IF NOT EXISTS repositories ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT UNIQUE NOT NULL,"
        "url TEXT NOT NULL,"
        "type TEXT DEFAULT 'github',"
        "enabled INTEGER DEFAULT 1,"
        "last_sync INTEGER"
        ");";
    
    // Index pour accélérer les recherches
    const char *sql_index1 = 
        "CREATE INDEX IF NOT EXISTS idx_available_name ON available_packages(name);";
    
    const char *sql_index2 = 
        "CREATE INDEX IF NOT EXISTS idx_available_version ON available_packages(version);";
    
    sqlite3_exec(db, sql_installed, NULL, NULL, NULL);
    sqlite3_exec(db, sql_available, NULL, NULL, NULL);
    sqlite3_exec(db, sql_repos, NULL, NULL, NULL);
    sqlite3_exec(db, sql_index1, NULL, NULL, NULL);
    sqlite3_exec(db, sql_index2, NULL, NULL, NULL);
    
    // Ajouter le dépôt par défaut
    const char *sql_add_repo = 
        "INSERT OR IGNORE INTO repositories (name, url, type) VALUES "
        "('apkm-gest', 'https://github.com/gopu-inc/apkm-gest', 'github');";
    
    sqlite3_exec(db, sql_add_repo, NULL, NULL, NULL);
    
    sqlite3_close(db);
    
    printf("[DB] Database initialized at %s\n", db_path);
    return 0;
}

static int db_sync_from_github(void) {
    printf("[DB] Syncing package database from GitHub...\n");
    
    // Télécharger le fichier DATA.db depuis GitHub
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/apkm_DATA.db");
    
    if (github_download_file("DATA.db", tmp_path) != 0) {
        fprintf(stderr, "[DB] Failed to download DATA.db from GitHub\n");
        return -1;
    }
    
    // Ouvrir la base de données temporaire
    sqlite3 *tmp_db;
    if (sqlite3_open(tmp_path, &tmp_db) != SQLITE_OK) {
        fprintf(stderr, "[DB] Failed to open downloaded database\n");
        unlink(tmp_path);
        return -1;
    }
    
    // Ouvrir la base de données principale
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/packages.db", APKM_DB_PATH);
    
    sqlite3 *main_db;
    if (sqlite3_open(db_path, &main_db) != SQLITE_OK) {
        sqlite3_close(tmp_db);
        unlink(tmp_path);
        return -1;
    }
    
    // Commencer une transaction
    sqlite3_exec(main_db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
    
    // Vider la table available_packages
    sqlite3_exec(main_db, "DELETE FROM available_packages;", NULL, NULL, NULL);
    
    // Copier les données depuis la base temporaire
    sqlite3_stmt *stmt;
    const char *sql_select = "SELECT name, version, release, architecture, "
                              "description, maintainer, license, url, "
                              "sha256, size, download_url FROM packages;";
    
    if (sqlite3_prepare_v2(tmp_db, sql_select, -1, &stmt, NULL) == SQLITE_OK) {
        const char *sql_insert = 
            "INSERT INTO available_packages "
            "(name, version, release, architecture, description, maintainer, "
            "license, url, sha256, size, download_url, last_update) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s','now'));";
        
        sqlite3_stmt *insert_stmt;
        sqlite3_prepare_v2(main_db, sql_insert, -1, &insert_stmt, NULL);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            sqlite3_bind_text(insert_stmt, 1, (const char*)sqlite3_column_text(stmt, 0), -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 2, (const char*)sqlite3_column_text(stmt, 1), -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 3, (const char*)sqlite3_column_text(stmt, 2), -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 4, (const char*)sqlite3_column_text(stmt, 3), -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 5, (const char*)sqlite3_column_text(stmt, 4), -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 6, (const char*)sqlite3_column_text(stmt, 5), -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 7, (const char*)sqlite3_column_text(stmt, 6), -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 8, (const char*)sqlite3_column_text(stmt, 7), -1, SQLITE_STATIC);
            sqlite3_bind_text(insert_stmt, 9, (const char*)sqlite3_column_text(stmt, 8), -1, SQLITE_STATIC);
            sqlite3_bind_int64(insert_stmt, 10, sqlite3_column_int(stmt, 9));
            sqlite3_bind_text(insert_stmt, 11, (const char*)sqlite3_column_text(stmt, 10), -1, SQLITE_STATIC);
            
            sqlite3_step(insert_stmt);
            sqlite3_reset(insert_stmt);
            sqlite3_clear_bindings(insert_stmt);
        }
        
        sqlite3_finalize(insert_stmt);
        sqlite3_finalize(stmt);
    }
    
    // Valider la transaction
    sqlite3_exec(main_db, "COMMIT;", NULL, NULL, NULL);
    
    sqlite3_close(main_db);
    sqlite3_close(tmp_db);
    
    unlink(tmp_path);
    
    printf("[DB] Sync completed successfully\n");
    return 0;
}

static int db_search_packages(const char *pattern, package_t *results, int max_results) {
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/packages.db", APKM_DB_PATH);
    
    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return -1;
    }
    
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "SELECT name, version, release, architecture, description, "
             "maintainer, license, sha256, size, download_url "
             "FROM available_packages WHERE name LIKE '%%%s%%' "
             "OR description LIKE '%%%s%%' LIMIT %d;",
             pattern, pattern, max_results);
    
    sqlite3_stmt *stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW && count < max_results) {
            package_t *pkg = &results[count];
            memset(pkg, 0, sizeof(package_t));
            
            strncpy(pkg->name, (const char*)sqlite3_column_text(stmt, 0), sizeof(pkg->name)-1);
            strncpy(pkg->version, (const char*)sqlite3_column_text(stmt, 1), sizeof(pkg->version)-1);
            
            const char *rel = (const char*)sqlite3_column_text(stmt, 2);
            if (rel) strncpy(pkg->release, rel, sizeof(pkg->release)-1);
            
            const char *arch = (const char*)sqlite3_column_text(stmt, 3);
            if (arch) strncpy(pkg->architecture, arch, sizeof(pkg->architecture)-1);
            
            const char *desc = (const char*)sqlite3_column_text(stmt, 4);
            if (desc) strncpy(pkg->description, desc, sizeof(pkg->description)-1);
            
            const char *maintainer = (const char*)sqlite3_column_text(stmt, 5);
            if (maintainer) strncpy(pkg->maintainer, maintainer, sizeof(pkg->maintainer)-1);
            
            const char *license = (const char*)sqlite3_column_text(stmt, 6);
            if (license) strncpy(pkg->license, license, sizeof(pkg->license)-1);
            
            const char *sha = (const char*)sqlite3_column_text(stmt, 7);
            if (sha) strncpy(pkg->sha256, sha, sizeof(pkg->sha256)-1);
            
            pkg->size = sqlite3_column_int(stmt, 8);
            
            count++;
        }
        sqlite3_finalize(stmt);
    }
    
    sqlite3_close(db);
    return count;
}

static package_t* db_get_package(const char *name, const char *version) {
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/packages.db", APKM_DB_PATH);
    
    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return NULL;
    }
    
    char sql[1024];
    if (version) {
        snprintf(sql, sizeof(sql),
                 "SELECT name, version, release, architecture, description, "
                 "maintainer, license, sha256, size, download_url "
                 "FROM available_packages WHERE name = '%s' AND version = '%s' "
                 "LIMIT 1;", name, version);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT name, version, release, architecture, description, "
                 "maintainer, license, sha256, size, download_url "
                 "FROM available_packages WHERE name = '%s' "
                 "ORDER BY version DESC LIMIT 1;", name);
    }
    
    sqlite3_stmt *stmt;
    package_t *pkg = NULL;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            pkg = calloc(1, sizeof(package_t));
            
            strncpy(pkg->name, (const char*)sqlite3_column_text(stmt, 0), sizeof(pkg->name)-1);
            strncpy(pkg->version, (const char*)sqlite3_column_text(stmt, 1), sizeof(pkg->version)-1);
            
            const char *rel = (const char*)sqlite3_column_text(stmt, 2);
            if (rel) strncpy(pkg->release, rel, sizeof(pkg->release)-1);
            
            const char *arch = (const char*)sqlite3_column_text(stmt, 3);
            if (arch) strncpy(pkg->architecture, arch, sizeof(pkg->architecture)-1);
            
            const char *desc = (const char*)sqlite3_column_text(stmt, 4);
            if (desc) strncpy(pkg->description, desc, sizeof(pkg->description)-1);
            
            const char *maintainer = (const char*)sqlite3_column_text(stmt, 5);
            if (maintainer) strncpy(pkg->maintainer, maintainer, sizeof(pkg->maintainer)-1);
            
            const char *license = (const char*)sqlite3_column_text(stmt, 6);
            if (license) strncpy(pkg->license, license, sizeof(pkg->license)-1);
            
            const char *sha = (const char*)sqlite3_column_text(stmt, 7);
            if (sha) strncpy(pkg->sha256, sha, sizeof(pkg->sha256)-1);
            
            pkg->size = sqlite3_column_int(stmt, 8);
        }
        sqlite3_finalize(stmt);
    }
    
    sqlite3_close(db);
    return pkg;
}

static int db_register_installed(const char *name, const char *version, 
                          const char *release, const char *arch,
                          const char *binary_path) {
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/packages.db", APKM_DB_PATH);
    
    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return -1;
    }
    
    const char *sql = 
        "INSERT OR REPLACE INTO installed_packages "
        "(name, version, release, architecture, binary_path, install_date) "
        "VALUES (?, ?, ?, ?, ?, strftime('%s','now'));";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, version, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, release, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, arch, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, binary_path, -1, SQLITE_STATIC);
        
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    sqlite3_close(db);
    return (rc == SQLITE_OK) ? 0 : -1;
}

static int db_list_installed(package_t *results, int max_results) {
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/packages.db", APKM_DB_PATH);
    
    sqlite3 *db;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return -1;
    }
    
    const char *sql = 
        "SELECT name, version, release, architecture, binary_path, "
        "datetime(install_date, 'unixepoch') FROM installed_packages "
        "ORDER BY name;";
    
    sqlite3_stmt *stmt;
    int count = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW && count < max_results) {
            package_t *pkg = &results[count];
            memset(pkg, 0, sizeof(package_t));
            
            strncpy(pkg->name, (const char*)sqlite3_column_text(stmt, 0), sizeof(pkg->name)-1);
            strncpy(pkg->version, (const char*)sqlite3_column_text(stmt, 1), sizeof(pkg->version)-1);
            
            const char *rel = (const char*)sqlite3_column_text(stmt, 2);
            if (rel) strncpy(pkg->release, rel, sizeof(pkg->release)-1);
            
            const char *arch = (const char*)sqlite3_column_text(stmt, 3);
            if (arch) strncpy(pkg->architecture, arch, sizeof(pkg->architecture)-1);
            
            count++;
        }
        sqlite3_finalize(stmt);
    }
    
    sqlite3_close(db);
    return count;
}

// ============================================================================
// FONCTIONS DE TÉLÉCHARGEMENT DE PACKAGES (STATIC POUR ÉVITER LES DOUBLONS)
// ============================================================================

static int download_package(const char *name, const char *version, const char *output_path) {
    // Chercher le package dans la base
    package_t *pkg = db_get_package(name, version);
    if (!pkg) {
        fprintf(stderr, "[APKM] Package %s %s not found in database\n", name, version);
        return -1;
    }
    
    // Construire l'URL de téléchargement
    char url[512];
    if (strlen(pkg->url) > 0) {
        strncpy(url, pkg->url, sizeof(url)-1);
    } else {
        snprintf(url, sizeof(url), 
                 "https://github.com/gopu-inc/apkm-gest/releases/download/v%s/%s-v%s-%s.%s.tar.bool",
                 version, name, version, pkg->release, pkg->architecture);
    }
    
    printf("[APKM] Downloading %s %s from %s\n", name, version, url);
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[APKM] Download failed: %s\n", curl_easy_strerror(res));
        unlink(output_path);
        curl_easy_cleanup(curl);
        free(pkg);
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (http_code != 200) {
        fprintf(stderr, "[APKM] HTTP error: %ld\n", http_code);
        unlink(output_path);
        free(pkg);
        return -1;
    }
    
    printf("[APKM] Download complete\n");
    free(pkg);
    return 0;
}

// ============================================================================
// FONCTIONS D'INSTALLATION
// ============================================================================

static int extract_package(const char *filepath, const char *dest_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -xzf '%s' -C '%s' 2>/dev/null", filepath, dest_path);
    
    if (system(cmd) == 0) return 0;
    
    snprintf(cmd, sizeof(cmd), "tar -xf '%s' -C '%s' 2>/dev/null", filepath, dest_path);
    if (system(cmd) == 0) return 0;
    
    return -1;
}

static int run_install_script(const char *staging_path, const char *pkg_name) {
    const char *scripts[] = {
        "install.sh", "INSTALL.sh", "post-install.sh", 
        "setup.sh", "configure.sh", NULL
    };
    
    for (int i = 0; scripts[i] != NULL; i++) {
        char script_path[512];
        snprintf(script_path, sizeof(script_path), "%s/%s", staging_path, scripts[i]);
        
        if (access(script_path, F_OK) == 0) {
            printf("[APKM] Executing %s...\n", scripts[i]);
            chmod(script_path, 0755);
            
            char current_dir[1024];
            getcwd(current_dir, sizeof(current_dir));
            chdir(staging_path);
            
            int ret = system(script_path);
            chdir(current_dir);
            
            if (ret == 0) return 0;
        }
    }
    
    // Chercher un binaire direct
    char binary_path[512];
    snprintf(binary_path, sizeof(binary_path), "%s/%s", staging_path, pkg_name);
    if (access(binary_path, F_OK) == 0) {
        printf("[APKM] Installing binary directly\n");
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), 
                 "cp '%s' /usr/local/bin/ && chmod 755 /usr/local/bin/%s",
                 binary_path, pkg_name);
        return system(cmd);
    }
    
    return -1;
}

// ============================================================================
// API PUBLIQUE
// ============================================================================

int apkm_init(security_level_t security, progress_callback_t progress_cb, error_callback_t error_cb) {
    if (ctx.initialized) return -1;
    
    memset(&ctx, 0, sizeof(ctx));
    ctx.security = security;
    ctx.progress_cb = progress_cb;
    ctx.error_cb = error_cb;
    ctx.thread_count = sysconf(_SC_NPROCESSORS_ONLN);
    
    pthread_mutex_init(&ctx.db_mutex, NULL);
    pthread_rwlock_init(&ctx.cache_lock, NULL);
    pthread_mutex_init(&ctx.queue_mutex, NULL);
    sem_init(&ctx.worker_sem, 0, ctx.thread_count * 2);
    
    // Initialiser la base de données
    db_init();
    
    ctx.initialized = true;
    
    return 0;
}

int apkm_install(const char* source) {
    if (!ctx.initialized) apkm_init(SECURITY_MEDIUM, NULL, NULL);
    
    char name[256], version[64], arch[32];
    
    // Parser le format: name@version/arch
    char temp[512];
    strncpy(temp, source, sizeof(temp)-1);
    temp[sizeof(temp)-1] = '\0';
    
    char *at = strchr(temp, '@');
    if (at) {
        *at = '\0';
        strcpy(name, temp);
        
        char *slash = strchr(at+1, '/');
        if (slash) {
            *slash = '\0';
            strcpy(version, at+1);
            strcpy(arch, slash+1);
        } else {
            strcpy(version, at+1);
            strcpy(arch, "x86_64");
        }
    } else {
        strcpy(name, source);
        strcpy(version, "latest");
        strcpy(arch, "x86_64");
    }
    
    printf("[APKM] Installing %s %s (%s)\n", name, version, arch);
    
    // Vérifier si la base de données est à jour
    struct stat st;
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/packages.db", APKM_DB_PATH);
    
    if (stat(db_path, &st) != 0 || time(NULL) - st.st_mtime > 86400) {
        printf("[APKM] Database is old, syncing...\n");
        db_sync_from_github();
    }
    
    // Télécharger le package
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s.tar.bool", name, version);
    
    if (download_package(name, version, tmp_path) != 0) {
        return -1;
    }
    
    // Extraire
    const char *staging = "/tmp/apkm_install";
    mkdir(staging, 0755);
    
    if (extract_package(tmp_path, staging) != 0) {
        fprintf(stderr, "[APKM] Extraction failed\n");
        unlink(tmp_path);
        return -1;
    }
    
    unlink(tmp_path);
    
    // Installer
    if (run_install_script(staging, name) == 0) {
        db_register_installed(name, version, "r0", arch, "/usr/local/bin");
        printf("[APKM] ✅ Installation successful\n");
        printf("[APKM] Try: %s --version\n", name);
    } else {
        printf("[APKM] ❌ Installation failed\n");
    }
    
    // Nettoyer
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", staging);
    system(cmd);
    
    return 0;
}

int apkm_list(void) {
    package_t results[256];
    int count = db_list_installed(results, 256);
    
    printf("[APKM] Installed packages:\n");
    printf("═══════════════════════════════════════════\n");
    printf("%-20s %-12s %-10s\n", "NAME", "VERSION", "ARCH");
    printf("───────────────────────────────────────────\n");
    
    for (int i = 0; i < count; i++) {
        printf(" • %-20s %-12s %-10s\n", 
               results[i].name, results[i].version, results[i].architecture);
    }
    
    printf("═══════════════════════════════════════════\n");
    printf(" Total: %d packages\n", count);
    
    return 0;
}

int apkm_search(const char *pattern, output_format_t format) {
    package_t results[256];
    int count = db_search_packages(pattern, results, 256);
    
    if (format == OUTPUT_JSON) {
        printf("[\n");
        for (int i = 0; i < count; i++) {
            printf("  {\"name\":\"%s\",\"version\":\"%s\",\"arch\":\"%s\"}%s\n",
                   results[i].name, results[i].version, results[i].architecture,
                   i < count-1 ? "," : "");
        }
        printf("]\n");
    } else {
        printf("\n[APKM] Search results for '%s':\n", pattern);
        printf("═══════════════════════════════════════════\n");
        printf("%-20s %-12s %-10s %s\n", "NAME", "VERSION", "ARCH", "DESCRIPTION");
        printf("───────────────────────────────────────────\n");
        
        for (int i = 0; i < count; i++) {
            printf(" • %-20s %-12s %-10s %.50s\n",
                   results[i].name, results[i].version, 
                   results[i].architecture, results[i].description);
        }
        printf("═══════════════════════════════════════════\n");
    }
    
    return 0;
}

int apkm_repos(output_format_t format) {
    // Pour l'instant, juste le dépôt par défaut
    if (format == OUTPUT_JSON) {
        printf("[{\"name\":\"apkm-gest\",\"url\":\"https://github.com/gopu-inc/apkm-gest\",\"type\":\"github\"}]\n");
    } else {
        printf("\n[APKM] Configured repositories:\n");
        printf("═══════════════════════════════════════════\n");
        printf(" • apkm-gest (github) - https://github.com/gopu-inc/apkm-gest\n");
        printf("═══════════════════════════════════════════\n");
    }
    return 0;
}

int apkm_update(output_format_t format) {
    int result = db_sync_from_github();
    
    if (format == OUTPUT_JSON) {
        printf("{\"success\":%s}\n", result == 0 ? "true" : "false");
    } else {
        if (result == 0) {
            printf("[APKM] ✅ Database updated successfully\n");
        } else {
            printf("[APKM] ❌ Database update failed\n");
        }
    }
    
    return result;
}

// ============================================================================
// FONCTIONS DU THREAD WORKER (simplifiées)
// ============================================================================

void* worker_thread(void* arg) {
    (void)arg;
    return NULL;
}

// ============================================================================
// FONCTIONS DE SÉCURITÉ (simplifiées)
// ============================================================================

static int check_vulnerabilities(package_t* pkg, security_level_t level) {
    (void)pkg; (void)level;
    return 0;
}

static void log_vulnerability(package_t* pkg) {
    (void)pkg;
}

static void verify_all_checksums(void) {}

static void scan_rootkits(void) {}

static void check_file_integrity(void) {}

static void analyze_processes(void) {}

static void update_security_metrics(int vulnerabilities) {
    (void)vulnerabilities;
}

static void load_signing_key(void) {}
