#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include "apkm.h"
#include "sandbox.h"

/**
 * APKM v2.0 - The Gopu.inc Smart Package Manager
 */

#define BUF_SIZE 8192
#define PROGRESS_WIDTH 50

// Progress bar callback structure
typedef struct {
    FILE *fp;
    double last_progress;
    char filename[256];
    time_t start_time;
    double download_speed;
    curl_off_t last_dlnow;
    time_t last_time;
} download_context_t;

// Native progress bar display (ASCII only)
void show_progress(double percentage, const char *filename, double speed) {
    int bar_width = PROGRESS_WIDTH;
    int pos = (int)(percentage * bar_width / 100.0);
    
    printf("\r[");
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) {
            printf("=");
        } else if (i == pos && percentage < 100.0) {
            printf(">");
        } else {
            printf(" ");
        }
    }
    
    if (percentage >= 100.0) {
        printf("] %3.0f%% %s - Complete        \n", percentage, filename);
    } else {
        printf("] %3.0f%% %s - %.1f KB/s      ", 
               percentage, filename, speed / 1024.0);
    }
    fflush(stdout);
}

// Progress callback for curl
int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, 
                       curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;  // Unused
    (void)ulnow;    // Unused
    
    download_context_t *ctx = (download_context_t *)clientp;
    
    if (dltotal > 0) {
        double percentage = (double)dlnow / (double)dltotal * 100.0;
        
        // Calculate speed
        time_t now = time(NULL);
        if (now > ctx->last_time) {
            curl_off_t diff = dlnow - ctx->last_dlnow;
            ctx->download_speed = (double)diff / (now - ctx->last_time);
            ctx->last_dlnow = dlnow;
            ctx->last_time = now;
        }
        
        // Update every 1% or on complete
        if (percentage - ctx->last_progress >= 1.0 || percentage >= 100.0) {
            show_progress(percentage, ctx->filename, ctx->download_speed);
            ctx->last_progress = percentage;
        }
    }
    return 0;
}

// Write callback for curl
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    FILE *fp = (FILE *)stream;
    return fwrite(ptr, size, nmemb, fp);
}

// Parse package info from filename
void parse_package_filename(const char *filepath, char *pkg_name, char *pkg_version, 
                           char *pkg_arch, size_t name_size, size_t ver_size, size_t arch_size) {
    // Default values
    strncpy(pkg_name, "unknown", name_size);
    strncpy(pkg_version, "0.0.0", ver_size);
    strncpy(pkg_arch, "x86_64", arch_size);
    
    char *basename = strrchr(filepath, '/');
    if (basename) basename++; else basename = (char*)filepath;
    
    char filename[512];
    strncpy(filename, basename, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
    
    // Parse: package-v1.0.0-r1.x86_64.tar.bool
    char *version_start = strstr(filename, "-v");
    if (version_start) {
        int name_len = (int)(version_start - filename);
        if (name_len > 0 && (size_t)name_len < name_size) {
            strncpy(pkg_name, filename, (size_t)name_len);
            pkg_name[name_len] = '\0';
        }
        
        char *arch_start = strstr(version_start + 2, ".");
        if (arch_start) {
            int ver_len = (int)(arch_start - (version_start + 2));
            if (ver_len > 0 && (size_t)ver_len < ver_size) {
                strncpy(pkg_version, version_start + 2, (size_t)ver_len);
                pkg_version[ver_len] = '\0';
            }
            
            char *ext_start = strstr(arch_start + 1, ".tar.bool");
            if (ext_start) {
                int arch_len = (int)(ext_start - (arch_start + 1));
                if (arch_len > 0 && (size_t)arch_len < arch_size) {
                    strncpy(pkg_arch, arch_start + 1, (size_t)arch_len);
                    pkg_arch[arch_len] = '\0';
                }
            }
        }
    }
}

// Download package from GitHub
int download_from_github(const char *pkg_name, const char *version, 
                         const char *output_path) {
    printf("[APKM] Downloading %s %s from GitHub...\n", pkg_name, version);
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[APKM] Failed to initialize curl\n");
        return -1;
    }
    
    // D'abord, chercher dans DATA.db pour obtenir l'URL exacte
    char db_url[512];
    snprintf(db_url, sizeof(db_url), 
             "https://raw.githubusercontent.com/%s/%s/main/DATA.db",
             REPO_OWNER, REPO_NAME);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "curl -s %s | grep '^%s|%s' | cut -d'|' -f6",
             db_url, pkg_name, version);
    
    FILE *fp = popen(cmd, "r");
    char url[1024] = "";
    if (fp) {
        fgets(url, sizeof(url), fp);
        pclose(fp);
        url[strcspn(url, "\n")] = 0;
    }
    
    // Si pas trouvé dans DATA.db, utiliser l'URL par défaut des releases
    if (strlen(url) == 0) {
        snprintf(url, sizeof(url),
                 "https://github.com/%s/%s/releases/download/v%s/%s-v%s.%s.tar.bool",
                 REPO_OWNER, REPO_NAME, version, pkg_name, version, "r1.x86_64");
    }
    
    printf("[APKM] Fetching from: %s\n", url);
    
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "[APKM] Cannot create output file\n");
        curl_easy_cleanup(curl);
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM-Installer/2.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(out);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "\n[APKM] Download failed: %s\n", curl_easy_strerror(res));
        unlink(output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (http_code != 200) {
        fprintf(stderr, "\n[APKM] HTTP error: %ld\n", http_code);
        unlink(output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    printf("[APKM] Download complete\n");
    curl_easy_cleanup(curl);
    
    return 0;
}

// Register installed package in database
void register_installed_package(const char *pkg_name, const char *version, 
                                const char *arch, const char *binary_path) {
    mkdir("/var/lib/apkm", 0755);
    
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "/var/lib/apkm/packages.db");
    
    FILE *db = fopen(db_path, "a");
    if (!db) {
        db = fopen(db_path, "w");
    }
    
    if (db) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char date_str[20];
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        fprintf(db, "%s|%s|%s|%lld|%s|%s\n", 
                pkg_name, version, arch, (long long)now, date_str, 
                binary_path ? binary_path : "/usr/local/bin");
        fclose(db);
        
        printf("[APKM] Package %s %s registered in database\n", pkg_name, version);
        
        // Create manifest
        char manifest_path[512];
        snprintf(manifest_path, sizeof(manifest_path), "/var/lib/apkm/%s.manifest", pkg_name);
        
        FILE *mf = fopen(manifest_path, "w");
        if (mf) {
            fprintf(mf, "NAME=%s\n", pkg_name);
            fprintf(mf, "VERSION=%s\n", version);
            fprintf(mf, "ARCH=%s\n", arch);
            fprintf(mf, "INSTALL_DATE=%s\n", date_str);
            fprintf(mf, "BINARY_PATH=%s\n", binary_path ? binary_path : "/usr/local/bin");
            fclose(mf);
        }
    } else {
        printf("[APKM] Cannot register package in database\n");
    }
}

// List installed packages
void apkm_list_packages(void) {
    printf("[APKM] Installed packages:\n");
    printf("========================================\n");
    
    FILE *db = fopen("/var/lib/apkm/packages.db", "r");
    if (!db) {
        printf("  No packages installed (database empty)\n");
        return;
    }
    
    char line[1024];
    int count = 0;
    
    printf(" %-20s %-12s %-10s %-20s\n", "NAME", "VERSION", "ARCH", "DATE");
    printf(" -------------------------------------------------\n");
    
    while (fgets(line, sizeof(line), db)) {
        char name[256] = "";
        char version[64] = "";
        char arch[32] = "";
        char date_str[20] = "";
        long long timestamp = 0;
        char binary[512] = "";
        
        int parsed = sscanf(line, "%255[^|]|%63[^|]|%31[^|]|%lld|%19[^|]|%511[^\n]", 
                            name, version, arch, &timestamp, date_str, binary);
        
        if (parsed >= 5) {
            printf(" * %-20s %-12s %-10s %-20s\n", name, version, arch, date_str);
            count++;
        }
    }
    
    fclose(db);
    
    if (count == 0) {
        printf("  No packages found\n");
    } else {
        printf("\n Total: %d package(s) installed\n", count);
    }
}

// Main installation function
int apkm_install_package(const char *source, int is_github) {
    char pkg_name[256] = "unknown";
    char pkg_version[64] = "0.0.0";
    char pkg_arch[32] = "x86_64";
    char local_file[512] = "";
    
    if (is_github) {
        // Source is GitHub package name
        strncpy(pkg_name, source, sizeof(pkg_name) - 1);
        
        // Try to get version from name
        char *version_sep = strchr(pkg_name, '@');
        if (version_sep) {
            *version_sep = '\0';
            strncpy(pkg_version, version_sep + 1, sizeof(pkg_version) - 1);
        } else {
            strcpy(pkg_version, "latest");
        }
        
        // Download from GitHub
        snprintf(local_file, sizeof(local_file), "/tmp/%s-v%s.tar.bool", 
                 pkg_name, pkg_version);
        
        if (download_from_github(pkg_name, pkg_version, local_file) != 0) {
            return -1;
        }
    } else {
        // Source is local file
        strncpy(local_file, source, sizeof(local_file) - 1);
        parse_package_filename(local_file, pkg_name, pkg_version, pkg_arch,
                               sizeof(pkg_name), sizeof(pkg_version), sizeof(pkg_arch));
    }
    
    printf("[APKM] Package: %s %s (%s)\n", pkg_name, pkg_version, pkg_arch);
    
    // Staging directory
    const char *staging_path = "/tmp/apkm_install";
    
    // Create and clean staging directory
    struct stat st = {0};
    if (stat(staging_path, &st) == -1) {
        mkdir(staging_path, 0755);
    }
    
    char cmd_clean[512];
    snprintf(cmd_clean, sizeof(cmd_clean), "rm -rf %s/*", staging_path);
    system(cmd_clean);
    
    // Extract package
    printf("[APKM] Extracting package...\n");
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -xzf '%s' -C '%s' 2>/dev/null", local_file, staging_path);
    
    if (system(cmd) != 0) {
        // Try without compression
        snprintf(cmd, sizeof(cmd), "tar -xf '%s' -C '%s' 2>/dev/null", local_file, staging_path);
        if (system(cmd) != 0) {
            fprintf(stderr, "[APKM] Extraction failed\n");
            if (is_github) unlink(local_file);
            return -1;
        }
    }
    
    // Resolve dependencies
    resolve_dependencies(staging_path);
    
    // Find and execute installation script
    printf("[APKM] Looking for installation script...\n");
    
    const char *scripts[] = {
        "install.sh",
        "INSTALL.sh",
        "post-install.sh",
        "setup.sh",
        "configure.sh",
        NULL
    };
    
    int script_found = 0;
    int install_success = 0;
    
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
            
            if (ret == 0) {
                printf("[APKM] Script executed successfully\n");
                script_found = 1;
                install_success = 1;
                break;
            } else {
                printf("[APKM] Script %s failed (code: %d)\n", scripts[i], ret);
            }
        }
    }
    
    if (!script_found) {
        printf("[APKM] No installation script found\n");
        
        // Look for binary at root
        char binary_path[512];
        snprintf(binary_path, sizeof(binary_path), "%s/%s", staging_path, pkg_name);
        
        if (access(binary_path, F_OK) == 0) {
            printf("[APKM] Binary found at root, installing directly\n");
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), 
                     "cp '%s' /usr/local/bin/ && chmod 755 /usr/local/bin/%s", 
                     binary_path, pkg_name);
            if (system(cmd) == 0) {
                install_success = 1;
            }
        }
        
        // Look in usr/bin/
        snprintf(binary_path, sizeof(binary_path), "%s/usr/bin/%s", staging_path, pkg_name);
        if (access(binary_path, F_OK) == 0) {
            printf("[APKM] Binary found in usr/bin/, installing\n");
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), 
                     "cp '%s' /usr/local/bin/ && chmod 755 /usr/local/bin/%s", 
                     binary_path, pkg_name);
            if (system(cmd) == 0) {
                install_success = 1;
            }
        }
    }
    
    // Register in database if successful
    if (install_success) {
        char binary_dest[512];
        snprintf(binary_dest, sizeof(binary_dest), "/usr/local/bin/%s", pkg_name);
        register_installed_package(pkg_name, pkg_version, pkg_arch, binary_dest);
    }
    
    // Cleanup
    printf("[APKM] Cleaning up...\n");
    snprintf(cmd_clean, sizeof(cmd_clean), "rm -rf %s", staging_path);
    system(cmd_clean);
    
    if (is_github) {
        unlink(local_file);
    }
    
    if (install_success) {
        printf("[APKM] Installation completed successfully!\n");
        printf("[APKM] Try: %s --version\n", pkg_name);
        return 0;
    } else {
        printf("[APKM] Installation failed\n");
        return -1;
    }
}

// Print help
void print_help(void) {
    printf("============================================================\n");
    printf("  APKM - Advanced Package Manager (Gopu.inc Edition) v2.0\n");
    printf("============================================================\n\n");
    printf("USAGE:\n");
    printf("  apkm <COMMAND> [OPTIONS]\n\n");
    printf("COMMANDS:\n");
    printf("  install <file>          Install local .tar.bool package\n");
    printf("  install <pkg>@<ver>     Install package from GitHub\n");
    printf("  list                     List installed packages\n");
    printf("  sync                     Sync Alpine database\n");
    printf("  audit                    Security audit\n");
    printf("  rollback                 Rollback to previous ref\n\n");
    printf("OPTIONS:\n");
    printf("  -j, --json               JSON output\n");
    printf("  -t, --toml               TOML output\n");
    printf("  --help                    Show this help\n\n");
    printf("EXAMPLES:\n");
    printf("  apkm install package-v1.0.0.tar.bool\n");
    printf("  apkm install super-app@2.1.0\n");
    printf("  apkm list\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }

    char *command = argv[1];
    output_format_t fmt = OUTPUT_TEXT;

    // Detect output formats
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) {
            fmt = OUTPUT_JSON;
        }
        if (strcmp(argv[i], "--toml") == 0 || strcmp(argv[i], "-t") == 0) {
            fmt = OUTPUT_TOML;
        }
    }

    // Command routing
    if (strcmp(command, "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[APKM] Specify a package or file\n");
            return 1;
        }
        
        char *source = argv[2];
        int is_github = 0;
        
        // Check if it's a GitHub package (contains @)
        if (strchr(source, '@') != NULL) {
            is_github = 1;
        } else if (strstr(source, ".tar.bool") == NULL && strstr(source, "/") == NULL) {
            // Assume it's a package name without version
            is_github = 1;
        }
        
        return apkm_install_package(source, is_github);
    }
    else if (strcmp(command, "list") == 0) {
        apkm_list_packages();
    }
    else if (strcmp(command, "sync") == 0) {
        sync_alpine_db(fmt);
    }
    else if (strcmp(command, "audit") == 0) {
        printf("[APKM] CVE analysis and integrity scan...\n");
        printf("[APKM] Audit completed (simulation)\n");
    }
    else if (strcmp(command, "rollback") == 0) {
        printf("[APKM] Rolling back to previous version...\n");
        printf("[APKM] Rollback completed (simulation)\n");
    }
    else {
        fprintf(stderr, "[APKM] Unknown command: %s\n", command);
        fprintf(stderr, "Use 'apkm --help' to see available commands\n");
        return 1;
    }

    return 0;
}
