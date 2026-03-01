#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "apkm.h"

#define MAX_RESULTS 100
#define PROGRESS_WIDTH 50

// Structure pour la progression
typedef struct {
    double last_progress;
    char filename[256];
    time_t start_time;
    double download_speed;
    curl_off_t last_dlnow;
    time_t last_time;
} download_context_t;

// Structure pour la réponse curl
struct curl_response {
    char *data;
    size_t size;
};

// ============================================================================
// Callbacks curl
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
// Barre de progression
// ============================================================================

void show_progress(double percentage, const char *filename, double speed) {
    int bar_width = PROGRESS_WIDTH;
    int pos = (int)(percentage * bar_width / 100.0);
    
    printf("\r[");
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) printf("=");
        else if (i == pos && percentage < 100.0) printf(">");
        else printf(" ");
    }
    
    if (percentage >= 100.0) {
        printf("] %3.0f%% %s - Complete        \n", percentage, filename);
    } else {
        printf("] %3.0f%% %s - %.1f KB/s      ", 
               percentage, filename, speed / 1024.0);
    }
    fflush(stdout);
}

int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, 
                       curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    
    download_context_t *ctx = (download_context_t *)clientp;
    
    if (dltotal > 0) {
        double percentage = (double)dlnow / (double)dltotal * 100.0;
        
        time_t now = time(NULL);
        if (now > ctx->last_time) {
            curl_off_t diff = dlnow - ctx->last_dlnow;
            ctx->download_speed = (double)diff / (now - ctx->last_time);
            ctx->last_dlnow = dlnow;
            ctx->last_time = now;
        }
        
        if (percentage - ctx->last_progress >= 1.0 || percentage >= 100.0) {
            show_progress(percentage, ctx->filename, ctx->download_speed);
            ctx->last_progress = percentage;
        }
    }
    return 0;
}

// ============================================================================
// ZARCH HUB FUNCTIONS
// ============================================================================

int zarch_download(const char* name, const char* version, const char* arch, 
                   const char* output_path) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/public/%s/%s", 
             ZARCH_PACKAGE_URL, name, version);
    
    printf("[ZARCH] Downloading %s %s...\n", name, version);
    
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return -1;
    }
    
    download_context_t ctx = {
        .last_progress = 0,
        .last_time = time(NULL),
        .last_dlnow = 0
    };
    strncpy(ctx.filename, name, sizeof(ctx.filename) - 1);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "\n[ZARCH] Download failed: %s\n", curl_easy_strerror(res));
        unlink(output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (http_code != 200) {
        fprintf(stderr, "\n[ZARCH] HTTP error: %ld\n", http_code);
        unlink(output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    printf("[ZARCH] Download complete\n");
    curl_easy_cleanup(curl);
    
    return 0;
}

int zarch_search(const char* query, zarch_package_t* results, int max_results) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct curl_response resp = {0};
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/search?q=%s", ZARCH_API_URL, query);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    int count = 0;
    if (res == CURLE_OK && resp.data) {
        struct json_object *parsed = json_tokener_parse(resp.data);
        if (parsed) {
            struct json_object *results_obj;
            if (json_object_object_get_ex(parsed, "results", &results_obj)) {
                int len = json_object_array_length(results_obj);
                for (int i = 0; i < len && i < max_results; i++) {
                    struct json_object *pkg = json_object_array_get_idx(results_obj, i);
                    
                    struct json_object *name, *version, *author, *downloads;
                    
                    if (json_object_object_get_ex(pkg, "name", &name))
                        strcpy(results[count].name, json_object_get_string(name));
                    if (json_object_object_get_ex(pkg, "version", &version))
                        strcpy(results[count].version, json_object_get_string(version));
                    if (json_object_object_get_ex(pkg, "author", &author))
                        strcpy(results[count].author, json_object_get_string(author));
                    if (json_object_object_get_ex(pkg, "downloads", &downloads))
                        results[count].downloads = json_object_get_int(downloads);
                    
                    count++;
                }
            }
            json_object_put(parsed);
        }
    }
    
    curl_easy_cleanup(curl);
    free(resp.data);
    return count;
}

int zarch_list_repos(output_format_t format) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct curl_response resp = {0};
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/search?q=", ZARCH_API_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && resp.data) {
        struct json_object *parsed = json_tokener_parse(resp.data);
        if (parsed) {
            struct json_object *results_obj;
            if (json_object_object_get_ex(parsed, "results", &results_obj)) {
                int len = json_object_array_length(results_obj);
                
                if (format == OUTPUT_JSON) {
                    printf("%s\n", resp.data);
                } else if (format == OUTPUT_TEXT) {
                    printf("\n📦 ZARCH HUB PACKAGES\n");
                    printf("═══════════════════════════════════════════\n");
                    printf("%-20s %-12s %-15s %-10s\n", "NAME", "VERSION", "AUTHOR", "DOWNLOADS");
                    printf("───────────────────────────────────────────\n");
                    
                    for (int i = 0; i < len; i++) {
                        struct json_object *pkg = json_object_array_get_idx(results_obj, i);
                        struct json_object *name, *version, *author, *downloads;
                        
                        if (json_object_object_get_ex(pkg, "name", &name) &&
                            json_object_object_get_ex(pkg, "version", &version)) {
                            const char *n = json_object_get_string(name);
                            const char *v = json_object_get_string(version);
                            const char *a = "";
                            int d = 0;
                            
                            if (json_object_object_get_ex(pkg, "author", &author))
                                a = json_object_get_string(author);
                            if (json_object_object_get_ex(pkg, "downloads", &downloads))
                                d = json_object_get_int(downloads);
                            
                            printf(" • %-20s %-12s %-15s %-10d\n", n, v, a, d);
                        }
                    }
                    printf("═══════════════════════════════════════════\n");
                    printf(" Total: %d packages\n", len);
                }
            }
            json_object_put(parsed);
        }
    }
    
    curl_easy_cleanup(curl);
    free(resp.data);
    return 0;
}

// ============================================================================
// GITHUB FUNCTIONS (uniquement pour DATA.db)
// ============================================================================

int github_fetch_database(char* buffer, size_t buffer_size) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/DATA.db", GITHUB_RAW_URL);
    
    struct curl_response resp = {0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK && resp.data) {
        strncpy(buffer, resp.data, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        free(resp.data);
        return 0;
    }
    
    free(resp.data);
    return -1;
}

// ============================================================================
// INSTALLATION FUNCTIONS
// ============================================================================

void parse_package_spec(const char *spec, char *name, char *version, char *arch) {
    // Format: name@version ou name@version/arch
    char temp[256];
    strncpy(temp, spec, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    char *at_pos = strchr(temp, '@');
    if (at_pos) {
        *at_pos = '\0';
        strcpy(name, temp);
        
        char *slash_pos = strchr(at_pos + 1, '/');
        if (slash_pos) {
            *slash_pos = '\0';
            strcpy(version, at_pos + 1);
            strcpy(arch, slash_pos + 1);
        } else {
            strcpy(version, at_pos + 1);
            strcpy(arch, "x86_64");
        }
    } else {
        strcpy(name, spec);
        strcpy(version, "latest");
        strcpy(arch, "x86_64");
    }
}

int extract_package(const char *filepath, const char *dest_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -xzf '%s' -C '%s' 2>/dev/null", filepath, dest_path);
    
    if (system(cmd) == 0) return 0;
    
    snprintf(cmd, sizeof(cmd), "tar -xf '%s' -C '%s' 2>/dev/null", filepath, dest_path);
    if (system(cmd) == 0) return 0;
    
    return -1;
}

int run_install_script(const char *staging_path, const char *pkg_name) {
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
        snprintf(cmd, sizeof(cmd), "cp '%s' /usr/local/bin/ && chmod 755 /usr/local/bin/%s",
                 binary_path, pkg_name);
        return system(cmd);
    }
    
    return -1;
}

void register_package(const char *name, const char *version, const char *arch) {
    mkdir("/var/lib/apkm", 0755);
    
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "/var/lib/apkm/packages.db");
    
    FILE *db = fopen(db_path, "a");
    if (!db) db = fopen(db_path, "w");
    
    if (db) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char date_str[20];
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        fprintf(db, "%s|%s|%s|%lld|%s|/usr/local/bin/%s\n",
                name, version, arch, (long long)now, date_str, name);
        fclose(db);
        
        printf("[APKM] Package %s %s registered\n", name, version);
    }
}

// ============================================================================
// COMMANDES PRINCIPALES
// ============================================================================

int cmd_install(const char *source) {
    char pkg_name[256], pkg_version[64], pkg_arch[32];
    char local_file[512];
    
    parse_package_spec(source, pkg_name, pkg_version, pkg_arch);
    
    printf("[APKM] Installing %s %s (%s) from Zarch Hub\n", 
           pkg_name, pkg_version, pkg_arch);
    
    snprintf(local_file, sizeof(local_file), "/tmp/%s-%s.tar.gz", pkg_name, pkg_version);
    
    if (zarch_download(pkg_name, pkg_version, pkg_arch, local_file) != 0) {
        return -1;
    }
    
    const char *staging = "/tmp/apkm_install";
    mkdir(staging, 0755);
    
    printf("[APKM] Extracting package...\n");
    if (extract_package(local_file, staging) != 0) {
        fprintf(stderr, "[APKM] Extraction failed\n");
        unlink(local_file);
        return -1;
    }
    
    unlink(local_file);
    
    if (run_install_script(staging, pkg_name) == 0) {
        register_package(pkg_name, pkg_version, pkg_arch);
        printf("[APKM] ✅ Installation successful\n");
        printf("[APKM] Try: %s --version\n", pkg_name);
    } else {
        printf("[APKM] ❌ Installation failed\n");
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", staging);
    system(cmd);
    
    return 0;
}

int cmd_install_local(const char *filepath) {
    printf("[APKM] Installing local package: %s\n", filepath);
    
    // Extraire le nom du fichier
    char *basename = strrchr(filepath, '/');
    if (basename) basename++; else basename = (char*)filepath;
    
    char pkg_name[256] = "package";
    char *ext = strstr(basename, ".tar.bool");
    if (ext) {
        int len = ext - basename;
        strncpy(pkg_name, basename, len);
        pkg_name[len] = '\0';
    }
    
    const char *staging = "/tmp/apkm_install";
    mkdir(staging, 0755);
    
    if (extract_package(filepath, staging) != 0) {
        fprintf(stderr, "[APKM] Extraction failed\n");
        return -1;
    }
    
    if (run_install_script(staging, pkg_name) == 0) {
        register_package(pkg_name, "local", "x86_64");
        printf("[APKM] ✅ Local installation successful\n");
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", staging);
    system(cmd);
    
    return 0;
}

int cmd_list(void) {
    printf("[APKM] Installed packages:\n");
    printf("═══════════════════════════════════════════\n");
    
    FILE *db = fopen("/var/lib/apkm/packages.db", "r");
    if (!db) {
        printf(" No packages installed\n");
        return 0;
    }
    
    printf("%-20s %-12s %-10s %-20s\n", "NAME", "VERSION", "ARCH", "DATE");
    printf("───────────────────────────────────────────\n");
    
    char line[1024];
    int count = 0;
    
    while (fgets(line, sizeof(line), db)) {
        char name[256], version[64], arch[32], date_str[20];
        long long ts;
        
        if (sscanf(line, "%[^|]|%[^|]|%[^|]|%lld|%[^|]", 
                   name, version, arch, &ts, date_str) >= 4) {
            printf(" • %-20s %-12s %-10s %-20s\n", name, version, arch, date_str);
            count++;
        }
    }
    fclose(db);
    
    printf("═══════════════════════════════════════════\n");
    printf(" Total: %d packages\n", count);
    
    return 0;
}

int cmd_search(const char *query, output_format_t format) {
    zarch_package_t results[MAX_RESULTS];
    int count = zarch_search(query, results, MAX_RESULTS);
    
    if (format == OUTPUT_JSON) {
        printf("[\n");
        for (int i = 0; i < count; i++) {
            printf("  {\"name\":\"%s\",\"version\":\"%s\",\"author\":\"%s\",\"downloads\":%d}%s\n",
                   results[i].name, results[i].version, 
                   results[i].author, results[i].downloads,
                   i < count-1 ? "," : "");
        }
        printf("]\n");
    } else {
        printf("\n[ZARCH] Search results for '%s':\n", query);
        printf("═══════════════════════════════════════════\n");
        printf("%-20s %-12s %-15s %-10s\n", "NAME", "VERSION", "AUTHOR", "DOWNLOADS");
        printf("───────────────────────────────────────────\n");
        
        for (int i = 0; i < count; i++) {
            printf(" • %-20s %-12s %-15s %-10d\n",
                   results[i].name, results[i].version,
                   results[i].author, results[i].downloads);
        }
        printf("═══════════════════════════════════════════\n");
    }
    
    return 0;
}

int cmd_repos(output_format_t format) {
    return zarch_list_repos(format);
}

int cmd_update(output_format_t format) {
    char db_content[65536];
    if (github_fetch_database(db_content, sizeof(db_content)) == 0) {
        printf("[APKM] Repository database updated\n");
        if (format == OUTPUT_JSON) {
            printf("%s\n", db_content);
        }
        return 0;
    }
    printf("[APKM] Failed to update database\n");
    return -1;
}

// ============================================================================
// HELP
// ============================================================================

void print_help(void) {
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  APKM - Zarch Package Manager v%s\n", APKM_VERSION);
    printf("═══════════════════════════════════════════════════════════════\n\n");
    printf("USAGE:\n");
    printf("  apkm <COMMAND> [OPTIONS]\n\n");
    printf("COMMANDS:\n");
    printf("  install <pkg>[@ver]    Install package from Zarch Hub\n");
    printf("  install <file>         Install local .tar.bool file\n");
    printf("  list                   List installed packages\n");
    printf("  search <query>         Search packages on Zarch Hub\n");
    printf("  repos                  List all packages on Zarch Hub\n");
    printf("  update                 Update repository database from GitHub\n");
    printf("\nOPTIONS:\n");
    printf("  -j, --json             JSON output\n");
    printf("  -t, --toml             TOML output\n");
    printf("  -y, --yaml             YAML output\n");
    printf("\nEXAMPLES:\n");
    printf("  apkm install apkm\n");
    printf("  apkm install apkm@2.0.0\n");
    printf("  apkm install apkm@2.0.0/armv7\n");
    printf("  apkm search database\n");
    printf("  apkm repos --json\n");
    printf("═══════════════════════════════════════════════════════════════\n");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    char *command = argv[1];
    output_format_t format = OUTPUT_TEXT;

    // Détection des formats
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0)
            format = OUTPUT_JSON;
        else if (strcmp(argv[i], "--toml") == 0 || strcmp(argv[i], "-t") == 0)
            format = OUTPUT_TOML;
        else if (strcmp(argv[i], "--yaml") == 0 || strcmp(argv[i], "-y") == 0)
            format = OUTPUT_YAML;
    }

    // Routage des commandes
    if (strcmp(command, "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[APKM] Missing package or file\n");
            return 1;
        }
        
        if (strstr(argv[2], ".tar.bool") || access(argv[2], F_OK) == 0) {
            return cmd_install_local(argv[2]);
        } else {
            return cmd_install(argv[2]);
        }
    }
    else if (strcmp(command, "list") == 0) {
        return cmd_list();
    }
    else if (strcmp(command, "search") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[APKM] Missing search query\n");
            return 1;
        }
        return cmd_search(argv[2], format);
    }
    else if (strcmp(command, "repos") == 0) {
        return cmd_repos(format);
    }
    else if (strcmp(command, "update") == 0) {
        return cmd_update(format);
    }
    else if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        print_help();
        return 0;
    }
    else {
        fprintf(stderr, "[APKM] Unknown command: %s\n", command);
        fprintf(stderr, "Try 'apkm --help'\n");
        return 1;
    }

    return 0;
}
