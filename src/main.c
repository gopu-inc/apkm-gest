#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <libgen.h>
#include <time.h>
#include <dirent.h>
#include <sqlite3.h>
#include "apkm.h"
#include <json-c/json.h>

#define ZARCH_HUB_URL "https://gsql-badge.onrender.com"
#define ZARCH_API_URL ZARCH_HUB_URL "/v5.2"
#define APKM_DB_PATH "/usr/local/share/apkm/database"
#define APKM_CACHE_PATH "/usr/local/share/apkm/cache"
#define APKM_CONF_PATH "/etc/apkm/repositories.conf"

struct curl_response {
    char *data;
    size_t size;
};

static int debug_mode = 0;
static int quiet_mode = 0;
static int interactive = 1;
static sqlite3 *global_db = NULL;

// Structure pour stocker les infos d'un package
typedef struct {
    char name[256];
    char version[64];
    char release[16];
    char arch[32];
    char author[256];
    char description[1024];
    char sha256[128];
    char url[512];
    char repository[128];
    char license[64];
    char maintainer[256];
    char dependencies[1024];
    int downloads;
    long size;
    time_t install_date;
    int installed;
} package_info_t;

// Structure pour les repositories
typedef struct {
    char name[128];
    char url[256];
    int enabled;
    int priority;
} repository_t;

#define MAX_REPOS 32
static repository_t repositories[MAX_REPOS];
static int repo_count = 0;

// ============================================================================
// FONCTIONS UTILITAIRES AVANCÉES
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

void debug_print(const char *format, ...) {
    if (!debug_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[90m[DEBUG] ");
    vprintf(format, args);
    printf("\033[0m\n");
    va_end(args);
    fflush(stdout);
}

void print_info(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[36mℹ️  \033[0m");
    vprintf(format, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

void print_success(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[32m✅ \033[0m");
    vprintf(format, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

void print_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "\033[31m❌ \033[0m");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
    fflush(stderr);
}

void print_warning(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[33m⚠️  \033[0m");
    vprintf(format, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

void print_step(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[35m▶ \033[0m");
    vprintf(format, args);
    printf("...\n");
    va_end(args);
    fflush(stdout);
}

// ============================================================================
// GESTION DE LA BASE DE DONNÉES SQLITE
// ============================================================================

int db_init(void) {
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/packages.db", APKM_DB_PATH);
    
    // Créer le répertoire si nécessaire
    mkdir(APKM_DB_PATH, 0755);
    
    sqlite3 *db;
    int rc = sqlite3_open(db_path, &db);
    if (rc != SQLITE_OK) {
        print_error("Cannot open database: %s", sqlite3_errmsg(db));
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
        "repository TEXT"
        ");";
    
    // Table des packages disponibles (cache)
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
        "author TEXT,"
        "sha256 TEXT,"
        "size INTEGER,"
        "downloads INTEGER DEFAULT 0,"
        "repository TEXT,"
        "last_update INTEGER,"
        "UNIQUE(name, version, release, architecture)"
        ");";
    
    // Table des repositories
    const char *sql_repos = 
        "CREATE TABLE IF NOT EXISTS repositories ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT UNIQUE NOT NULL,"
        "url TEXT NOT NULL,"
        "enabled INTEGER DEFAULT 1,"
        "priority INTEGER DEFAULT 10,"
        "last_sync INTEGER"
        ");";
    
    // Index pour les performances
    sqlite3_exec(db, sql_installed, NULL, NULL, NULL);
    sqlite3_exec(db, sql_available, NULL, NULL, NULL);
    sqlite3_exec(db, sql_repos, NULL, NULL, NULL);
    
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_available_name ON available_packages(name);", NULL, NULL, NULL);
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_available_version ON available_packages(version);", NULL, NULL, NULL);
    
    // Ajouter le repository par défaut
    const char *sql_add_repo = 
        "INSERT OR IGNORE INTO repositories (name, url, priority) VALUES "
        "('zarch-hub', 'https://gsql-badge.onrender.com', 5);";
    
    sqlite3_exec(db, sql_add_repo, NULL, NULL, NULL);
    
    global_db = db;
    print_info("Database initialized at %s", db_path);
    return 0;
}

int db_close(void) {
    if (global_db) {
        sqlite3_close(global_db);
        global_db = NULL;
    }
    return 0;
}

int db_register_installed(package_info_t *info) {
    if (!global_db) return -1;
    
    const char *sql = 
        "INSERT OR REPLACE INTO installed_packages "
        "(name, version, release, architecture, binary_path, size, sha256, repository, install_date) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, strftime('%s','now'));";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(global_db, sql, -1, &stmt, NULL);
    
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, info->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, info->version, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, info->release, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, info->arch, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, "/usr/local/bin", -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 6, info->size);
        sqlite3_bind_text(stmt, 7, info->sha256, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, info->repository, -1, SQLITE_STATIC);
        
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE) ? 0 : -1;
    }
    
    return -1;
}

int db_is_installed(const char *name, char *version, size_t version_size) {
    if (!global_db) return 0;
    
    const char *sql = "SELECT version FROM installed_packages WHERE name = ?;";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(global_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (version) {
                strncpy(version, (const char*)sqlite3_column_text(stmt, 0), version_size - 1);
                version[version_size - 1] = '\0';
            }
            sqlite3_finalize(stmt);
            return 1;
        }
        sqlite3_finalize(stmt);
    }
    
    return 0;
}

int db_remove_installed(const char *name) {
    if (!global_db) return -1;
    
    const char *sql = "DELETE FROM installed_packages WHERE name = ?;";
    sqlite3_stmt *stmt;
    
    if (sqlite3_prepare_v2(global_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE) ? 0 : -1;
    }
    
    return -1;
}

// ============================================================================
// GESTION DES REPOSITORIES
// ============================================================================

int load_repositories(void) {
    FILE *f = fopen(APKM_CONF_PATH, "r");
    if (!f) {
        // Créer le fichier par défaut
        mkdir("/etc/apkm", 0755);
        f = fopen(APKM_CONF_PATH, "w");
        if (f) {
            fprintf(f, "# APKM Repositories\n");
            fprintf(f, "# Format: name url [priority]\n");
            fprintf(f, "zarch-hub https://gsql-badge.onrender.com 5\n");
            fclose(f);
        }
        f = fopen(APKM_CONF_PATH, "r");
    }
    
    if (!f) return -1;
    
    char line[512];
    repo_count = 0;
    
    while (fgets(line, sizeof(line), f) && repo_count < MAX_REPOS) {
        // Ignorer les commentaires
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char name[128], url[256];
        int priority = 10;
        
        if (sscanf(line, "%127s %255s %d", name, url, &priority) >= 2) {
            strcpy(repositories[repo_count].name, name);
            strcpy(repositories[repo_count].url, url);
            repositories[repo_count].enabled = 1;
            repositories[repo_count].priority = priority;
            repo_count++;
            debug_print("Loaded repository: %s (%s) priority %d", name, url, priority);
        }
    }
    
    fclose(f);
    return repo_count;
}

// ============================================================================
// RECHERCHE DE PACKAGE (AMÉLIORÉE)
// ============================================================================

int search_package(const char *name, package_info_t *info) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    // Essayer d'abord les repositories locaux
    for (int i = 0; i < repo_count; i++) {
        if (!repositories[i].enabled) continue;
        
        char url[512];
        snprintf(url, sizeof(url), "%s/v5.2/package/%s", repositories[i].url, name);
        
        debug_print("Trying repository %s: %s", repositories[i].name, url);
        
        struct curl_response resp = {0};
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
        
        CURLcode res = curl_easy_perform(curl);
        
        if (res == CURLE_OK && resp.data) {
            struct json_object *parsed = json_tokener_parse(resp.data);
            if (parsed) {
                struct json_object *package_obj;
                if (json_object_object_get_ex(parsed, "package", &package_obj)) {
                    // Package trouvé !
                    struct json_object *tmp;
                    
                    strcpy(info->name, name);
                    strcpy(info->repository, repositories[i].name);
                    
                    if (json_object_object_get_ex(package_obj, "version", &tmp))
                        strcpy(info->version, json_object_get_string(tmp));
                    if (json_object_object_get_ex(package_obj, "release", &tmp))
                        strcpy(info->release, json_object_get_string(tmp));
                    else
                        strcpy(info->release, "r0");
                    if (json_object_object_get_ex(package_obj, "arch", &tmp))
                        strcpy(info->arch, json_object_get_string(tmp));
                    else
                        strcpy(info->arch, "x86_64");
                    if (json_object_object_get_ex(package_obj, "author", &tmp))
                        strcpy(info->author, json_object_get_string(tmp));
                    if (json_object_object_get_ex(package_obj, "description", &tmp))
                        strcpy(info->description, json_object_get_string(tmp));
                    if (json_object_object_get_ex(package_obj, "license", &tmp))
                        strcpy(info->license, json_object_get_string(tmp));
                    if (json_object_object_get_ex(package_obj, "sha256", &tmp))
                        strcpy(info->sha256, json_object_get_string(tmp));
                    if (json_object_object_get_ex(package_obj, "size", &tmp))
                        info->size = json_object_get_int(tmp);
                    if (json_object_object_get_ex(package_obj, "downloads", &tmp))
                        info->downloads = json_object_get_int(tmp);
                    
                    // Construire l'URL de téléchargement
                    snprintf(info->url, sizeof(info->url),
                            "%s/package/download/public/%s/%s/%s/%s",
                            repositories[i].url, name, info->version, 
                            info->release, info->arch);
                    
                    json_object_put(parsed);
                    free(resp.data);
                    curl_easy_cleanup(curl);
                    
                    debug_print("Found in repository %s: %s %s-%s", 
                               info->repository, name, info->version, info->release);
                    return 0;
                }
                json_object_put(parsed);
            }
            free(resp.data);
        }
    }
    
    curl_easy_cleanup(curl);
    return -1;
}

// ============================================================================
// TÉLÉCHARGEMENT AVEC BARRE DE PROGRESSION
// ============================================================================

struct progress_data {
    char name[256];
    double last_percent;
    time_t last_time;
    curl_off_t last_downloaded;
    double speed;
};

static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, 
                              curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    
    struct progress_data *prog = (struct progress_data *)clientp;
    
    if (dltotal > 0 && !quiet_mode) {
        double percent = (double)dlnow / (double)dltotal * 100.0;
        
        time_t now = time(NULL);
        if (now > prog->last_time) {
            curl_off_t diff = dlnow - prog->last_downloaded;
            prog->speed = (double)diff / (now - prog->last_time);
            prog->last_downloaded = dlnow;
            prog->last_time = now;
        }
        
        if (percent - prog->last_percent >= 2.0 || percent >= 100.0) {
            int bar_width = 40;
            int pos = (int)(percent * bar_width / 100.0);
            
            printf("\r\033[36m📥 Downloading %s: [", prog->name);
            for (int i = 0; i < bar_width; i++) {
                if (i < pos) printf("=");
                else if (i == pos && percent < 100.0) printf(">");
                else printf(" ");
            }
            
            if (percent >= 100.0) {
                printf("] %3.0f%% - Complete        \n", percent);
            } else {
                printf("] %3.0f%% - %.1f KB/s      ", percent, prog->speed / 1024.0);
            }
            fflush(stdout);
            
            prog->last_percent = percent;
        }
    }
    return 0;
}

int download_package(package_info_t *info, const char *output_path) {
    debug_print("Downloading from: %s", info->url);
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        debug_print("Cannot open output file: %s", output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    struct progress_data prog;
    strncpy(prog.name, info->name, sizeof(prog.name) - 1);
    prog.last_percent = 0;
    prog.last_time = time(NULL);
    prog.last_downloaded = 0;
    prog.speed = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, info->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
    
    if (!quiet_mode && !debug_mode) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);
    }
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || http_code != 200) {
        debug_print("Download failed: %s", curl_easy_strerror(res));
        unlink(output_path);
        return -1;
    }
    
    // Vérifier la taille
    struct stat st;
    if (stat(output_path, &st) != 0 || st.st_size == 0) {
        unlink(output_path);
        return -1;
    }
    
    // Vérifier SHA256
    if (strlen(info->sha256) > 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "sha256sum %s | cut -d' ' -f1", output_path);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char sha[256] = {0};
            if (fgets(sha, sizeof(sha), fp)) {
                sha[strcspn(sha, "\n")] = 0;
                if (strcmp(sha, info->sha256) != 0) {
                    print_warning("SHA256 mismatch! Expected: %s", info->sha256);
                    print_warning("Got: %s", sha);
                    print_warning("File may be corrupted");
                } else {
                    debug_print("SHA256 verification passed");
                }
            }
            pclose(fp);
        }
    }
    
    return 0;
}

// ============================================================================
// INSTALLATION INTELLIGENTE
// ============================================================================

int extract_package(const char *package_path, const char *extract_dir) {
    char cmd[1024];
    
    mkdir(extract_dir, 0755);
    
    // Essayer différents formats
    const char *tar_options[] = {
        "tar -xzf '%s' -C '%s' 2>/dev/null",
        "tar -xf '%s' -C '%s' 2>/dev/null",
        "tar -xzf '%s' -C '%s' --strip-components=1 2>/dev/null",
        NULL
    };
    
    for (int i = 0; tar_options[i] != NULL; i++) {
        snprintf(cmd, sizeof(cmd), tar_options[i], package_path, extract_dir);
        debug_print("Trying extraction: %s", cmd);
        
        if (system(cmd) == 0) {
            // Vérifier que des fichiers ont été extraits
            snprintf(cmd, sizeof(cmd), "ls -A '%s' | wc -l", extract_dir);
            FILE *fp = popen(cmd, "r");
            if (fp) {
                int count = 0;
                fscanf(fp, "%d", &count);
                pclose(fp);
                if (count > 0) return 0;
            }
        }
    }
    
    return -1;
}

int resolve_dependencies(package_info_t *info) {
    if (strlen(info->dependencies) == 0) return 0;
    
    print_step("Checking dependencies");
    
    // Parser les dépendances (séparées par ';' ou ',')
    char deps[1024];
    strcpy(deps, info->dependencies);
    
    char *token = strtok(deps, ";, ");
    while (token) {
        if (strlen(token) > 0) {
            // Nettoyer le token
            char *start = token;
            while (*start == ' ' || *start == '\t') start++;
            char *end = start + strlen(start) - 1;
            while (end > start && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
            end[1] = '\0';
            
            if (strlen(start) > 0) {
                // Vérifier si déjà installé
                char installed_version[64] = "";
                if (db_is_installed(start, installed_version, sizeof(installed_version))) {
                    print_info("  ✓ %s already installed (%s)", start, installed_version);
                } else {
                    print_info("  ⚡ Need to install: %s", start);
                    // Ici on pourrait lancer l'installation récursive
                    // Pour l'instant on prévient juste
                    print_warning("Dependency %s not installed", start);
                }
            }
        }
        token = strtok(NULL, ";, ");
    }
    
    return 0;
}

int find_binary_and_install(const char *extract_dir, const char *binary_name) {
    char cmd[1024];
    char found_path[512] = {0};
    
    // Chercher le binaire dans l'arborescence
    snprintf(cmd, sizeof(cmd), 
             "find '%s' -type f -name '%s' -o -name '%s' -o -executable -name '%s' 2>/dev/null | head -1",
             extract_dir, binary_name, binary_name, binary_name);
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        if (fgets(found_path, sizeof(found_path), fp)) {
            found_path[strcspn(found_path, "\n")] = 0;
            debug_print("Found binary at: %s", found_path);
            
            // Copier vers /usr/local/bin
            snprintf(cmd, sizeof(cmd), "cp '%s' /usr/local/bin/ && chmod 755 /usr/local/bin/%s", 
                     found_path, binary_name);
            if (system(cmd) == 0) {
                pclose(fp);
                return 0;
            }
        }
        pclose(fp);
    }
    
    // Si pas trouvé, copier tous les exécutables
    snprintf(cmd, sizeof(cmd), 
             "find '%s' -type f -executable -exec cp {} /usr/local/bin/ \\; 2>/dev/null", 
             extract_dir);
    system(cmd);
    
    // Vérifier si notre binaire a été copié
    snprintf(cmd, sizeof(cmd), "ls /usr/local/bin/%s 2>/dev/null", binary_name);
    return (system(cmd) == 0) ? 0 : -1;
}

int run_install_scripts(const char *extract_dir) {
    const char *scripts[] = {
        "install.sh", "INSTALL.sh", "post-install.sh", 
        "setup.sh", "configure.sh", "APKMBUILD", ".INSTALL", NULL
    };
    
    char current_dir[1024];
    getcwd(current_dir, sizeof(current_dir));
    
    if (chdir(extract_dir) != 0) {
        return -1;
    }
    
    for (int i = 0; scripts[i] != NULL; i++) {
        if (access(scripts[i], F_OK) == 0) {
            debug_print("Found install script: %s", scripts[i]);
            chmod(scripts[i], 0755);
            
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "./%s", scripts[i]);
            
            int ret = system(cmd);
            if (ret == 0) {
                print_success("Executed %s", scripts[i]);
                chdir(current_dir);
                return 0;
            }
        }
    }
    
    chdir(current_dir);
    return -1;
}

int install_package(const char *name, const char *version_specific) {
    package_info_t info;
    memset(&info, 0, sizeof(info));
    strcpy(info.name, name);
    
    print_step("Searching for %s", name);
    
    if (search_package(name, &info) != 0) {
        print_error("Package '%s' not found in any repository", name);
        print_info("Try 'apkm search %s' to find similar packages", name);
        return -1;
    }
    
    // Vérifier si déjà installé
    char installed_version[64] = "";
    if (db_is_installed(name, installed_version, sizeof(installed_version))) {
        if (strcmp(installed_version, info.version) == 0) {
            print_warning("Package %s version %s is already installed", name, info.version);
            if (interactive) {
                printf("Reinstall? [y/N] ");
                char response = getchar();
                if (response != 'y' && response != 'Y') {
                    print_info("Installation cancelled");
                    return 0;
                }
            }
        } else {
            print_info("Upgrading %s from %s to %s", name, installed_version, info.version);
        }
    }
    
    print_success("Found %s version %s-%s (%s)", 
                  info.name, info.version, info.release, info.arch);
    print_info("  Repository: %s", info.repository);
    print_info("  Author: %s", info.author);
    print_info("  Downloads: %d", info.downloads);
    print_info("  Size: %.1f KB", info.size / 1024.0);
    
    if (strlen(info.description) > 0) {
        print_info("  Description: %s", info.description);
    }
    
    // Résoudre les dépendances
    resolve_dependencies(&info);
    
    // Demander confirmation
    if (interactive) {
        printf("Proceed with installation? [Y/n] ");
        char response = getchar();
        if (response == 'n' || response == 'N') {
            print_info("Installation cancelled");
            return 0;
        }
    }
    
    // Télécharger
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s-%s.%s.tar.bool", 
             info.name, info.version, info.release, info.arch);
    
    print_step("Downloading package");
    
    if (download_package(&info, tmp_path) != 0) {
        print_error("Download failed");
        return -1;
    }
    
    // Extraire
    char extract_dir[512];
    snprintf(extract_dir, sizeof(extract_dir), "/tmp/apkm_extract_%s_%d", info.name, getpid());
    
    print_step("Extracting package");
    
    if (extract_package(tmp_path, extract_dir) != 0) {
        print_error("Failed to extract package");
        unlink(tmp_path);
        return -1;
    }
    
    // Installer
    print_step("Installing files");
    
    int install_result = run_install_scripts(extract_dir);
    
    if (install_result != 0) {
        install_result = find_binary_and_install(extract_dir, info.name);
    }
    
    // Nettoyer
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", extract_dir);
    system(cmd);
    
    if (install_result == 0) {
        // Enregistrer dans la base de données
        db_register_installed(&info);
        
        print_success("Package %s %s-%s installed successfully", 
                     info.name, info.version, info.release);
        
        // Vérifier le PATH
        snprintf(cmd, sizeof(cmd), "which %s > /dev/null 2>&1", info.name);
        if (system(cmd) != 0) {
            print_warning("Binary not in PATH. You may need to add /usr/local/bin to your PATH");
            print_info("  export PATH=$PATH:/usr/local/bin");
        } else {
            print_info("Try: %s --version", info.name);
        }
        
        unlink(tmp_path);
        return 0;
    }
    
    print_error("Installation failed");
    print_info("Package file saved at: %s", tmp_path);
    print_info("You can try manual installation from there");
    
    return -1;
}

// ============================================================================
// COMMANDES AVANCÉES
// ============================================================================

int cmd_update(void) {
    print_step("Updating package databases");
    
    load_repositories();
    
    int success = 0;
    int total = 0;
    
    for (int i = 0; i < repo_count; i++) {
        if (!repositories[i].enabled) continue;
        
        print_info("Fetching %s (%s)", repositories[i].name, repositories[i].url);
        
        CURL *curl = curl_easy_init();
        if (!curl) continue;
        
        char url[512];
        snprintf(url, sizeof(url), "%s/v5.2/package/search?q=", repositories[i].url);
        
        struct curl_response resp = {0};
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res == CURLE_OK && resp.data) {
            // Sauvegarder dans le cache
            char cache_file[512];
            snprintf(cache_file, sizeof(cache_file), "%s/%s.json", APKM_CACHE_PATH, repositories[i].name);
            
            mkdir(APKM_CACHE_PATH, 0755);
            
            FILE *f = fopen(cache_file, "w");
            if (f) {
                fprintf(f, "%s", resp.data);
                fclose(f);
                
                // Mettre à jour la base de données SQLite
                struct json_object *parsed = json_tokener_parse(resp.data);
                if (parsed) {
                    struct json_object *results;
                    if (json_object_object_get_ex(parsed, "results", &results)) {
                        int count = json_object_array_length(results);
                        total += count;
                        success++;
                        
                        // Ici on pourrait mettre à jour available_packages
                    }
                    json_object_put(parsed);
                }
                
                print_success("Updated %s (%d packages)", repositories[i].name, count);
            }
            free(resp.data);
        } else {
            print_warning("Failed to update %s", repositories[i].name);
        }
    }
    
    if (success > 0) {
        print_success("Updated %d/%d repositories, %d packages total", 
                     success, repo_count, total);
        
        // Afficher les stats
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char date[64];
        strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", tm);
        print_info("Last update: %s", date);
    } else {
        print_error("Failed to update any repository");
        return -1;
    }
    
    return 0;
}

int cmd_upgrade(void) {
    print_step("Checking for upgradable packages");
    
    // Lister les packages installés
    const char *sql = "SELECT name, version, release, architecture FROM installed_packages;";
    sqlite3_stmt *stmt;
    
    if (!global_db || sqlite3_prepare_v2(global_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        print_error("No packages installed");
        return -1;
    }
    
    int upgradable = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0);
        const char *current_ver = (const char*)sqlite3_column_text(stmt, 1);
        const char *current_rel = (const char*)sqlite3_column_text(stmt, 2);
        
        // Chercher la dernière version disponible
        package_info_t info;
        memset(&info, 0, sizeof(info));
        strcpy(info.name, name);
        
        if (search_package(name, &info) == 0) {
            // Comparer les versions (simplifié)
            if (strcmp(info.version, current_ver) != 0 || 
                strcmp(info.release, current_rel) != 0) {
                
                printf("  • %s: %s-%s -> %s-%s\n", 
                       name, current_ver, current_rel, info.version, info.release);
                upgradable++;
            }
        }
    }
    
    sqlite3_finalize(stmt);
    
    if (upgradable == 0) {
        print_success("All packages are up to date");
        return 0;
    }
    
    print_info("%d package(s) can be upgraded", upgradable);
    
    if (interactive) {
        printf("Upgrade all? [y/N] ");
        char response = getchar();
        if (response != 'y' && response != 'Y') {
            print_info("Upgrade cancelled");
            return 0;
        }
    }
    
    // Ré-exécuter la requête pour upgrade
    sqlite3_prepare_v2(global_db, sql, -1, &stmt, NULL);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0);
        
        package_info_t info;
        memset(&info, 0, sizeof(info));
        strcpy(info.name, name);
        
        if (search_package(name, &info) == 0) {
            print_step("Upgrading %s", name);
            install_package(name, NULL);
        }
    }
    
    sqlite3_finalize(stmt);
    
    print_success("Upgrade completed");
    return 0;
}

int cmd_list_installed(void) {
    const char *sql = 
        "SELECT name, version, release, architecture, size, install_date, repository "
        "FROM installed_packages ORDER BY name;";
    
    sqlite3_stmt *stmt;
    
    if (!global_db || sqlite3_prepare_v2(global_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        print_info("No packages installed");
        return 0;
    }
    
    printf("\n\033[1m📦 Installed packages:\033[0m\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("%-20s %-15s %-10s %-10s %s\n", "NAME", "VERSION", "ARCH", "SIZE", "REPOSITORY");
    printf("────────────────────────────────────────────────────────────\n");
    
    int count = 0;
    long total_size = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0);
        const char *version = (const char*)sqlite3_column_text(stmt, 1);
        const char *release = (const char*)sqlite3_column_text(stmt, 2);
        const char *arch = (const char*)sqlite3_column_text(stmt, 3);
        long size = sqlite3_column_int(stmt, 4);
        const char *repo = (const char*)sqlite3_column_text(stmt, 6);
        
        char ver_str[64];
        snprintf(ver_str, sizeof(ver_str), "%s-%s", version, release);
        
        printf("  • %-18s %-15s %-10s %5.1f KB  %s\n", 
               name, ver_str, arch, size / 1024.0, repo ? repo : "local");
        
        count++;
        total_size += size;
    }
    
    sqlite3_finalize(stmt);
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Total: %d packages, %.2f MB\n", count, total_size / (1024.0 * 1024.0));
    printf("\n");
    
    return 0;
}

int cmd_search(const char *query) {
    print_step("Searching for '%s'", query);
    
    load_repositories();
    
    int total = 0;
    
    for (int i = 0; i < repo_count; i++) {
        if (!repositories[i].enabled) continue;
        
        CURL *curl = curl_easy_init();
        if (!curl) continue;
        
        char url[512];
        snprintf(url, sizeof(url), "%s/v5.2/package/search?q=%s", repositories[i].url, query);
        
        struct curl_response resp = {0};
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res == CURLE_OK && resp.data) {
            struct json_object *parsed = json_tokener_parse(resp.data);
            if (parsed) {
                struct json_object *results;
                if (json_object_object_get_ex(parsed, "results", &results)) {
                    int len = json_object_array_length(results);
                    
                    if (len > 0 && total == 0) {
                        printf("\n\033[1m🔍 Search results:\033[0m\n");
                        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
                        printf("%-20s %-12s %-10s %-15s %s\n", 
                               "NAME", "VERSION", "ARCH", "AUTHOR", "REPO");
                        printf("────────────────────────────────────────────────────\n");
                    }
                    
                    for (int j = 0; j < len; j++) {
                        struct json_object *pkg = json_object_array_get_idx(results, j);
                        struct json_object *name, *version, *author, *arch, *release;
                        
                        const char *n = "?", *v = "?", *a = "?", *ar = "x86_64", *r = "r0";
                        
                        if (json_object_object_get_ex(pkg, "name", &name))
                            n = json_object_get_string(name);
                        if (json_object_object_get_ex(pkg, "version", &version))
                            v = json_object_get_string(version);
                        if (json_object_object_get_ex(pkg, "author", &author))
                            a = json_object_get_string(author);
                        if (json_object_object_get_ex(pkg, "arch", &arch))
                            ar = json_object_get_string(arch);
                        
                        char ver_str[64];
                        snprintf(ver_str, sizeof(ver_str), "%s", v);
                        
                        printf("  • %-18s %-12s %-10s %-15s %s\n", 
                               n, ver_str, ar, a, repositories[i].name);
                        total++;
                    }
                }
                json_object_put(parsed);
            }
            free(resp.data);
        }
    }
    
    if (total == 0) {
        print_info("No packages found matching '%s'", query);
    } else {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("Found %d packages\n", total);
    }
    
    return 0;
}

int cmd_info(const char *name) {
    package_info_t info;
    memset(&info, 0, sizeof(info));
    
    if (search_package(name, &info) != 0) {
        print_error("Package '%s' not found", name);
        return -1;
    }
    
    printf("\n\033[1m📦 Package: %s\033[0m\n", info.name);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Version:     %s-%s\n", info.version, info.release);
    printf("  Architecture: %s\n", info.arch);
    printf("  Repository:   %s\n", info.repository);
    printf("  Author:       %s\n", info.author);
    printf("  Downloads:    %d\n", info.downloads);
    printf("  Size:         %.1f KB\n", info.size / 1024.0);
    printf("  SHA256:       %.16s...\n", info.sha256);
    
    if (strlen(info.license) > 0) {
        printf("  License:      %s\n", info.license);
    }
    
    if (strlen(info.description) > 0) {
        printf("\n  Description:\n    %s\n", info.description);
    }
    
    // Vérifier si installé
    char installed_version[64] = "";
    if (db_is_installed(name, installed_version, sizeof(installed_version))) {
        printf("\n  \033[32m✓ Installed version: %s\033[0m\n", installed_version);
    }
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return 0;
}

int cmd_remove(const char *name) {
    // Vérifier si installé
    if (!db_is_installed(name, NULL, 0)) {
        print_error("Package %s is not installed", name);
        return -1;
    }
    
    print_warning("This will remove %s", name);
    
    if (interactive) {
        printf("Continue? [y/N] ");
        char response = getchar();
        if (response != 'y' && response != 'Y') {
            print_info("Removal cancelled");
            return 0;
        }
    }
    
    // Supprimer le binaire
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -f /usr/local/bin/%s 2>/dev/null", name);
    system(cmd);
    
    // Supprimer de la base
    db_remove_installed(name);
    
    print_success("Package %s removed", name);
    return 0;
}

int cmd_clean(void) {
    print_step("Cleaning cache");
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s/*.tmp /tmp/apkm_* /tmp/*.tar.bool 2>/dev/null", APKM_CACHE_PATH);
    system(cmd);
    
    print_success("Cache cleaned");
    return 0;
}

int cmd_list_repos(void) {
    load_repositories();
    
    printf("\n\033[1m📋 Configured repositories:\033[0m\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("%-3s %-15s %-30s %s\n", "", "NAME", "URL", "PRIORITY");
    printf("──────────────────────────────────────────────\n");
    
    for (int i = 0; i < repo_count; i++) {
        printf("  %s%-15s %-30s %d\n", 
               repositories[i].enabled ? "✓ " : "✗ ",
               repositories[i].name, 
               repositories[i].url,
               repositories[i].priority);
    }
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return 0;
}

// ============================================================================
// AIDE COMPLÈTE
// ============================================================================

void print_help(void) {
    printf("\n\033[1m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n");
    printf("\033[1m  APKM - Zarch Hub Package Manager v2.0\033[0m\n");
    printf("\033[1m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n\n");
    
    printf("\033[36mUSAGE:\033[0m\n");
    printf("  apkm <command> [arguments]\n\n");
    
    printf("\033[36mPACKAGE MANAGEMENT:\033[0m\n");
    printf("  install <pkg>[@ver]   Install a package (optionally specific version)\n");
    printf("  remove <pkg>           Remove an installed package\n");
    printf("  upgrade                 Upgrade all installed packages\n");
    printf("  update                  Update package database from repositories\n\n");
    
    printf("\033[36mQUERYING:\033[0m\n");
    printf("  search <term>          Search for packages\n");
    printf("  info <pkg>             Show detailed package information\n");
    printf("  list                    List installed packages\n");
    printf("  files <pkg>             List files installed by a package\n");
    printf("  depends <pkg>           Show package dependencies\n");
    printf("  rdepends <pkg>          Show reverse dependencies\n\n");
    
    printf("\033[36mREPOSITORY MANAGEMENT:\033[0m\n");
    printf("  repo list               List configured repositories\n");
    printf("  repo add <name> <url>   Add a repository\n");
    printf("  repo remove <name>      Remove a repository\n");
    printf("  repo enable <name>      Enable a repository\n");
    printf("  repo disable <name>     Disable a repository\n\n");
    
    printf("\033[36mSYSTEM:\033[0m\n");
    printf("  clean                   Clean cache and temporary files\n");
    printf("  version                  Show version information\n");
    printf("  help                     Show this help message\n\n");
    
    printf("\033[36mOPTIONS:\033[0m\n");
    printf("  --debug                 Enable debug output\n");
    printf("  --quiet                 Suppress output\n");
    printf("  --yes                    Automatic yes to prompts\n");
    printf("  --no-interactive         Disable interactive prompts\n\n");
    
    printf("\033[36mEXAMPLES:\033[0m\n");
    printf("  apkm install nginx\n");
    printf("  apkm install nodejs@18.0.0\n");
    printf("  apkm search database\n");
    printf("  apkm info python\n");
    printf("  apkm update\n");
    printf("  apkm upgrade\n");
    printf("  apkm list\n");
    printf("  apkm repo list\n\n");
    
    printf("\033[1m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\033[0m\n");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    
    // Créer les répertoires nécessaires
    mkdir("/usr/local/share/apkm", 0755);
    mkdir(APKM_DB_PATH, 0755);
    mkdir(APKM_CACHE_PATH, 0755);
    mkdir("/usr/local/bin", 0755);
    
    // Initialisation globale
    curl_global_init(CURL_GLOBAL_ALL);
    db_init();
    load_repositories();
    
    // Parser les options globales
    int args_processed = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
            args_processed++;
        }
        else if (strcmp(argv[i], "--quiet") == 0) {
            quiet_mode = 1;
            args_processed++;
        }
        else if (strcmp(argv[i], "--yes") == 0 || strcmp(argv[i], "-y") == 0) {
            interactive = 0;
            args_processed++;
        }
        else if (strcmp(argv[i], "--no-interactive") == 0) {
            interactive = 0;
            args_processed++;
        }
        else {
            break;
        }
    }
    
    // Décaler les arguments
    if (args_processed > 1) {
        argv += args_processed - 1;
        argc -= args_processed - 1;
    }
    
    debug_print("APKM v2.0.0 starting...");
    debug_print("Command: %s", argv[1]);
    
    int result = 0;
    
    // Traiter les commandes
    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            print_error("Missing package name");
            print_info("Usage: apkm install <package>[@version]");
            result = 1;
        } else {
            char *package_name = argv[2];
            char *version = NULL;
            
            char *at = strchr(package_name, '@');
            if (at) {
                *at = '\0';
                version = at + 1;
                debug_print("Installing %s version %s", package_name, version);
            }
            
            result = install_package(package_name, version);
        }
    }
    else if (strcmp(argv[1], "remove") == 0 || strcmp(argv[1], "rm") == 0) {
        if (argc < 3) {
            print_error("Missing package name");
            result = 1;
        } else {
            result = cmd_remove(argv[2]);
        }
    }
    else if (strcmp(argv[1], "update") == 0) {
        result = cmd_update();
    }
    else if (strcmp(argv[1], "upgrade") == 0 || strcmp(argv[1], "up") == 0) {
        result = cmd_upgrade();
    }
    else if (strcmp(argv[1], "search") == 0) {
        if (argc < 3) {
            print_error("Missing search term");
            result = 1;
        } else {
            result = cmd_search(argv[2]);
        }
    }
    else if (strcmp(argv[1], "info") == 0 || strcmp(argv[1], "show") == 0) {
        if (argc < 3) {
            print_error("Missing package name");
            result = 1;
        } else {
            result = cmd_info(argv[2]);
        }
    }
    else if (strcmp(argv[1], "list") == 0 || strcmp(argv[1], "ls") == 0) {
        result = cmd_list_installed();
    }
    else if (strcmp(argv[1], "clean") == 0) {
        result = cmd_clean();
    }
    else if (strcmp(argv[1], "repo") == 0 && argc >= 3) {
        if (strcmp(argv[2], "list") == 0) {
            result = cmd_list_repos();
        } else {
            print_error("Unknown repo command: %s", argv[2]);
            result = 1;
        }
    }
    else if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("APKM version 2.0.0\n");
        printf("Copyright (c) 2026 Gopu.inc\n");
        printf("Zarch Hub: %s\n", ZARCH_HUB_URL);
        result = 0;
    }
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        result = 0;
    }
    else {
        print_error("Unknown command: %s", argv[1]);
        print_info("Try 'apkm help' for usage information");
        result = 1;
    }
    
    db_close();
    curl_global_cleanup();
    return result;
}
