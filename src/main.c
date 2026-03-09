#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <libgen.h>
#include "apkm.h"
#include <json-c/json.h>

#define ZARCH_HUB_URL "https://gsql-badge.onrender.com"
#define ZARCH_API_URL ZARCH_HUB_URL "/v5.2"

struct curl_response {
    char *data;
    size_t size;
};

static int debug_mode = 0;
static int quiet_mode = 0;
static char config_dir[512] = "/usr/local/share/apkm";

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
    int downloads;
    long size;
} package_info_t;

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
    vprintf(format, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

void print_success(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("✅ ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

void print_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "❌ ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
    fflush(stderr);
}

void print_warning(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("⚠️  ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

// ============================================================================
// RECHERCHE DE PACKAGE
// ============================================================================

int search_package(const char *name, package_info_t *info) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char search_url[512];
    snprintf(search_url, sizeof(search_url), "%s/package/search?q=%s", ZARCH_API_URL, name);
    
    debug_print("Searching URL: %s", search_url);
    
    struct curl_response resp = {0};
    curl_easy_setopt(curl, CURLOPT_URL, search_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || !resp.data) {
        debug_print("Search failed: %s", curl_easy_strerror(res));
        free(resp.data);
        return -1;
    }
    
    debug_print("Search response: %s", resp.data);
    
    struct json_object *parsed = json_tokener_parse(resp.data);
    int found = -1;
    
    if (parsed) {
        struct json_object *results;
        if (json_object_object_get_ex(parsed, "results", &results)) {
            int len = json_object_array_length(results);
            debug_print("Found %d results", len);
            
            for (int i = 0; i < len; i++) {
                struct json_object *pkg = json_object_array_get_idx(results, i);
                struct json_object *pkg_name;
                
                if (json_object_object_get_ex(pkg, "name", &pkg_name)) {
                    const char *found_name = json_object_get_string(pkg_name);
                    debug_print("Checking package: %s", found_name);
                    
                    if (strcmp(found_name, name) == 0) {
                        // Récupérer toutes les infos
                        struct json_object *tmp;
                        
                        strcpy(info->name, found_name);
                        
                        if (json_object_object_get_ex(pkg, "version", &tmp))
                            strcpy(info->version, json_object_get_string(tmp));
                        if (json_object_object_get_ex(pkg, "release", &tmp))
                            strcpy(info->release, json_object_get_string(tmp));
                        else
                            strcpy(info->release, "r0");
                            
                        if (json_object_object_get_ex(pkg, "arch", &tmp))
                            strcpy(info->arch, json_object_get_string(tmp));
                        else
                            strcpy(info->arch, "x86_64");
                            
                        if (json_object_object_get_ex(pkg, "author", &tmp))
                            strcpy(info->author, json_object_get_string(tmp));
                            
                        if (json_object_object_get_ex(pkg, "downloads", &tmp))
                            info->downloads = json_object_get_int(tmp);
                            
                        if (json_object_object_get_ex(pkg, "size", &tmp))
                            info->size = json_object_get_int(tmp);
                            
                        // Construire l'URL complète de téléchargement
                        snprintf(info->url, sizeof(info->url),
                                "%s/package/download/%s/%s/%s/%s/%s",
                                ZARCH_HUB_URL, "public", info->name, 
                                info->version, info->release, info->arch);
                        
                        debug_print("Found exact match: %s %s-%s (%s)", 
                                   info->name, info->version, info->release, info->arch);
                        debug_print("Download URL: %s", info->url);
                        
                        found = 0;
                        break;
                    }
                }
            }
        }
        json_object_put(parsed);
    }
    
    free(resp.data);
    return found;
}

// ============================================================================
// TÉLÉCHARGEMENT DE PACKAGE
// ============================================================================

int download_package(package_info_t *info, const char *output_path) {
    debug_print("Downloading from: %s", info->url);
    debug_print("Saving to: %s", output_path);
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        debug_print("Cannot open output file: %s", output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    // Barre de progression simple
    curl_easy_setopt(curl, CURLOPT_URL, info->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
    
    // Barre de progression si pas en mode quiet
    if (!quiet_mode && !debug_mode) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    
    debug_print("HTTP Response Code: %ld", http_code);
    
    if (res != CURLE_OK || http_code != 200) {
        debug_print("Download failed: %s", curl_easy_strerror(res));
        unlink(output_path);
        return -1;
    }
    
    // Vérifier la taille
    struct stat st;
    if (stat(output_path, &st) != 0 || st.st_size == 0) {
        debug_print("Downloaded file is empty");
        unlink(output_path);
        return -1;
    }
    
    // Vérifier le SHA256 si disponible
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
                } else {
                    debug_print("SHA256 verification passed");
                }
            }
            pclose(fp);
        }
    }
    
    debug_print("Download successful: %ld bytes", st.st_size);
    return 0;
}

// ============================================================================
// INSTALLATION DE PACKAGE
// ============================================================================

int extract_package(const char *package_path, const char *extract_dir) {
    char cmd[1024];
    
    // Créer le répertoire d'extraction
    mkdir(extract_dir, 0755);
    
    // Extraire l'archive
    snprintf(cmd, sizeof(cmd), "tar -xzf '%s' -C '%s' 2>/dev/null", package_path, extract_dir);
    debug_print("Extracting: %s", cmd);
    
    if (system(cmd) == 0) {
        return 0;
    }
    
    // Essayer sans gz si échec
    snprintf(cmd, sizeof(cmd), "tar -xf '%s' -C '%s' 2>/dev/null", package_path, extract_dir);
    debug_print("Retry without gz: %s", cmd);
    
    return system(cmd);
}

int install_binary(const char *source, const char *dest_dir, const char *binary_name) {
    char cmd[1024];
    
    // Créer le répertoire de destination
    mkdir(dest_dir, 0755);
    
    // Chercher le binaire dans l'arborescence
    snprintf(cmd, sizeof(cmd), 
             "find '%s' -type f -name '%s' -exec cp {} '%s/' \\; 2>/dev/null", 
             source, binary_name, dest_dir);
    
    debug_print("Finding binary: %s", cmd);
    system(cmd);
    
    // Chercher aussi dans usr/bin
    snprintf(cmd, sizeof(cmd), 
             "find '%s/usr/bin' -type f -name '%s' -exec cp {} '%s/' \\; 2>/dev/null", 
             source, binary_name, dest_dir);
    
    debug_print("Checking usr/bin: %s", cmd);
    system(cmd);
    
    // Chercher n'importe quel binaire exécutable
    snprintf(cmd, sizeof(cmd), 
             "find '%s' -type f -executable -exec cp {} '%s/' \\; 2>/dev/null", 
             source, dest_dir);
    
    debug_print("Copying all executables: %s", cmd);
    system(cmd);
    
    // Rendre exécutable
    snprintf(cmd, sizeof(cmd), "chmod -R 755 '%s/' 2>/dev/null", dest_dir);
    system(cmd);
    
    // Vérifier si le binaire a été installé
    char binary_path[512];
    snprintf(binary_path, sizeof(binary_path), "%s/%s", dest_dir, binary_name);
    
    return access(binary_path, X_OK) == 0 ? 0 : -1;
}

int run_install_script(const char *extract_dir, const char *package_name) {
    const char *scripts[] = {
        "install.sh", "INSTALL.sh", "post-install.sh", 
        "setup.sh", "configure.sh", "APKMBUILD", NULL
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
            chdir(current_dir);
            
            if (ret == 0) {
                print_success("Install script executed: %s", scripts[i]);
                return 0;
            }
        }
    }
    
    chdir(current_dir);
    return -1;
}

int install_package(package_info_t *info, const char *version_specific) {
    print_info("[APKM] 🔍 Searching for %s...", info->name);
    
    if (search_package(info->name, info) != 0) {
        print_error("Package '%s' not found in Zarch Hub", info->name);
        print_info("💡 Try 'apkm search %s' to see similar packages", info->name);
        return -1;
    }
    
    // Si une version spécifique est demandée
    if (version_specific && strcmp(version_specific, "latest") != 0) {
        if (strcmp(version_specific, info->version) != 0) {
            print_warning("Requested version %s but latest is %s", 
                         version_specific, info->version);
            print_info("💡 Use 'apkm install %s@%s' for exact version", 
                      info->name, version_specific);
        }
    }
    
    print_success("Found %s version %s-%s (%s)", 
                 info->name, info->version, info->release, info->arch);
    print_info("   Author: %s | Downloads: %d | Size: %.1f KB", 
              info->author, info->downloads, info->size / 1024.0);
    
    // Préparer le chemin temporaire
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s-%s.%s.tar.bool", 
             info->name, info->version, info->release, info->arch);
    
    print_info("[APKM] 📥 Downloading from Zarch Hub...");
    
    if (download_package(info, tmp_path) != 0) {
        print_error("Download failed");
        print_info("💡 Check your internet connection or if the package exists");
        return -1;
    }
    
    print_info("[APKM] 📦 Installing...");
    
    // Créer un répertoire d'extraction temporaire
    char extract_dir[512];
    snprintf(extract_dir, sizeof(extract_dir), "/tmp/apkm_extract_%s", info->name);
    
    // Nettoyer les anciennes extractions
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", extract_dir);
    system(cmd);
    
    // Extraire le package
    if (extract_package(tmp_path, extract_dir) != 0) {
        print_error("Failed to extract package");
        unlink(tmp_path);
        return -1;
    }
    
    debug_print("Package extracted to: %s", extract_dir);
    
    // Essayer d'exécuter un script d'installation
    int install_result = run_install_script(extract_dir, info->name);
    
    if (install_result != 0) {
        // Installer les binaires manuellement
        install_result = install_binary(extract_dir, "/usr/local/bin", info->name);
    }
    
    // Nettoyage
    snprintf(cmd, sizeof(cmd), "rm -rf %s", extract_dir);
    system(cmd);
    
    if (install_result == 0) {
        print_success("Package %s installed successfully to /usr/local/bin/", info->name);
        
        // Vérifier si le binaire est dans le PATH
        char which_cmd[256];
        snprintf(which_cmd, sizeof(which_cmd), "which %s > /dev/null 2>&1", info->name);
        
        if (system(which_cmd) == 0) {
            print_info("🚀 Try: %s --version", info->name);
        } else {
            print_warning("Binary not in PATH. You may need to add /usr/local/bin to your PATH");
            print_info("   export PATH=$PATH:/usr/local/bin");
        }
        
        // Sauvegarder dans la base locale
        unlink(tmp_path);
        return 0;
    }
    
    print_error("Installation failed");
    print_info("📁 Package file saved at: %s", tmp_path);
    print_info("💡 You can try manual installation from there");
    
    return -1;
}

// ============================================================================
// LISTE DES PACKAGES INSTALLÉS
// ============================================================================

int list_installed(void) {
    print_info("[APKM] 📦 Installed packages in /usr/local/bin:");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    char cmd[1024];
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
    int count = system(cmd);
    
    if (count == 0 || count == 256) {
        printf("  (no packages found)\n");
    }
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    return 0;
}

// ============================================================================
// AFFICHAGE DES INFOS D'UN PACKAGE
// ============================================================================

int show_package_info(const char *name) {
    package_info_t info;
    memset(&info, 0, sizeof(info));
    strcpy(info.name, name);
    
    print_info("[APKM] 🔍 Fetching info for %s...", name);
    
    if (search_package(name, &info) != 0) {
        print_error("Package '%s' not found", name);
        return -1;
    }
    
    // Récupérer la description si disponible
    CURL *curl = curl_easy_init();
    if (curl) {
        char url[512];
        snprintf(url, sizeof(url), "%s/package/%s", ZARCH_API_URL, name);
        
        struct curl_response resp = {0};
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK && resp.data) {
            struct json_object *parsed = json_tokener_parse(resp.data);
            if (parsed) {
                struct json_object *package_obj, *desc_obj;
                if (json_object_object_get_ex(parsed, "package", &package_obj)) {
                    if (json_object_object_get_ex(package_obj, "description", &desc_obj)) {
                        strcpy(info.description, json_object_get_string(desc_obj));
                    }
                }
                json_object_put(parsed);
            }
            free(resp.data);
        }
        curl_easy_cleanup(curl);
    }
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Package Information: %s\n", info.name);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  📦 Name:      %s\n", info.name);
    printf("  📌 Version:   %s-%s\n", info.version, info.release);
    printf("  🔧 Arch:      %s\n", info.arch);
    printf("  👤 Author:    %s\n", info.author);
    printf("  📥 Downloads: %d\n", info.downloads);
    printf("  📏 Size:      %.1f KB\n", info.size / 1024.0);
    if (strlen(info.description) > 0) {
        printf("  📝 Description: %s\n", info.description);
    }
    printf("  🔗 URL:       %s\n", info.url);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return 0;
}

// ============================================================================
// MISE À JOUR DE LA BASE
// ============================================================================

int update_database(void) {
    print_info("[APKM] 🔄 Updating package database...");
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/search?q=", ZARCH_API_URL);
    
    struct curl_response resp = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK && resp.data) {
        // Sauvegarder le cache local
        char cache_dir[512];
        snprintf(cache_dir, sizeof(cache_dir), "%s/cache", config_dir);
        mkdir(cache_dir, 0755);
        
        char cache_path[512];
        snprintf(cache_path, sizeof(cache_path), "%s/packages.json", cache_dir);
        
        FILE *f = fopen(cache_path, "w");
        if (f) {
            fprintf(f, "%s", resp.data);
            fclose(f);
            print_success("Database updated (%lu bytes)", strlen(resp.data));
        }
        
        free(resp.data);
        return 0;
    }
    
    print_error("Failed to update database");
    return -1;
}

// ============================================================================
// RECHERCHE AVANCÉE
// ============================================================================

int search_packages(const char *query) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/search?q=%s", ZARCH_API_URL, query);
    
    struct curl_response resp = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || !resp.data) {
        print_error("Search failed");
        free(resp.data);
        return -1;
    }
    
    struct json_object *parsed = json_tokener_parse(resp.data);
    if (!parsed) {
        free(resp.data);
        return -1;
    }
    
    struct json_object *results;
    if (!json_object_object_get_ex(parsed, "results", &results)) {
        json_object_put(parsed);
        free(resp.data);
        return -1;
    }
    
    int len = json_object_array_length(results);
    
    if (len == 0) {
        print_info("No packages found matching '%s'", query);
    } else {
        print_info("[APKM] 🔍 Search results for '%s':", query);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("%-20s %-12s %-10s %s\n", "NAME", "VERSION", "ARCH", "AUTHOR");
        printf("───────────────────────────────────────────\n");
        
        for (int i = 0; i < len; i++) {
            struct json_object *pkg = json_object_array_get_idx(results, i);
            struct json_object *name, *version, *author, *arch, *release;
            
            const char *n = "?", *v = "?", *a = "?", *r = "r0", *ar = "x86_64";
            
            if (json_object_object_get_ex(pkg, "name", &name))
                n = json_object_get_string(name);
            if (json_object_object_get_ex(pkg, "version", &version))
                v = json_object_get_string(version);
            if (json_object_object_get_ex(pkg, "author", &author))
                a = json_object_get_string(author);
            if (json_object_object_get_ex(pkg, "arch", &arch))
                ar = json_object_get_string(arch);
            if (json_object_object_get_ex(pkg, "release", &release))
                r = json_object_get_string(release);
            
            printf("  • %-20s %-12s %-10s %s\n", n, v, ar, a);
        }
        
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  Total: %d packages found\n", len);
    }
    
    json_object_put(parsed);
    free(resp.data);
    return 0;
}

// ============================================================================
// AIDE
// ============================================================================

void print_help(void) {
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APKM - Zarch Hub Package Manager v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("USAGE:\n");
    printf("  apkm <command> [arguments]\n\n");
    printf("COMMANDS:\n");
    printf("  install <pkg>         Install latest version\n");
    printf("  install <pkg>@<ver>   Install specific version\n");
    printf("  search <term>         Search for packages\n");
    printf("  info <pkg>            Show package information\n");
    printf("  list                  List installed packages\n");
    printf("  update                Update package database\n");
    printf("  version               Show version\n");
    printf("  help                  Show this help\n\n");
    printf("OPTIONS:\n");
    printf("  --debug                Enable debug output\n");
    printf("  --quiet                Suppress output\n\n");
    printf("EXAMPLES:\n");
    printf("  apkm install nginx\n");
    printf("  apkm install nodejs@18.0.0\n");
    printf("  apkm search database\n");
    printf("  apkm info python\n");
    printf("  apkm list\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    
    // Créer le répertoire de configuration
    mkdir(config_dir, 0755);
    mkdir("/usr/local/bin", 0755);
    
    // Initialisation globale
    curl_global_init(CURL_GLOBAL_ALL);
    
    // Parser les options globales d'abord
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
            break;  // Premier argument non-option
        }
    }
    
    // Décaler les arguments si nécessaire
    if (args_processed > 1) {
        argv += args_processed - 1;
        argc -= args_processed - 1;
    }
    
    debug_print("APKM v2.0.0 starting...");
    debug_print("Command: %s", argv[1]);
    
    // Traiter les commandes
    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            print_error("Missing package name");
            print_info("Usage: apkm install <package>[@version]");
            curl_global_cleanup();
            return 1;
        }
        
        char *package_name = argv[2];
        char *version = NULL;
        
        // Vérifier si version spécifiée (package@version)
        char *at = strchr(package_name, '@');
        if (at) {
            *at = '\0';
            version = at + 1;
            debug_print("Installing %s version %s", package_name, version);
        }
        
        package_info_t info;
        memset(&info, 0, sizeof(info));
        strcpy(info.name, package_name);
        
        int result = install_package(&info, version);
        curl_global_cleanup();
        return result;
    }
    else if (strcmp(argv[1], "search") == 0) {
        if (argc < 3) {
            print_error("Missing search term");
            curl_global_cleanup();
            return 1;
        }
        int result = search_packages(argv[2]);
        curl_global_cleanup();
        return result;
    }
    else if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            print_error("Missing package name");
            curl_global_cleanup();
            return 1;
        }
        int result = show_package_info(argv[2]);
        curl_global_cleanup();
        return result;
    }
    else if (strcmp(argv[1], "list") == 0) {
        int result = list_installed();
        curl_global_cleanup();
        return result;
    }
    else if (strcmp(argv[1], "update") == 0) {
        int result = update_database();
        curl_global_cleanup();
        return result;
    }
    else if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("APKM version 2.0.0\n");
        curl_global_cleanup();
        return 0;
    }
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        curl_global_cleanup();
        return 0;
    }
    else {
        print_error("Unknown command: %s", argv[1]);
        print_help();
        curl_global_cleanup();
        return 1;
    }
    
    curl_global_cleanup();
    return 0;
}
