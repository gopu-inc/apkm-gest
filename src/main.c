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
#include <json-c/json.h>
#include <toml.h>  // Pour parser le manifest.toml

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

typedef struct {
    char name[128];
    char url[256];
    char type[32];
    int enabled;
    int priority;
} repository_t;

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
    char dependencies[1024];
    int downloads;
    long size;
} package_info_t;

typedef struct {
    char name[256];
    char version[64];
    char release[16];
    char arch[32];
    char **deps;
    int dep_count;
} manifest_t;

#define MAX_REPOS 32
static repository_t repositories[MAX_REPOS];
static int repo_count = 0;

// ============================================================================
// ANIMATIONS
// ============================================================================

void print_spinner(int phase) {
    const char *spinner = "|/-\\";
    printf("\r[%c] ", spinner[phase % 4]);
    fflush(stdout);
}

void print_warning(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[33m⚠️  \033[0m");  // Jaune
    vprintf(format, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

void print_progress(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\r\033[K");  // Effacer la ligne
    vprintf(format, args);
    fflush(stdout);
    va_end(args);
}

// ============================================================================
// UTILITAIRES
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
    printf("[DEBUG] ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

void print_info(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[!]  ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_success(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[V] ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[X] ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void print_step(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("▶ ");
    vprintf(format, args);
    printf("... ");
    fflush(stdout);
    va_end(args);
}

// ============================================================================
// GESTION DES REPOSITORIES
// ============================================================================

int load_repositories(void) {
    FILE *f = fopen(APKM_CONF_PATH, "r");
    if (!f) {
        mkdir("/etc/apkm", 0755);
        f = fopen(APKM_CONF_PATH, "w");
        if (f) {
            fprintf(f, "# APKM Repositories Configuration\n");
            fprintf(f, "# Format: name url [priority] [enabled]\n");
            fprintf(f, "# Example:\n");
            fprintf(f, "zarch-hub https://gsql-badge.onrender.com/v5.2 5 1\n");
            fprintf(f, "pypi     https://files.pythonhosted.org/packages 3 1\n");
            fclose(f);
        }
        f = fopen(APKM_CONF_PATH, "r");
    }
    
    if (!f) return -1;
    
    char line[512];
    repo_count = 0;
    
    while (fgets(line, sizeof(line), f) && repo_count < MAX_REPOS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char name[128], url[256], type[32] = "zarch";
        int priority = 10;
        int enabled = 1;
        
        int parsed = sscanf(line, "%127s %255s %d %d", name, url, &priority, &enabled);
        if (parsed >= 2) {
            strcpy(repositories[repo_count].name, name);
            strcpy(repositories[repo_count].url, url);
            repositories[repo_count].priority = (parsed >= 3) ? priority : 10;
            repositories[repo_count].enabled = (parsed >= 4) ? enabled : 1;
            
            // Détection du type
            if (strstr(url, "pypi") || strstr(url, "pythonhosted"))
                strcpy(repositories[repo_count].type, "pypi");
            else
                strcpy(repositories[repo_count].type, "zarch");
            
            repo_count++;
            debug_print("Loaded repo: %s (%s) prio=%d enabled=%d", 
                       name, url, repositories[repo_count-1].priority, 
                       repositories[repo_count-1].enabled);
        }
    }
    
    fclose(f);
    return repo_count;
}

// ============================================================================
// MANIFEST.TOML PARSER
// ============================================================================

int parse_manifest(const char *path, manifest_t *manifest) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[1024];
    char section[64] = "";
    int in_deps = 0;
    
    while (fgets(line, sizeof(line), f)) {
        char *p = strchr(line, '\n');
        if (p) *p = 0;
        
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = 0;
                strcpy(section, line + 1);
                in_deps = (strcmp(section, "dependencies") == 0);
            }
            continue;
        }
        
        if (strlen(line) == 0 || line[0] == '#') continue;
        
        char *eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = 0;
        char *key = line;
        char *value = eq + 1;
        
        // Nettoyer
        while (*key == ' ' || *key == '\t') key++;
        char *kend = key + strlen(key) - 1;
        while (kend > key && (*kend == ' ' || *kend == '\t')) *kend-- = 0;
        
        while (*value == ' ' || *value == '\t') value++;
        char *vend = value + strlen(value) - 1;
        while (vend > value && (*vend == ' ' || *vend == '\t' || *vend == '"')) *vend-- = 0;
        if (*value == '"') value++;
        
        if (strcmp(section, "metadata") == 0) {
            if (strcmp(key, "name") == 0) strcpy(manifest->name, value);
            else if (strcmp(key, "version") == 0) strcpy(manifest->version, value);
            else if (strcmp(key, "release") == 0) strcpy(manifest->release, value);
            else if (strcmp(key, "arch") == 0) strcpy(manifest->arch, value);
        }
        else if (in_deps) {
            // Stocker les dépendances
            // Format simplifié pour l'exemple
            debug_print("Dependency: %s = %s", key, value);
        }
    }
    
    fclose(f);
    return 0;
}

// ============================================================================
// RECHERCHE MULTI-REPOS
// ============================================================================
int search_package(const char *name, package_info_t *info) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    int found = -1;
    
    // Initialiser info avec des valeurs par défaut
    memset(info, 0, sizeof(package_info_t));
    strcpy(info->name, name);
    strcpy(info->release, "r0");
    strcpy(info->arch, "x86_64");
    
    for (int i = 0; i < repo_count && found != 0; i++) {
        if (!repositories[i].enabled) continue;
        
        print_progress("🔍 Searching %s...", repositories[i].name);
        
        char url[512];
        if (strcmp(repositories[i].type, "pypi") == 0) {
            snprintf(url, sizeof(url), "%s/%s/json", repositories[i].url, name);
        } else {
            snprintf(url, sizeof(url), "%s/package/%s", repositories[i].url, name);
        }
        
        struct curl_response resp = {0};
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        CURLcode res = curl_easy_perform(curl);
        
        if (res == CURLE_OK && resp.data) {
            struct json_object *parsed = json_tokener_parse(resp.data);
            if (parsed) {
                if (strcmp(repositories[i].type, "pypi") == 0) {
                    struct json_object *info_obj;
                    if (json_object_object_get_ex(parsed, "info", &info_obj)) {
                        struct json_object *tmp;
                        
                        // Version avec valeurs par défaut
                        strcpy(info->version, "0.0.0");
                        strcpy(info->author, "Unknown");
                        strcpy(info->description, "");
                        strcpy(info->license, "Unknown");
                        
                        if (json_object_object_get_ex(info_obj, "version", &tmp) && tmp)
                            strcpy(info->version, json_object_get_string(tmp));
                        if (json_object_object_get_ex(info_obj, "author", &tmp) && tmp)
                            strcpy(info->author, json_object_get_string(tmp));
                        if (json_object_object_get_ex(info_obj, "author_email", &tmp) && tmp)
                            snprintf(info->author, sizeof(info->author), "%s", json_object_get_string(tmp));
                        if (json_object_object_get_ex(info_obj, "description", &tmp) && tmp)
                            strcpy(info->description, json_object_get_string(tmp));
                        if (json_object_object_get_ex(info_obj, "license", &tmp) && tmp)
                            strcpy(info->license, json_object_get_string(tmp));
                        
                        // Pour PyPI, on utilise des valeurs par défaut pour release/arch
                        strcpy(info->release, "pypi");
                        strcpy(info->arch, "noarch");
                        strcpy(info->repository, repositories[i].name);
                        
                        // Construire l'URL (simplifiée pour PyPI)
                        snprintf(info->url, sizeof(info->url), 
                                "https://pypi.org/pypi/%s/json", info->name);
                        
                        found = 0;
                        print_success("Found in %s", repositories[i].name);
                    }
                } else {
                    struct json_object *package_obj;
                    if (json_object_object_get_ex(parsed, "package", &package_obj)) {
                        struct json_object *tmp;
                        
                        if (json_object_object_get_ex(package_obj, "version", &tmp) && tmp)
                            strcpy(info->version, json_object_get_string(tmp));
                        if (json_object_object_get_ex(package_obj, "release", &tmp) && tmp)
                            strcpy(info->release, json_object_get_string(tmp));
                        if (json_object_object_get_ex(package_obj, "arch", &tmp) && tmp)
                            strcpy(info->arch, json_object_get_string(tmp));
                        if (json_object_object_get_ex(package_obj, "author", &tmp) && tmp)
                            strcpy(info->author, json_object_get_string(tmp));
                        if (json_object_object_get_ex(package_obj, "description", &tmp) && tmp)
                            strcpy(info->description, json_object_get_string(tmp));
                        if (json_object_object_get_ex(package_obj, "license", &tmp) && tmp)
                            strcpy(info->license, json_object_get_string(tmp));
                        if (json_object_object_get_ex(package_obj, "sha256", &tmp) && tmp)
                            strcpy(info->sha256, json_object_get_string(tmp));
                        if (json_object_object_get_ex(package_obj, "size", &tmp) && tmp)
                            info->size = json_object_get_int(tmp);
                        if (json_object_object_get_ex(package_obj, "downloads", &tmp) && tmp)
                            info->downloads = json_object_get_int(tmp);
                        
                        strcpy(info->repository, repositories[i].name);
                        
                        // Construire l'URL de téléchargement
                        snprintf(info->url, sizeof(info->url),
                                "%s/download/public/%s/%s/%s/%s",
                                repositories[i].url, name, info->version, 
                                info->release, info->arch);
                        
                        found = 0;
                        print_success("Found in %s", repositories[i].name);
                    }
                }
                json_object_put(parsed);
            }
            free(resp.data);
        }
    }
    
    curl_easy_cleanup(curl);
    printf("\n");  // Nouvelle ligne après le spinner
    
    return found;
}

// ============================================================================
// TÉLÉCHARGEMENT AVEC ANIMATION
// ============================================================================
int download_package(package_info_t *info, const char *output_path) {
    if (!info || !info->url || strlen(info->url) == 0) {
        print_error("Invalid package URL");
        return -1;
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return -1;
    }
    
    print_step("Downloading");
    
    curl_easy_setopt(curl, CURLOPT_URL, info->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || http_code != 200) {
        print_error("Download failed (HTTP %ld)", http_code);
        unlink(output_path);
        return -1;
    }
    
    // Vérifier que le fichier n'est pas vide
    struct stat st;
    if (stat(output_path, &st) != 0 || st.st_size == 0) {
        print_error("Downloaded file is empty");
        unlink(output_path);
        return -1;
    }
    
    print_success("Download complete (%.2f KB)", st.st_size / 1024.0);
    return 0;
}

// ============================================================================
// INSTALLATION
// ============================================================================
int install_package(const char *name, const char *version_specific) {
    // Si c'est un chemin local
    if (access(name, F_OK) == 0 && (strstr(name, ".tar.bool") || strstr(name, ".selp.bool"))) {
        return install_local_package(name);
    }
    
    package_info_t info;
    memset(&info, 0, sizeof(info));
    strcpy(info->name, name);
    
    print_step("Searching for %s", name);
    
    if (search_package(name, &info) != 0) {
        print_error("Package '%s' not found", name);
        return -1;
    }
    
    print_success("Found %s version %s-%s", name, info.version, info.release);
    print_info("  Repository: %s", info.repository);
    print_info("  Author: %s", info.author);
    print_info("  Downloads: %d", info.downloads);
    print_info("  Size: %.1f KB", info.size / 1024.0);
    
    // Construire le chemin temporaire
    char tmp_path[512];
    if (strcmp(info.repository, "pypi") == 0) {
        snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s.tar.bool", name, info.version);
    } else {
        snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s-%s.%s.tar.bool", 
                 name, info.version, info.release, info.arch);
    }
    
    if (download_package(&info, tmp_path) != 0) {
        return -1;
    }
    
    return install_local_package(tmp_path);
}

int install_package(const char *name, const char *version_specific) {
    // Si c'est un chemin local
    if (access(name, F_OK) == 0 && strstr(name, ".tar.bool")) {
        return install_local_package(name);
    }
    
    package_info_t info;
    memset(&info, 0, sizeof(info));
    strcpy(info.name, name);
    
    print_step("Searching for %s", name);
    
    if (search_package(name, &info) != 0) {
        print_error("Package '%s' not found", name);
        return -1;
    }
    
    print_success("Found %s version %s-%s", name, info.version, info.release);
    print_info("  Repository: %s", info.repository);
    print_info("  Author: %s", info.author);
    print_info("  Downloads: %d", info.downloads);
    print_info("  Size: %.1f KB", info.size / 1024.0);
    
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s-%s.%s.tar.bool", 
             name, info.version, info.release, info.arch);
    
    print_step("Downloading");
    if (download_package(&info, tmp_path) != 0) {
        print_error("Download failed");
        return -1;
    }
    print_success("Download complete");
    
    return install_local_package(tmp_path);
}

// ============================================================================
// COMMANDES
// ============================================================================

int cmd_update(void) {
    print_step("Updating repositories");
    
    load_repositories();
    
    for (int i = 0; i < repo_count; i++) {
        if (!repositories[i].enabled) continue;
        
        print_progress("Fetching %s...", repositories[i].name);
        // Ici on ferait une vraie mise à jour
        usleep(500000);  // Simulation
    }
    printf("\n");
    
    print_success("Updated %d repositories", repo_count);
    return 0;
}

int cmd_search(const char *query) {
    package_info_t info;
    if (search_package(query, &info) == 0) {
        printf("\n📦 Package: %s\n", info.name);
        printf("  Version: %s-%s\n", info.version, info.release);
        printf("  Author:  %s\n", info.author);
        printf("  Repo:    %s\n", info.repository);
        printf("  Size:    %.1f KB\n", info.size / 1024.0);
    }
    return 0;
}

int cmd_list_installed(void) {
    printf("\n📦 Installed packages:\n");
    printf("────────────────────────\n");
    system("ls -1 /usr/local/bin/ | grep -v '^$' | sed 's/^/  • /'");
    return 0;
}

int cmd_list_repos(void) {
    load_repositories();
    
    printf("\n📋 Configured repositories:\n");
    printf("────────────────────────────\n");
    for (int i = 0; i < repo_count; i++) {
        printf("  %s %s (%s) [prio:%d]\n",
               repositories[i].enabled ? "✓" : "✗",
               repositories[i].name,
               repositories[i].type,
               repositories[i].priority);
    }
    return 0;
}

// ============================================================================
// HELP
// ============================================================================

void print_help(void) {
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APKM - Advanced Package Manager v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    printf("USAGE:\n");
    printf("  apkm <command> [arguments]\n\n");
    
    printf("COMMANDS:\n");
    printf("  install <pkg>[@ver]   Install package (or local .tar.bool)\n");
    printf("  remove <pkg>           Remove installed package\n");
    printf("  update                  Update repository databases\n");
    printf("  upgrade                 Upgrade all packages\n");
    printf("  search <term>          Search for packages\n");
    printf("  info <pkg>             Show package info\n");
    printf("  list                    List installed packages\n");
    printf("  repo list               List configured repositories\n");
    printf("  clean                   Clean cache\n");
    printf("  version                  Show version\n");
    printf("  help                     Show this help\n\n");
    
    printf("OPTIONS:\n");
    printf("  --debug                 Enable debug output\n");
    printf("  --quiet                 Suppress output\n\n");
    
    printf("EXAMPLES:\n");
    printf("  apkm install nginx\n");
    printf("  apkm install ./package.tar.bool\n");
    printf("  apkm search database\n");
    printf("  apkm update\n");
    printf("  apkm repo list\n\n");
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    
    // Créer les répertoires
    mkdir("/usr/local/share/apkm", 0755);
    mkdir(APKM_DB_PATH, 0755);
    mkdir(APKM_CACHE_PATH, 0755);
    
    curl_global_init(CURL_GLOBAL_ALL);
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
        else {
            break;
        }
    }
    
    if (args_processed > 1) {
        argv += args_processed - 1;
        argc -= args_processed - 1;
    }
    
    int result = 0;
    
    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            print_error("Missing package name");
            result = 1;
        } else {
            char *package = argv[2];
            char *version = NULL;
            
            char *at = strchr(package, '@');
            if (at) {
                *at = '\0';
                version = at + 1;
            }
            
            result = install_package(package, version);
        }
    }
    else if (strcmp(argv[1], "update") == 0) {
        result = cmd_update();
    }
    else if (strcmp(argv[1], "search") == 0) {
        if (argc < 3) {
            print_error("Missing search term");
            result = 1;
        } else {
            result = cmd_search(argv[2]);
        }
    }
    else if (strcmp(argv[1], "list") == 0) {
        result = cmd_list_installed();
    }
    else if (strcmp(argv[1], "repo") == 0 && argc >= 3) {
        if (strcmp(argv[2], "list") == 0) {
            result = cmd_list_repos();
        } else {
            print_error("Unknown repo command");
            result = 1;
        }
    }
    else if (strcmp(argv[1], "version") == 0) {
        printf("APKM version 2.0.0\n");
    }
    else if (strcmp(argv[1], "help") == 0) {
        print_help();
    }
    else {
        print_error("Unknown command: %s", argv[1]);
        print_help();
        result = 1;
    }
    
    curl_global_cleanup();
    return result;
}
