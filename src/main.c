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

// Rechercher un package
int search_package(const char *name, char *version, char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char search_url[512];
    snprintf(search_url, sizeof(search_url), "%s/package/search?q=%s", ZARCH_API_URL, name);
    
    struct curl_response resp = {0};
    curl_easy_setopt(curl, CURLOPT_URL, search_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK || !resp.data) {
        free(resp.data);
        return -1;
    }
    
    struct json_object *parsed = json_tokener_parse(resp.data);
    int found = -1;
    
    if (parsed) {
        struct json_object *results;
        if (json_object_object_get_ex(parsed, "results", &results)) {
            int len = json_object_array_length(results);
            for (int i = 0; i < len; i++) {
                struct json_object *pkg = json_object_array_get_idx(results, i);
                struct json_object *pkg_name;
                if (json_object_object_get_ex(pkg, "name", &pkg_name)) {
                    if (strcmp(json_object_get_string(pkg_name), name) == 0) {
                        struct json_object *pkg_ver;
                        if (json_object_object_get_ex(pkg, "version", &pkg_ver)) {
                            strcpy(version, json_object_get_string(pkg_ver));
                        }
                        found = 0;
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
    }
    
    return found;
}

// Télécharger un package
int download_package(const char *url, const char *output_path) {
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

// Installer un package
int install_package(const char *name, const char *version) {
    char url[512];
    char ver[64] = "";
    
    printf("[APKM] 🔍 Searching for %s...\n", name);
    
    if (search_package(name, ver, url) != 0) {
        printf("[APKM] ❌ Package %s not found\n", name);
        return -1;
    }
    
    printf("[APKM] ✅ Found version %s\n", ver);
    
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s.tar.bool", name, ver);
    
    printf("[APKM] 📥 Downloading...\n");
    if (download_package(url, tmp_path) != 0) {
        printf("[APKM] ❌ Download failed\n");
        return -1;
    }
    
    printf("[APKM] 📦 Installing...\n");
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tar -xzf %s -C /usr/local/bin 2>/dev/null", tmp_path);
    
    if (system(cmd) == 0) {
        printf("[APKM] ✅ Package %s installed successfully\n", name);
        unlink(tmp_path);
        return 0;
    }
    
    // Alternative: chercher dans usr/bin
    snprintf(cmd, sizeof(cmd), 
             "mkdir -p /tmp/apkm_extract && "
             "tar -xzf %s -C /tmp/apkm_extract && "
             "cp /tmp/apkm_extract/usr/bin/* /usr/local/bin/ 2>/dev/null && "
             "rm -rf /tmp/apkm_extract", tmp_path);
    
    if (system(cmd) == 0) {
        printf("[APKM] ✅ Package %s installed successfully\n", name);
        unlink(tmp_path);
        return 0;
    }
    
    printf("[APKM] ❌ Installation failed\n");
    unlink(tmp_path);
    return -1;
}

void print_help() {
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APKM - Zarch Hub Package Manager\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("USAGE:\n");
    printf("  apkm install <package>   Install a package from Zarch Hub\n");
    printf("  apkm search <package>    Search for a package\n");
    printf("  apkm list                List installed packages\n");
    printf("\nEXAMPLES:\n");
    printf("  apkm install zarch-utils\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    
    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            printf("[APKM] ❌ Missing package name\n");
            return 1;
        }
        return install_package(argv[2], NULL);
    }
    else if (strcmp(argv[1], "search") == 0) {
        if (argc < 3) {
            printf("[APKM] ❌ Missing search term\n");
            return 1;
        }
        char ver[64], url[512];
        if (search_package(argv[2], ver, url) == 0) {
            printf("[APKM] ✅ Package %s found (version %s)\n", argv[2], ver);
        } else {
            printf("[APKM] ❌ Package %s not found\n", argv[2]);
        }
        return 0;
    }
    else if (strcmp(argv[1], "list") == 0) {
        printf("[APKM] Installed packages:\n");
        system("ls -1 /usr/local/bin/ 2>/dev/null | grep -v '^$' | sed 's/^/  • /'");
        return 0;
    }
    else {
        printf("[APKM] ❌ Unknown command: %s\n", argv[1]);
        print_help();
        return 1;
    }
    
    return 0;
}
