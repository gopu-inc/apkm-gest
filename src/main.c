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
#define MAX_ASSETS 50
#define MAX_PATTERNS 50

// Structure pour la réponse curl
struct curl_response {
    char *data;
    size_t size;
};

// Structure pour les assets GitHub
typedef struct {
    char name[256];
    char url[1024];
    char arch[32];
    char release[16];
    char version[64];
    char pkg_name[128];
    char suffix[16];  // r, m, s, a, c, etc.
    int size;
    time_t created_at;
} github_asset_t;

// Structure pour la barre de progression
typedef struct {
    FILE *fp;
    double last_progress;
    char filename[256];
    time_t start_time;
    double download_speed;
    curl_off_t last_dlnow;
    time_t last_time;
} download_context_t;

// Write callback for curl
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}

// Write callback for response
size_t response_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct curl_response *resp = (struct curl_response *)userdata;
    size_t total = size * nmemb;
    
    resp->data = realloc(resp->data, resp->size + total + 1);
    if (!resp->data) return 0;
    
    memcpy(resp->data + resp->size, ptr, total);
    resp->size += total;
    resp->data[resp->size] = '\0';
    
    return total;
}

// Native progress bar display
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

// Parser les métadonnées du nom de fichier
int parse_package_metadata(const char *filename, char *pkg_name, char *version, 
                          char *release, char *arch, char *suffix) {
    // Format: package-v1.0.0-r1.x86_64.tar.bool
    // ou: package-v1.0.0-m1.armv7.tar.bool
    // ou: package-v1.0.0-s2.aarch64.tar.bool
    // ou: package-v1.0.0-c3.i686.tar.bool
    
    char temp[512];
    strncpy(temp, filename, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    char *base = strrchr(temp, '/');
    if (base) base++; else base = temp;
    
    // Enlever .tar.bool
    char *ext = strstr(base, ".tar.bool");
    if (!ext) return -1;
    *ext = '\0';
    
    // Chercher le séparateur de version
    char *version_start = strstr(base, "-v");
    if (!version_start) return -1;
    
    // Nom du package
    int name_len = version_start - base;
    strncpy(pkg_name, base, name_len);
    pkg_name[name_len] = '\0';
    
    // Chercher le suffixe de release (r, m, s, a, c, etc.)
    char *release_start = strstr(version_start + 2, "-");
    if (release_start) {
        // Version sans release
        int ver_len = release_start - (version_start + 2);
        strncpy(version, version_start + 2, ver_len);
        version[ver_len] = '\0';
        
        // Extraire le suffixe (r, m, s, a, c)
        if (release_start[1] >= 'a' && release_start[1] <= 'z') {
            suffix[0] = release_start[1];
            suffix[1] = '\0';
            
            // Extraire le numéro de release
            char *arch_start = strchr(release_start + 2, '.');
            if (arch_start) {
                int rel_len = arch_start - (release_start + 2);
                strncpy(release, release_start + 2, rel_len);
                release[rel_len] = '\0';
                
                // Extraire l'architecture
                strncpy(arch, arch_start + 1, 31);
                arch[31] = '\0';
            }
        }
    } else {
        // Pas de release, juste version.arch
        char *arch_start = strchr(version_start + 2, '.');
        if (arch_start) {
            int ver_len = arch_start - (version_start + 2);
            strncpy(version, version_start + 2, ver_len);
            version[ver_len] = '\0';
            
            strcpy(release, "0");
            strcpy(suffix, "r");
            strncpy(arch, arch_start + 1, 31);
            arch[31] = '\0';
        }
    }
    
    return 0;
}

// Formater le nom de fichier avec métadonnées
void format_package_filename(const char *pkg_name, const char *version,
                            const char *suffix, const char *release,
                            const char *arch, char *output, size_t output_size) {
    if (suffix && strlen(suffix) > 0 && release && strlen(release) > 0) {
        snprintf(output, output_size, "%s-v%s-%s%s.%s.tar.bool",
                 pkg_name, version, suffix, release, arch);
    } else {
        snprintf(output, output_size, "%s-v%s.%s.tar.bool",
                 pkg_name, version, arch);
    }
}

// Obtenir l'architecture du système
const char* get_system_arch(void) {
    FILE *fp = popen("uname -m", "r");
    static char arch[32];
    if (fp) {
        fgets(arch, sizeof(arch), fp);
        pclose(fp);
        arch[strcspn(arch, "\n")] = 0;
        
        // Normaliser les noms d'architecture
        if (strcmp(arch, "x86_64") == 0 || strcmp(arch, "amd64") == 0)
            return "x86_64";
        if (strcmp(arch, "i386") == 0 || strcmp(arch, "i686") == 0)
            return "i686";
        if (strcmp(arch, "aarch64") == 0)
            return "aarch64";
        if (strcmp(arch, "armv7l") == 0)
            return "armv7";
        return arch;
    }
    return "x86_64";
}

// Parser les assets GitHub depuis la réponse JSON
int parse_github_assets(const char *json, github_asset_t *assets, int max_assets) {
    int count = 0;
    const char *ptr = json;
    
    while ((ptr = strstr(ptr, "\"name\":")) && count < max_assets) {
        ptr += 7;
        while (*ptr == ' ' || *ptr == '"') ptr++;
        
        const char *end = strchr(ptr, '"');
        if (!end) break;
        
        int len = end - ptr;
        strncpy(assets[count].name, ptr, len);
        assets[count].name[len] = '\0';
        
        // Chercher l'URL
        const char *url_ptr = strstr(end, "\"browser_download_url\":");
        if (url_ptr) {
            url_ptr += 23;
            while (*url_ptr == ' ' || *url_ptr == '"') url_ptr++;
            
            const char *url_end = strchr(url_ptr, '"');
            if (url_end) {
                len = url_end - url_ptr;
                strncpy(assets[count].url, url_ptr, len);
                assets[count].url[len] = '\0';
            }
        }
        
        // Parser les métadonnées du nom
        char temp_name[256];
        strcpy(temp_name, assets[count].name);
        
        char *ext = strstr(temp_name, ".tar.bool");
        if (ext) *ext = '\0';
        
        // Extraire les infos
        char *v_start = strstr(temp_name, "-v");
        if (v_start) {
            int name_len = v_start - temp_name;
            strncpy(assets[count].pkg_name, temp_name, name_len);
            assets[count].pkg_name[name_len] = '\0';
            
            char *rel_start = strstr(v_start + 2, "-");
            if (rel_start) {
                int ver_len = rel_start - (v_start + 2);
                strncpy(assets[count].version, v_start + 2, ver_len);
                assets[count].version[ver_len] = '\0';
                
                // Extraire suffixe (r, m, s, a, c)
                if (rel_start[1] >= 'a' && rel_start[1] <= 'z') {
                    assets[count].suffix[0] = rel_start[1];
                    assets[count].suffix[1] = '\0';
                    
                    char *arch_start = strchr(rel_start + 2, '.');
                    if (arch_start) {
                        int rel_len = arch_start - (rel_start + 2);
                        strncpy(assets[count].release, rel_start + 2, rel_len);
                        assets[count].release[rel_len] = '\0';
                        
                        strncpy(assets[count].arch, arch_start + 1, 31);
                        assets[count].arch[31] = '\0';
                    }
                }
            } else {
                char *arch_start = strchr(v_start + 2, '.');
                if (arch_start) {
                    int ver_len = arch_start - (v_start + 2);
                    strncpy(assets[count].version, v_start + 2, ver_len);
                    assets[count].version[ver_len] = '\0';
                    strcpy(assets[count].suffix, "r");
                    strcpy(assets[count].release, "0");
                    strncpy(assets[count].arch, arch_start + 1, 31);
                    assets[count].arch[31] = '\0';
                }
            }
        }
        
        count++;
        ptr = end + 1;
    }
    
    return count;
}

// Télécharger depuis GitHub avec support multi-arch
int download_from_github(const char *pkg_name, const char *version, 
                         const char *output_path) {
    printf("[APKM] Downloading %s %s from GitHub...\n", pkg_name, version);
    
    // Nettoyer la version
    char ver[64];
    strncpy(ver, version, sizeof(ver) - 1);
    ver[sizeof(ver) - 1] = '\0';
    if (ver[0] == 'v') {
        memmove(ver, ver + 1, strlen(ver));
    }
    
    // Obtenir l'architecture système
    const char *system_arch = get_system_arch();
    printf("[APKM] System architecture: %s\n", system_arch);
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[APKM] Failed to initialize curl\n");
        return -1;
    }
    
    // Étape 1: Récupérer les infos de la release
    printf("[APKM] 🔍 Fetching release information...\n");
    
    char api_url[256];
    snprintf(api_url, sizeof(api_url), 
             "https://api.github.com/repos/gopu-inc/apkm-gest/releases/tags/v%s", ver);
    
    struct curl_response resp = {0};
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "User-Agent: APKM-Installer/2.0");
    
    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK && resp.data) {
        // Parser les assets
        github_asset_t assets[MAX_ASSETS];
        int asset_count = parse_github_assets(resp.data, assets, MAX_ASSETS);
        
        printf("[APKM] 📦 Found %d assets:\n", asset_count);
        
        // Chercher le meilleur asset pour notre architecture
        int best_match = -1;
        int best_score = -1;
        
        for (int i = 0; i < asset_count; i++) {
            printf("[APKM]   • %s (%s)\n", assets[i].name, assets[i].arch);
            
            // Vérifier si c'est notre package
            if (strcmp(assets[i].pkg_name, pkg_name) != 0)
                continue;
            
            // Vérifier la version
            if (strcmp(assets[i].version, ver) != 0)
                continue;
            
            // Calculer le score de compatibilité
            int score = 0;
            
            // Architecture exacte = meilleur score
            if (strcmp(assets[i].arch, system_arch) == 0) {
                score = 100;
            }
            // Architecture compatible
            else if (strstr(system_arch, "64") && strstr(assets[i].arch, "64")) {
                score = 80;
            }
            else if (strstr(system_arch, "86") && strstr(assets[i].arch, "86")) {
                score = 70;
            }
            else if (strstr(assets[i].arch, "all") || strstr(assets[i].arch, "any")) {
                score = 50;
            }
            
            // Préférer les releases récentes (r plus élevé)
            if (assets[i].release[0]) {
                int rel_num = atoi(assets[i].release);
                score += rel_num;
            }
            
            if (score > best_score) {
                best_score = score;
                best_match = i;
            }
        }
        
        if (best_match >= 0) {
            printf("[APKM] ✅ Selected: %s (score: %d)\n", 
                   assets[best_match].name, best_score);
            
            // Télécharger l'asset
            FILE *out = fopen(output_path, "wb");
            if (out) {
                download_context_t ctx = {
                    .fp = out,
                    .last_progress = 0,
                    .last_time = time(NULL),
                    .last_dlnow = 0
                };
                strncpy(ctx.filename, assets[best_match].name, sizeof(ctx.filename) - 1);
                
                curl_easy_setopt(curl, CURLOPT_URL, assets[best_match].url);
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
                curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                
                CURLcode dl_res = curl_easy_perform(curl);
                fclose(out);
                
                if (dl_res == CURLE_OK) {
                    long http_code = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                    
                    if (http_code == 200) {
                        printf("[APKM] ✅ Download successful\n");
                        curl_slist_free_all(headers);
                        free(resp.data);
                        curl_easy_cleanup(curl);
                        return 0;
                    }
                }
                unlink(output_path);
            }
        }
    }
    
    free(resp.data);
    
    // Étape 2: Patterns d'URL avec toutes les combinaisons
    printf("[APKM] ⚠️ Trying all possible URL patterns...\n");
    
    char url_patterns[MAX_PATTERNS][1024];
    int pattern_count = 0;
    
    // Suffixes possibles
    const char *suffixes[] = {"r", "m", "s", "a", "c", "b", "d", "rc", "beta", "alpha", ""};
    const char *releases[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
    const char *archs[] = {
        "x86_64", "amd64", "i686", "i386", "aarch64", "arm64",
        "armv7", "armhf", "arm", "mips", "mips64", "ppc64le",
        "s390x", "riscv64", "all", "any", "noarch"
    };
    
    // Générer toutes les combinaisons
    for (int s = 0; s < sizeof(suffixes)/sizeof(suffixes[0]) && pattern_count < MAX_PATTERNS; s++) {
        for (int r = 0; r < sizeof(releases)/sizeof(releases[0]) && pattern_count < MAX_PATTERNS; r++) {
            for (int a = 0; a < sizeof(archs)/sizeof(archs[0]) && pattern_count < MAX_PATTERNS; a++) {
                if (strlen(suffixes[s]) > 0) {
                    snprintf(url_patterns[pattern_count], sizeof(url_patterns[pattern_count]),
                             "https://github.com/gopu-inc/apkm-gest/releases/download/v%s/%s-v%s-%s%s.%s.tar.bool",
                             ver, pkg_name, ver, suffixes[s], releases[r], archs[a]);
                    pattern_count++;
                }
            }
        }
    }
    
    // Patterns sans release
    for (int a = 0; a < sizeof(archs)/sizeof(archs[0]) && pattern_count < MAX_PATTERNS; a++) {
        snprintf(url_patterns[pattern_count], sizeof(url_patterns[pattern_count]),
                 "https://github.com/gopu-inc/apkm-gest/releases/download/v%s/%s-v%s.%s.tar.bool",
                 ver, pkg_name, ver, archs[a]);
        pattern_count++;
    }
    
    // Patterns depuis le dossier build
    for (int s = 0; s < sizeof(suffixes)/sizeof(suffixes[0]) && pattern_count < MAX_PATTERNS; s++) {
        for (int r = 0; r < sizeof(releases)/sizeof(releases[0]) && pattern_count < MAX_PATTERNS; r++) {
            for (int a = 0; a < sizeof(archs)/sizeof(archs[0]) && pattern_count < MAX_PATTERNS; a++) {
                if (strlen(suffixes[s]) > 0) {
                    snprintf(url_patterns[pattern_count], sizeof(url_patterns[pattern_count]),
                             "https://raw.githubusercontent.com/gopu-inc/apkm-gest/main/build/%s-v%s-%s%s.%s.tar.bool",
                             pkg_name, ver, suffixes[s], releases[r], archs[a]);
                    pattern_count++;
                }
            }
        }
    }
    
    // Essayer chaque pattern
    for (int i = 0; i < pattern_count; i++) {
        printf("[APKM] Trying pattern %d/%d\r", i + 1, pattern_count);
        fflush(stdout);
        
        FILE *out = fopen(output_path, "wb");
        if (!out) continue;
        
        curl_easy_setopt(curl, CURLOPT_URL, url_patterns[i]);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        CURLcode dl_res = curl_easy_perform(curl);
        fclose(out);
        
        if (dl_res == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            
            if (http_code == 200) {
                printf("\n[APKM] ✅ Download successful from pattern %d\n", i + 1);
                curl_easy_cleanup(curl);
                return 0;
            }
        }
        
        unlink(output_path);
    }
    
    printf("\n");
    curl_easy_cleanup(curl);
    fprintf(stderr, "[APKM] ❌ Failed to download package\n");
    return -1;
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
    char pkg_release[16] = "r0";
    char pkg_suffix[8] = "r";
    char local_file[512] = "";
    
    if (is_github) {
        char temp_source[512];
        strncpy(temp_source, source, sizeof(temp_source) - 1);
        temp_source[sizeof(temp_source) - 1] = '\0';
        
        char *at_pos = strchr(temp_source, '@');
        if (at_pos) {
            *at_pos = '\0';
            strncpy(pkg_name, temp_source, sizeof(pkg_name) - 1);
            
            char *ver_part = at_pos + 1;
            
            // Parser la version complète avec métadonnées
            char *slash_pos = strchr(ver_part, '/');
            if (slash_pos) {
                int ver_len = slash_pos - ver_part;
                strncpy(pkg_version, ver_part, ver_len);
                pkg_version[ver_len] = '\0';
                
                // Parser les métadonnées après /
                char metadata[64];
                strncpy(metadata, slash_pos + 1, sizeof(metadata) - 1);
                
                char *arch_pos = strchr(metadata, '/');
                if (arch_pos) {
                    strncpy(pkg_suffix, metadata, arch_pos - metadata);
                    pkg_suffix[arch_pos - metadata] = '\0';
                    strncpy(pkg_arch, arch_pos + 1, sizeof(pkg_arch) - 1);
                } else {
                    strncpy(pkg_arch, metadata, sizeof(pkg_arch) - 1);
                }
            } else {
                strncpy(pkg_version, ver_part, sizeof(pkg_version) - 1);
                strcpy(pkg_arch, get_system_arch());
            }
        } else {
            strncpy(pkg_name, temp_source, sizeof(pkg_name) - 1);
            strcpy(pkg_version, "latest");
            strcpy(pkg_arch, get_system_arch());
        }
        
        printf("[APKM] Package: %s %s (%s)\n", pkg_name, pkg_version, pkg_arch);
        
        snprintf(local_file, sizeof(local_file), "/tmp/%s-%s.tar.bool", pkg_name, pkg_version);
        
        if (download_from_github(pkg_name, pkg_version, local_file) != 0) {
            return -1;
        }
    } else {
        strncpy(local_file, source, sizeof(local_file) - 1);
        
        // Parser les métadonnées du fichier local
        char suffix[8] = "";
        if (parse_package_metadata(local_file, pkg_name, pkg_version, 
                                   pkg_release, pkg_arch, suffix) == 0) {
            printf("[APKM] Package: %s %s-%s (%s)\n", 
                   pkg_name, pkg_version, pkg_release, pkg_arch);
        } else {
            printf("[APKM] Package: %s\n", local_file);
        }
    }
    
    // Staging directory
    const char *staging_path = "/tmp/apkm_install";
    
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
    
    if (install_success) {
        char binary_dest[512];
        snprintf(binary_dest, sizeof(binary_dest), "/usr/local/bin/%s", pkg_name);
        register_installed_package(pkg_name, pkg_version, pkg_arch, binary_dest);
    }
    
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
    printf("  install <file>              Install local .tar.bool package\n");
    printf("  install <pkg>@<ver>         Install package from GitHub (auto-detect arch)\n");
    printf("  install <pkg>@<ver>/<arch>  Install with specific architecture\n");
    printf("  install <pkg>@<ver>/<suffix><release>/<arch>  Full metadata\n");
    printf("  list                        List installed packages\n");
    printf("  sync                        Sync Alpine database\n");
    printf("  audit                       Security audit\n");
    printf("  rollback                    Rollback to previous ref\n\n");
    printf("EXAMPLES:\n");
    printf("  apkm install package-v1.0.0-r1.x86_64.tar.bool\n");
    printf("  apkm install super-app@1.0.0                    (auto-detects arch)\n");
    printf("  apkm install super-app@1.0.0/armv7              (specific arch)\n");
    printf("  apkm install super-app@1.0.0/r2/armv7           (with release)\n");
    printf("  apkm install super-app@latest\n");
    printf("  apkm list\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }

    char *command = argv[1];
    output_format_t fmt = OUTPUT_TEXT;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) {
            fmt = OUTPUT_JSON;
        }
        if (strcmp(argv[i], "--toml") == 0 || strcmp(argv[i], "-t") == 0) {
            fmt = OUTPUT_TOML;
        }
    }

    if (strcmp(command, "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[APKM] Specify a package or file\n");
            return 1;
        }
        
        char *source = argv[2];
        int is_github = 0;
        
        if (strchr(source, '@') != NULL) {
            is_github = 1;
        } else if (strstr(source, ".tar.bool") == NULL && strstr(source, "/") == NULL) {
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
