#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include "apkm.h"
#include "security.h"

#define MAX_RESULTS 100
#define PROGRESS_WIDTH 50

// ============================================================================
// STRUCTURES
// ============================================================================

typedef struct {
    double last_progress;
    char filename[256];
    time_t start_time;
    double download_speed;
    curl_off_t last_dlnow;
    time_t last_time;
} download_context_t;

struct curl_response {
    char *data;
    size_t size;
};

// ============================================================================
// DÉCLARATIONS EXTERNES
// ============================================================================

extern int extract_package(const char *filepath, const char *dest_path);
extern int run_install_script(const char *staging_path, const char *pkg_name);
extern int db_register_installed(const char *name, const char *version, 
                                  const char *release, const char *arch,
                                  const char *binary_path);
extern int db_sync_from_github(void);
extern int db_search_packages(const char *pattern, package_t *results, int max_results);
extern int db_list_installed(package_t *results, int max_results);
extern package_t* db_get_package(const char *name, const char *version);
extern int download_package(const char *name, const char *version, const char *output_path);

// Déclarations Zarch (provenant de zarch.c)
extern int zarch_download(const char* name, const char* version, const char* arch, const char* output_path);
extern int zarch_search(const char* query, zarch_package_t* results, int max_results);
extern int zarch_list_repos(output_format_t format);
extern int zarch_login(const char *username, const char *password, char *token, size_t token_size);

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

static void show_progress(double percentage, const char *filename, double speed) {
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

static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, 
                              curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    
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
// COMMANDES PRINCIPALES
// ============================================================================

static void parse_package_spec(const char *spec, char *name, char *version, char *arch) {
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

static int cmd_install(const char *source) {
    char name[256], version[64], arch[32];
    char tmp_path[512];
    
    parse_package_spec(source, name, version, arch);
    
    printf("[APKM] Installing %s %s (%s) from Zarch Hub\n", name, version, arch);
    
    struct stat st;
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/packages.db", APKM_DB_PATH);
    
    if (stat(db_path, &st) != 0 || time(NULL) - st.st_mtime > 86400) {
        printf("[APKM] Database is old, syncing...\n");
        db_sync_from_github();
    }
    
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/%s-%s.tar.bool", name, version);
    
    if (download_package(name, version, tmp_path) != 0) {
        return -1;
    }
    
    const char *staging = "/tmp/apkm_install";
    mkdir(staging, 0755);
    
    printf("[APKM] Extracting package...\n");
    if (extract_package(tmp_path, staging) != 0) {
        fprintf(stderr, "[APKM] Extraction failed\n");
        unlink(tmp_path);
        return -1;
    }
    
    unlink(tmp_path);
    
    if (run_install_script(staging, name) == 0) {
        db_register_installed(name, version, "r0", arch, "/usr/local/bin");
        printf("[APKM] ✅ Installation successful\n");
        printf("[APKM] Try: %s --version\n", name);
    } else {
        printf("[APKM] ❌ Installation failed\n");
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", staging);
    system(cmd);
    
    return 0;
}

static int cmd_install_local(const char *filepath) {
    printf("[APKM] Installing local package: %s\n", filepath);
    
    char *basename = strrchr(filepath, '/');
    if (basename) basename++; else basename = (char*)filepath;
    
    char name[256] = "package";
    char *ext = strstr(basename, ".tar.bool");
    if (ext) {
        int len = ext - basename;
        strncpy(name, basename, len);
        name[len] = '\0';
    }
    
    const char *staging = "/tmp/apkm_install";
    mkdir(staging, 0755);
    
    if (extract_package(filepath, staging) != 0) {
        fprintf(stderr, "[APKM] Extraction failed\n");
        return -1;
    }
    
    if (run_install_script(staging, name) == 0) {
        db_register_installed(name, "local", "r0", "x86_64", "/usr/local/bin");
        printf("[APKM] ✅ Local installation successful\n");
    }
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", staging);
    system(cmd);
    
    return 0;
}

static int cmd_list(void) {
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

static int cmd_search(const char *query, output_format_t format) {
    package_t results[256];
    int count = db_search_packages(query, results, 256);
    
    if (format == OUTPUT_JSON) {
        printf("[\n");
        for (int i = 0; i < count; i++) {
            printf("  {\"name\":\"%s\",\"version\":\"%s\",\"arch\":\"%s\"}%s\n",
                   results[i].name, results[i].version, results[i].architecture,
                   i < count-1 ? "," : "");
        }
        printf("]\n");
    } else {
        printf("\n[APKM] Search results for '%s':\n", query);
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

static int cmd_repos(output_format_t format) {
    return zarch_list_repos(format);
}

static int cmd_update(output_format_t format) {
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
// HELP
// ============================================================================

static void print_help(void) {
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
    printf("  repos                  List repositories\n");
    printf("  update                 Update package database\n");
    printf("\nOPTIONS:\n");
    printf("  -j, --json             JSON output\n");
    printf("\nEXAMPLES:\n");
    printf("  apkm install apkm\n");
    printf("  apkm install apkm@2.0.0\n");
    printf("  apkm install local-package.tar.bool\n");
    printf("  apkm search database\n");
    printf("  apkm update\n");
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

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) {
            format = OUTPUT_JSON;
        }
    }

    if (strcmp(command, "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[APKM] Missing package or file\n");
            return 1;
        }
        
        if (strstr(argv[2], ".tar.bool") != NULL || access(argv[2], F_OK) == 0) {
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
}