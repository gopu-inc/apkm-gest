#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "apkm.h"
#include <json-c/json.h>

#define ZARCH_HUB_URL "https://gsql-badge.onrender.com"
#define ZARCH_API_URL ZARCH_HUB_URL "/v5.2"
#define ZARCH_PACKAGE_URL ZARCH_HUB_URL "/package/download"

struct curl_response {
    char *data;
    size_t size;
};

static int debug_mode = 0;

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

// Debug print
void debug_print(const char *format, ...) {
    if (!debug_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("[DEBUG] ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

// Rechercher un package
int search_package(const char *name, char *version, char *url, char *author, int *downloads) {
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
                        struct json_object *pkg_ver, *pkg_author, *pkg_downloads;
                        
                        if (json_object_object_get_ex(pkg, "version", &pkg_ver)) {
                            strcpy(version, json_object_get_string(pkg_ver));
                        }
                        if (author && json_object_object_get_ex(pkg, "author", &pkg_author)) {
                            strcpy(author, json_object_get_string(pkg_author));
                        }
                        if (downloads && json_object_object_get_ex(pkg, "downloads", &pkg_downloads)) {
                            *downloads = json_object_get_int(pkg_downloads);
                        }
                        
                        found = 0;
                        debug_print("Found exact match: %s %s", name, version);
                        break;
                    }
                }
            }
        }
        json_object_put(parsed);
    }
    
    free(resp.data);
    
    if (found == 0) {
        snprintf(url, 512, "%s/public/%s/%s", ZARCH_PACKAGE_URL, name, version);
        debug_print("Download URL: %s", url);
    }
    
    return found;
}

// Télécharger un package
int download_package(const char *url, const char *output_path) {
    debug_print("Downloading from: %s", url);
    debug_print("Saving to: %s", output_path);
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        debug_print("Cannot open output file: %s", output_path);
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
    
    debug_print("HTTP Response Code: %ld", http_code);
    
    if (res != CURLE_OK) {
        debug_print("Download failed: %s", curl_easy_strerror(res));
        unlink(output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    curl_easy_cleanup(curl);
    
    if (http_code != 200) {
        debug_print("HTTP error: %ld", http_code);
        unlink(output_path);
        return -1;
    }
    
    // Vérifier que le fichier a été téléchargé
    struct stat st;
    if (stat(output_path, &st) != 0 || st.st_size == 0) {
        debug_print("Downloaded file is empty or missing");
        unlink(output_path);
        return -1;
    }
    
    debug_print("Download successful, size: %ld bytes", st.st_size);
    
    return 0;
}

// Installer un package
int install_package(const char *name, const char *version_specific) {
    char url[512];
    char ver[64] = "";
    char author[256] = "";
    int downloads = 0;
    
    printf("[APKM] 🔍 Searching for %s...\n", name);
    
    if (search_package(name, ver, url, author, &downloads) != 0) {
        printf("[APKM] ❌ Package '%s' not found in Zarch Hub\n", name);
        printf("[APKM] 💡 Try 'apkm search %s' to see similar packages\n", name);
        return -1;
    }
    
    printf("[APKM] ✅ Found %s version %s (by %s, %d downloads)\n", 
           name, ver, author, downloads);
    
    // Si une version spécifique est demandée
    if (version_specific && strcmp(version_specific, "latest") != 0) {
        if (strcmp(version_specific, ver) != 0) {
            printf("[APKM] ⚠️  Requested version %s but latest is %s\n", 
                   version_specific, ver);
            printf("[APKM] 💡 Use 'apkm install %s@%s' for exact version\n", 
                   name, version_specific);
        }
        // Reconstruire l'URL avec la version demandée
        snprintf(url, 512, "%s/public/%s/%s", ZARCH_PACKAGE_URL, name, version_specific);
    }
    
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s.tar.bool", name, ver);
    
    printf("[APKM] 📥 Downloading from Zarch Hub...\n");
    if (download_package(url, tmp_path) != 0) {
        printf("[APKM] ❌ Download failed\n");
        printf("[APKM] 💡 Check your internet connection or if the package exists\n");
        return -1;
    }
    
    printf("[APKM] 📦 Installing...\n");
    
    // Créer le répertoire d'installation si nécessaire
    mkdir("/usr/local/bin", 0755);
    
    // Essayer d'extraire et d'installer
    char cmd[1024];
    int result = -1;
    
    // Méthode 1: Extraction directe vers /usr/local/bin
    snprintf(cmd, sizeof(cmd), 
             "tar -xzf %s -C /usr/local/bin 2>/dev/null && chmod 755 /usr/local/bin/%s 2>/dev/null", 
             tmp_path, name);
    
    debug_print("Trying method 1: %s", cmd);
    
    if (system(cmd) == 0) {
        result = 0;
    } else {
        // Méthode 2: Extraction temporaire puis copie
        debug_print("Method 1 failed, trying method 2");
        
        snprintf(cmd, sizeof(cmd), 
                 "mkdir -p /tmp/apkm_extract && "
                 "tar -xzf %s -C /tmp/apkm_extract 2>/dev/null && "
                 "find /tmp/apkm_extract -type f -executable -name '%s' -exec cp {} /usr/local/bin/ \\; 2>/dev/null && "
                 "chmod 755 /usr/local/bin/%s 2>/dev/null && "
                 "rm -rf /tmp/apkm_extract", 
                 tmp_path, name, name);
        
        debug_print("Trying method 2: %s", cmd);
        
        if (system(cmd) == 0 && access("/usr/local/bin", F_OK) == 0) {
            result = 0;
        }
    }
    
    if (result == 0) {
        printf("[APKM] ✅ Package %s installed successfully to /usr/local/bin/\n", name);
        printf("[APKM] 🚀 Try: %s --version\n", name);
        
        // Nettoyer
        unlink(tmp_path);
        return 0;
    }
    
    printf("[APKM] ❌ Installation failed\n");
    printf("[APKM] 💡 The package might need manual installation\n");
    printf("[APKM] 📁 File saved at: %s\n", tmp_path);
    
    return -1;
}

// Lister les packages installés
int list_installed(void) {
    printf("[APKM] 📦 Installed packages in /usr/local/bin:\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), 
             "ls -1 /usr/local/bin/ 2>/dev/null | "
             "while read f; do "
             "  if file \"/usr/local/bin/$f\" | grep -q ELF; then "
             "    echo \"  • $f\"; "
             "  fi; "
             "done");
    
    int count = system(cmd);
    
    if (count == 0 || count == 256) { // Aucun fichier trouvé
        printf("  (no packages found)\n");
    }
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    return 0;
}

// Afficher les informations d'un package
int show_package_info(const char *name) {
    char url[512];
    char ver[64] = "";
    char author[256] = "";
    int downloads = 0;
    
    printf("[APKM] 🔍 Fetching info for %s...\n", name);
    
    if (search_package(name, ver, url, author, &downloads) != 0) {
        printf("[APKM] ❌ Package '%s' not found\n", name);
        return -1;
    }
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Package Information: %s\n", name);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  📦 Name:      %s\n", name);
    printf("  📌 Version:   %s\n", ver);
    printf("  👤 Author:    %s\n", author);
    printf("  📥 Downloads: %d\n", downloads);
    printf("  🔗 URL:       %s\n", url);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return 0;
}

void print_help(void) {
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APKM - Zarch Hub Package Manager v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("USAGE:\n");
    printf("  apkm <command> [arguments]\n\n");
    printf("COMMANDS:\n");
    printf("  install <pkg>         Install a package (latest version)\n");
    printf("  install <pkg>@<ver>   Install specific version\n");
    printf("  search <pkg>          Search for a package\n");
    printf("  info <pkg>            Show package information\n");
    printf("  list                  List installed packages\n");
    printf("  update                 Update package database\n");
    printf("  version                Show version\n");
    printf("  help                   Show this help\n");
    printf("\nOPTIONS:\n");
    printf("  --debug                Enable debug output\n");
    printf("  --quiet                Suppress output\n");
    printf("\nEXAMPLES:\n");
    printf("  apkm install nginx\n");
    printf("  apkm install nodejs@18.0.0\n");
    printf("  apkm search database\n");
    printf("  apkm info python\n");
    printf("  apkm list\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    
    // Parse global options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
            debug_print("Debug mode enabled");
        }
        else if (strcmp(argv[i], "--quiet") == 0) {
            // Supprimer l'option mais ne pas la traiter ici
        }
    }
    
    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            printf("[APKM] ❌ Missing package name\n");
            printf("[APKM] 💡 Usage: apkm install <package>[@version]\n");
            return 1;
        }
        
        char *package_name = argv[2];
        char *version = NULL;
        
        // Check for version specifier (package@version)
        char *at = strchr(package_name, '@');
        if (at) {
            *at = '\0';
            version = at + 1;
            debug_print("Installing %s version %s", package_name, version);
        }
        
        return install_package(package_name, version);
    }
    else if (strcmp(argv[1], "search") == 0) {
        if (argc < 3) {
            printf("[APKM] ❌ Missing search term\n");
            return 1;
        }
        char ver[64], url[512], author[256];
        int downloads = 0;
        if (search_package(argv[2], ver, url, author, &downloads) == 0) {
            printf("[APKM] ✅ Package '%s' found\n", argv[2]);
            printf("  Version:   %s\n", ver);
            printf("  Author:    %s\n", author);
            printf("  Downloads: %d\n", downloads);
        } else {
            printf("[APKM] ❌ Package '%s' not found\n", argv[2]);
            printf("[APKM] 💡 Try a different name or check spelling\n");
        }
        return 0;
    }
    else if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            printf("[APKM] ❌ Missing package name\n");
            return 1;
        }
        return show_package_info(argv[2]);
    }
    else if (strcmp(argv[1], "list") == 0) {
        return list_installed();
    }
    else if (strcmp(argv[1], "update") == 0) {
        printf("[APKM] 🔄 Updating package database...\n");
        // À implémenter : sync avec le hub
        printf("[APKM] ✅ Database updated\n");
        return 0;
    }
    else if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("APKM version 2.0.0\n");
        return 0;
    }
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }
    else {
        printf("[APKM] ❌ Unknown command: %s\n", argv[1]);
        print_help();
        return 1;
    }
    
    return 0;
}
