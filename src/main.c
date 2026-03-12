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
#include "apkm.h"
#include <json-c/json.h>

#define ZARCH_HUB_URL "https://gsql-badge.onrender.com"
#define ZARCH_API_URL ZARCH_HUB_URL "/v5.2"
#define ZARCH_PACKAGE_URL ZARCH_HUB_URL "/package/download"

// Configuration
#define APKM_CONF_PATH "/etc/apkm/repositories.conf"
#define APKM_LOCAL_DB_PATH "/usr/local/share/apkm/database"
#define APKM_CACHE_PATH "/usr/local/share/apkm/cache"

struct curl_response {
    char *data;
    size_t size;
};

static int debug_mode = 0;
static int quiet_mode = 0;

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

// Structure pour Manifest.toml
typedef struct {
    char name[256];
    char version[64];
    char release[16];
    char arch[32];
    char description[1024];
    char maintainer[256];
    char license[64];
    char dependencies[1024];
} manifest_t;

// ============================================================================
// FONCTIONS UTILITAIRES
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
    printf("ℹ️  ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_success(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("✅ ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "❌ ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void print_warning(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("⚠️  ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_step(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("▶ ");
    vprintf(format, args);
    printf("...\n");
    va_end(args);
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
// PARSEUR SIMPLE DE MANIFEST.TOML
// ============================================================================

int parse_manifest(const char *path, manifest_t *manifest) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[1024];
    char section[64] = "";
    
    memset(manifest, 0, sizeof(manifest_t));
    
    while (fgets(line, sizeof(line), f)) {
        // Enlever le saut de ligne
        line[strcspn(line, "\n")] = 0;
        
        // Ignorer les commentaires et lignes vides
        if (line[0] == '#' || line[0] == '\0') continue;
        
        // Détection des sections
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = 0;
                strcpy(section, line + 1);
            }
            continue;
        }
        
        // Parser les clés/valeurs
        char *eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = 0;
        char *key = line;
        char *value = eq + 1;
        
        // Nettoyer les espaces
        while (*key == ' ') key++;
        char *kend = key + strlen(key) - 1;
        while (kend > key && *kend == ' ') *kend-- = 0;
        
        while (*value == ' ') value++;
        if (*value == '"') {
            value++;
            char *vend = strchr(value, '"');
            if (vend) *vend = 0;
        }
        
        if (strcmp(section, "metadata") == 0) {
            if (strcmp(key, "name") == 0) strcpy(manifest->name, value);
            else if (strcmp(key, "version") == 0) strcpy(manifest->version, value);
            else if (strcmp(key, "release") == 0) strcpy(manifest->release, value);
            else if (strcmp(key, "arch") == 0) strcpy(manifest->arch, value);
            else if (strcmp(key, "description") == 0) strcpy(manifest->description, value);
            else if (strcmp(key, "maintainer") == 0) strcpy(manifest->maintainer, value);
            else if (strcmp(key, "license") == 0) strcpy(manifest->license, value);
        }
        else if (strcmp(section, "dependencies") == 0) {
            // Pour l'instant, on stocke juste la ligne
            if (strlen(manifest->dependencies) > 0)
                strcat(manifest->dependencies, ";");
            strcat(manifest->dependencies, value);
        }
    }
    
    fclose(f);
    return 0;
}

// ============================================================================
// RECHERCHE DE PACKAGE
// ============================================================================

int search_package(const char *name, char *version, char *url, char *author, int *downloads) {
    load_repositories();
    
    for (int i = 0; i < repo_count; i++) {
        if (!repositories[i].enabled) continue;
        
        debug_print("Searching in %s: %s/v5.2/package/%s", 
                   repositories[i].name, repositories[i].url, name);
        
        CURL *curl = curl_easy_init();
        if (!curl) continue;
        
        char search_url[512];
        snprintf(search_url, sizeof(search_url), "%s/v5.2/package/%s", 
                 repositories[i].url, name);
        
        struct curl_response resp = {0};
        curl_easy_setopt(curl, CURLOPT_URL, search_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res == CURLE_OK && resp.data) {
            struct json_object *parsed = json_tokener_parse(resp.data);
            if (parsed) {
                struct json_object *package_obj;
                if (json_object_object_get_ex(parsed, "package", &package_obj)) {
                    struct json_object *tmp;
                    
                    if (json_object_object_get_ex(package_obj, "version", &tmp))
                        strcpy(version, json_object_get_string(tmp));
                    else
                        strcpy(version, "0.0.0");
                    
                    if (author && json_object_object_get_ex(package_obj, "author", &tmp))
                        strcpy(author, json_object_get_string(tmp));
                    
                    if (downloads && json_object_object_get_ex(package_obj, "downloads", &tmp))
                        *downloads = json_object_get_int(tmp);
                    
                    // Construire l'URL de téléchargement
                    const char *release = "r0";
                    const char *arch = "x86_64";
                    
                    if (json_object_object_get_ex(package_obj, "release", &tmp))
                        release = json_object_get_string(tmp);
                    if (json_object_object_get_ex(package_obj, "arch", &tmp))
                        arch = json_object_get_string(tmp);
                    
                    snprintf(url, 512, "%s/download/public/%s/%s/%s/%s", 
                             repositories[i].url, name, version, release, arch);
                    
                    json_object_put(parsed);
                    free(resp.data);
                    return 0;
                }
                json_object_put(parsed);
            }
            free(resp.data);
        }
    }
    
    return -1;
}

// ============================================================================
// TÉLÉCHARGEMENT
// ============================================================================

int download_package(const char *url, const char *output_path) {
    debug_print("Downloading from: %s", url);
    
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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || http_code != 200) {
        unlink(output_path);
        return -1;
    }
    
    // Vérifier la taille
    struct stat st;
    if (stat(output_path, &st) != 0 || st.st_size == 0) {
        unlink(output_path);
        return -1;
    }
    
    return 0;
}

// ============================================================================
// EXTRACTION AVEC TAR
// ============================================================================

int extract_package(const char *package_path, const char *extract_dir) {
    char cmd[1024];
    
    mkdir(extract_dir, 0755);
    
    // Essayer différents formats tar
    const char *tar_options[] = {
        "tar -xzf '%s' -C '%s' 2>/dev/null",
        "tar -xf '%s' -C '%s' 2>/dev/null",
        "tar -xzf '%s' -C '%s' --strip-components=1 2>/dev/null",
        NULL
    };
    
    for (int i = 0; tar_options[i] != NULL; i++) {
        snprintf(cmd, sizeof(cmd), tar_options[i], package_path, extract_dir);
        debug_print("Trying: %s", cmd);
        
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

// ============================================================================
// INSTALLATION AVEC MANIFEST.TOML
// ============================================================================

int install_from_manifest(const char *extract_dir, manifest_t *manifest) {
    print_step("Installing from Manifest.toml");
    
    // Chercher les binaires
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "find '%s' -type f -executable -exec cp {} /usr/local/bin/ \\; 2>/dev/null", 
             extract_dir);
    system(cmd);
    
    // Chercher les bibliothèques
    snprintf(cmd, sizeof(cmd),
             "find '%s' -name '*.so*' -exec cp {} /usr/local/lib/ \\; 2>/dev/null",
             extract_dir);
    system(cmd);
    
    // Chercher les headers
    snprintf(cmd, sizeof(cmd),
             "find '%s' -name '*.h' -exec cp {} /usr/local/include/ \\; 2>/dev/null",
             extract_dir);
    system(cmd);
    
    // Exécuter install.sh s'il existe
    char install_script[512];
    snprintf(install_script, sizeof(install_script), "%s/install.sh", extract_dir);
    
    if (access(install_script, F_OK) == 0) {
        print_step("Running install.sh");
        chmod(install_script, 0755);
        
        char current_dir[1024];
        getcwd(current_dir, sizeof(current_dir));
        chdir(extract_dir);
        
        int ret = system("./install.sh");
        chdir(current_dir);
        
        if (ret != 0) {
            print_warning("install.sh exited with code %d", ret);
        }
    }
    
    print_success("Installation completed");
    return 0;
}
// ============================================================================
// INSTALLATION PRINCIPALE
// ============================================================================
int install_package(const char *name, const char *version_specific) {
    char version[64] = "";
    char url[512] = "";
    char author[256] = "";
    int downloads = 0;
    
    print_step("Searching for %s", name);
    
    if (search_package(name, version, url, author, &downloads) != 0) {
        print_error("Package '%s' not found", name);
        return -1;
    }
    
    print_success("Found %s version %s", name, version);
    print_info("  Author: %s", author);
    print_info("  Downloads: %d", downloads);
    
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s.tar.bool", name, version);
    
    print_step("Downloading");
    if (download_package(url, tmp_path) != 0) {
        print_error("Download failed");
        return -1;
    }
    print_success("Download complete");
    
    char extract_dir[512];
    snprintf(extract_dir, sizeof(extract_dir), "/tmp/apkm_extract_%d", getpid());
    
    print_step("Extracting");
    if (extract_package(tmp_path, extract_dir) != 0) {
        print_error("Extraction failed");
        unlink(tmp_path);
        return -1;
    }
    print_success("Extraction complete");
    
    // Chercher Manifest.toml
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/Manifest.toml", extract_dir);
    
    int use_legacy = 0;
    
    if (access(manifest_path, F_OK) == 0) {
        print_info("Found Manifest.toml");
        
        manifest_t manifest;
        if (parse_manifest(manifest_path, &manifest) == 0) {
            if (strlen(manifest.description) > 0) {
                print_info("Description: %s", manifest.description);
            }
            install_from_manifest(extract_dir, &manifest);
        } else {
            print_warning("Failed to parse Manifest.toml");
            use_legacy = 1;
        }
    } else {
        print_warning("No Manifest.toml found, using legacy installation");
        use_legacy = 1;
    }
    
    if (use_legacy) {
        // Installation à l'ancienne
        char legacy_cmd[1024];
        snprintf(legacy_cmd, sizeof(legacy_cmd), 
                 "cp '%s'/* /usr/local/bin/ 2>/dev/null || "
                 "cp '%s'/usr/bin/* /usr/local/bin/ 2>/dev/null", 
                 extract_dir, extract_dir);
        system(legacy_cmd);
    }
    
    // Nettoyer
    char cleanup_cmd[1024];
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf %s", extract_dir);
    system(cleanup_cmd);
    unlink(tmp_path);
    
    print_success("Package %s installed", name);
    return 0;
}

// ============================================================================
// LISTE DES PACKAGES INSTALLÉS
// ============================================================================

int cmd_list_installed(void) {
    printf("\n📦 Installed packages:\n");
    printf("──────────────────────\n");
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), 
             "ls -1 /usr/local/bin/ 2>/dev/null | "
             "while read f; do "
             "  if [ -x \"/usr/local/bin/$f\" ]; then "
             "    size=$(du -b \"/usr/local/bin/$f\" 2>/dev/null | cut -f1); "
             "    if [ -n \"$size\" ]; then "
             "      printf \"  • %-20s (%d KB)\\n\" \"$f\" $((size/1024)); "
             "    else "
             "      printf \"  • %s\\n\" \"$f\"; "
             "    fi; "
             "  fi; "
             "done | sort");
    
    fflush(stdout);
    system(cmd);
    
    return 0;
}

// ============================================================================
// LISTE DES REPOSITORIES
// ============================================================================

int cmd_list_repos(void) {
    load_repositories();
    
    printf("\n📋 Configured repositories:\n");
    printf("────────────────────────────\n");
    
    for (int i = 0; i < repo_count; i++) {
        printf("  %s %s (priority %d)\n", 
               repositories[i].enabled ? "✓" : "✗",
               repositories[i].name,
               repositories[i].priority);
    }
    
    return 0;
}

// ============================================================================
// AIDE
// ============================================================================

void print_help(void) {
    printf("\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APKM - Zarch Hub Package Manager\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    printf("USAGE:\n");
    printf("  apkm <command> [arguments]\n\n");
    
    printf("COMMANDS:\n");
    printf("  install <pkg>        Install a package from repository\n");
    printf("  search <term>        Search for packages\n");
    printf("  list                  List installed packages\n");
    printf("  repo list             List configured repositories\n");
    printf("  help                   Show this help\n\n");
    
    printf("OPTIONS:\n");
    printf("  --debug               Enable debug output\n");
    printf("  --quiet               Suppress output\n\n");
    
    printf("EXAMPLES:\n");
    printf("  apkm install nginx\n");
    printf("  apkm search database\n");
    printf("  apkm list\n");
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
    
    // Créer les répertoires nécessaires
    mkdir("/usr/local/share/apkm", 0755);
    mkdir(APKM_LOCAL_DB_PATH, 0755);
    mkdir(APKM_CACHE_PATH, 0755);
    
    curl_global_init(CURL_GLOBAL_ALL);
    
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
            result = install_package(argv[2], NULL);
        }
    }
    else if (strcmp(argv[1], "search") == 0) {
        if (argc < 3) {
            print_error("Missing search term");
            result = 1;
        } else {
            char ver[64], url[512], author[256];
            int downloads = 0;
            if (search_package(argv[2], ver, url, author, &downloads) == 0) {
                print_success("Package %s found (version %s)", argv[2], ver);
                printf("  Author: %s\n", author);
                printf("  Downloads: %d\n", downloads);
            } else {
                print_error("Package '%s' not found", argv[2]);
            }
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
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
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
