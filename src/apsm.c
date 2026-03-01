#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <libgen.h>
#include <json-c/json.h>
#include "apkm.h"
#include "security.h"

#define MAX_DOC_SIZE 16384
#define UPLOAD_CHUNK_SIZE 8192

// Structure pour la réponse curl
struct curl_response {
    char *data;
    size_t size;
};

// Structure pour la progression d'upload
typedef struct {
    double last_progress;
    char filename[256];
    time_t start_time;
    double upload_speed;
    curl_off_t last_ulnow;
    time_t last_time;
    long total_size;
} upload_context_t;

// ============================================================================
// Callbacks curl
// ============================================================================

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct curl_response *resp = (struct curl_response *)userdata;
    size_t total = size * nmemb;
    
    resp->data = realloc(resp->data, resp->size + total + 1);
    if (!resp->data) return 0;
    
    memcpy(resp->data + resp->size, ptr, total);
    resp->size += total;
    resp->data[resp->size] = '\0';
    
    return total;
}

// Barre de progression pour upload
void show_upload_progress(double percentage, const char *filename, double speed) {
    int bar_width = 50;
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

int upload_progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, 
                              curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow;
    
    upload_context_t *ctx = (upload_context_t *)clientp;
    
    if (ctx->total_size > 0) {
        double percentage = (double)ulnow / (double)ctx->total_size * 100.0;
        
        time_t now = time(NULL);
        if (now > ctx->last_time) {
            curl_off_t diff = ulnow - ctx->last_ulnow;
            ctx->upload_speed = (double)diff / (now - ctx->last_time);
            ctx->last_ulnow = ulnow;
            ctx->last_time = now;
        }
        
        if (percentage - ctx->last_progress >= 1.0 || percentage >= 100.0) {
            show_upload_progress(percentage, ctx->filename, ctx->upload_speed);
            ctx->last_progress = percentage;
        }
    }
    return 0;
}

// ============================================================================
// Utilitaires
// ============================================================================

void clean_string(char *str) {
    int len = strlen(str);
    if (len >= 2 && str[0] == '"' && str[len-1] == '"') {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
    
    char *start = str;
    while (*start == ' ' || *start == '\t') start++;
    if (start != str) memmove(str, start, strlen(start) + 1);
    
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
}

// Extraire les infos du package depuis le nom de fichier
void parse_package_filename(const char *filename, char *name, char *version, 
                            char *release, char *arch) {
    char temp[512];
    strncpy(temp, filename, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    char *base = strrchr(temp, '/');
    if (base) base++; else base = temp;
    
    char *ext = strstr(base, ".tar.bool");
    if (!ext) return;
    *ext = '\0';
    
    char *version_start = strstr(base, "-v");
    if (!version_start) return;
    
    int name_len = version_start - base;
    strncpy(name, base, name_len);
    name[name_len] = '\0';
    
    char *release_start = strstr(version_start + 2, "-r");
    if (release_start) {
        int ver_len = release_start - (version_start + 2);
        strncpy(version, version_start + 2, ver_len);
        version[ver_len] = '\0';
        
        char *arch_start = strchr(release_start + 2, '.');
        if (arch_start) {
            int rel_len = arch_start - (release_start + 2);
            strncpy(release, release_start + 2, rel_len);
            release[rel_len] = '\0';
            
            strncpy(arch, arch_start + 1, 31);
            arch[31] = '\0';
        }
    } else {
        char *arch_start = strchr(version_start + 2, '.');
        if (arch_start) {
            int ver_len = arch_start - (version_start + 2);
            strncpy(version, version_start + 2, ver_len);
            version[ver_len] = '\0';
            strcpy(release, "r0");
            strncpy(arch, arch_start + 1, 31);
            arch[31] = '\0';
        }
    }
}

// Charger la documentation
char* load_documentation(char *buffer, size_t buffer_size) {
    buffer[0] = '\0';
    
    // Chercher README.md
    const char *readme_files[] = {
        "README.md", "README", "readme.md",
        "Readme.md", "docs/README.md", NULL
    };
    
    for (int i = 0; readme_files[i] != NULL; i++) {
        if (access(readme_files[i], F_OK) == 0) {
            FILE *rf = fopen(readme_files[i], "r");
            if (rf) {
                size_t total = 0;
                char line[256];
                
                snprintf(buffer, buffer_size, "# Documentation\n\n");
                total = strlen(buffer);
                
                while (fgets(line, sizeof(line), rf) && total < buffer_size - 1) {
                    size_t len = strlen(line);
                    if (total + len < buffer_size - 1) {
                        strcpy(buffer + total, line);
                        total += len;
                    }
                }
                fclose(rf);
                break;
            }
        }
    }
    
    if (strlen(buffer) == 0) {
        snprintf(buffer, buffer_size, "No documentation provided.");
    }
    
    return buffer;
}

// ============================================================================
// ZARCH HUB PUBLISHING
// ============================================================================

// Login à Zarch Hub
int zarch_login(const char *username, const char *password, char *token, size_t token_size) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct curl_response resp = {0};
    struct curl_slist *headers = NULL;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/auth/login", ZARCH_API_URL);
    
    char post_data[1024];
    snprintf(post_data, sizeof(post_data),
             "{\"username\":\"%s\",\"password\":\"%s\"}",
             username, password);
    
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    int success = -1;
    if (res == CURLE_OK && resp.data) {
        struct json_object *parsed = json_tokener_parse(resp.data);
        if (parsed) {
            struct json_object *token_obj;
            if (json_object_object_get_ex(parsed, "token", &token_obj)) {
                const char *token_str = json_object_get_string(token_obj);
                strncpy(token, token_str, token_size - 1);
                token[token_size - 1] = '\0';
                success = 0;
            }
            json_object_put(parsed);
        }
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    
    return success;
}

// Upload du package vers Zarch Hub
int zarch_upload_package(const char *token, const char *filepath, 
                         const char *name, const char *version,
                         const char *release, const char *arch) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct stat file_stat;
    if (stat(filepath, &file_stat) != 0) {
        fprintf(stderr, "[APSM] ❌ File not found: %s\n", filepath);
        return -1;
    }
    
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        perror("[APSM] ❌ Cannot open file");
        return -1;
    }
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/upload/public/%s", ZARCH_API_URL, name);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Authorization: Bearer");
    headers = curl_slist_append(headers, "Content-Type: multipart/form-data");
    
    struct curl_httppost *formpost = NULL;
    struct curl_httppost *lastptr = NULL;
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "file",
                 CURLFORM_FILE, filepath,
                 CURLFORM_CONTENTTYPE, "application/gzip",
                 CURLFORM_END);
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "version",
                 CURLFORM_COPYCONTENTS, version,
                 CURLFORM_END);
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "release",
                 CURLFORM_COPYCONTENTS, release,
                 CURLFORM_END);
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "arch",
                 CURLFORM_COPYCONTENTS, arch,
                 CURLFORM_END);
    
    upload_context_t ctx = {
        .last_progress = 0,
        .last_time = time(NULL),
        .last_ulnow = 0,
        .total_size = file_stat.st_size
    };
    strncpy(ctx.filename, name, sizeof(ctx.filename) - 1);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, upload_progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    
    printf("[APSM] 📤 Uploading %s %s-%s (%s) - %.2f KB\n", 
           name, version, release, arch, file_stat.st_size / 1024.0);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(file);
    
    curl_formfree(formpost);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "\n[APSM] ❌ Upload failed: %s\n", curl_easy_strerror(res));
        return -1;
    }
    
    printf("\n[APSM] ✅ Upload complete\n");
    return 0;
}

// ============================================================================
// PUBLISH COMMAND
// ============================================================================

int publish_package(const char *filepath) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APSM - Zarch Hub Publisher v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    // Vérifier que le fichier existe
    if (access(filepath, F_OK) != 0) {
        printf("[APSM] ❌ File not found: %s\n", filepath);
        return -1;
    }
    
    // Extraire les infos du package
    char name[256] = "", version[64] = "", release[16] = "r0", arch[32] = "x86_64";
    parse_package_filename(filepath, name, version, release, arch);
    
    if (strlen(name) == 0) {
        printf("[APSM] ❌ Could not parse package name from filename\n");
        return -1;
    }
    
    printf("📦 Package: %s\n", name);
    printf("📌 Version: %s\n", version);
    printf("🔖 Release: %s\n", release);
    printf("🔧 Arch:    %s\n", arch);
    
    // Calculer SHA256
    char sha256[128];
    if (calculate_sha256(filepath, sha256) == 0) {
        printf("🔏 SHA256:  %.32s...\n", sha256);
    }
    
    // Charger la documentation
    char doc_content[MAX_DOC_SIZE] = "";
    load_documentation(doc_content, sizeof(doc_content));
    printf("📚 Documentation: %zu bytes\n", strlen(doc_content));
    
    // Demander les identifiants Zarch
    printf("\n🔐 Zarch Hub Login\n");
    printf("Username: ");
    char username[256];
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;
    
    printf("Password: ");
    char password[256];
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = 0;
    
    char token[512];
    if (zarch_login(username, password, token, sizeof(token)) != 0) {
        printf("[APSM] ❌ Authentication failed\n");
        return -1;
    }
    printf("[APSM] ✅ Authenticated as %s\n", username);
    
    // Upload du package
    if (zarch_upload_package(token, filepath, name, version, release, arch) != 0) {
        return -1;
    }
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("✅ Publication completed successfully!\n");
    printf("📦 Package: %s %s-%s (%s)\n", name, version, release, arch);
    printf("🔗 Zarch Hub: %s/package/public/%s/%s\n", 
           ZARCH_HUB_URL, name, version);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return 0;
}

// ============================================================================
// AUTH COMMANDS
// ============================================================================

int auth_command(const char *username, const char *password) {
    char token[512];
    
    if (zarch_login(username, password, token, sizeof(token)) == 0) {
        // Sauvegarder le token avec BTSCRYPT
        security_token_t sec_token;
        strncpy(sec_token.token, token, sizeof(sec_token.token) - 1);
        sec_token.last_update = time(NULL);
        sec_token.validated = 1;
        
        if (security_save_token(&sec_token) == 0) {
            printf("[APSM] ✅ Zarch token saved securely\n");
            return 0;
        }
    }
    
    printf("[APSM] ❌ Authentication failed\n");
    return -1;
}

int status_command(void) {
    security_token_t token;
    
    if (security_load_token(&token) == 0) {
        printf("[APSM] ✅ Authenticated to Zarch Hub\n");
        printf("[APSM] 📁 Token: %s\n", TOKEN_PATH);
        return 0;
    }
    
    printf("[APSM] ❌ Not authenticated\n");
    return -1;
}

int sync_command(void) {
    printf("[APSM] Sync not needed for Zarch Hub\n");
    return 0;
}

// ============================================================================
// LIST COMMAND (Zarch packages)
// ============================================================================

int list_command(output_format_t format) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct curl_response resp = {0};
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/search?q=", ZARCH_API_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK && resp.data) {
        if (format == OUTPUT_JSON) {
            printf("%s\n", resp.data);
        } else {
            struct json_object *parsed = json_tokener_parse(resp.data);
            if (parsed) {
                struct json_object *results;
                if (json_object_object_get_ex(parsed, "results", &results)) {
                    int len = json_object_array_length(results);
                    
                    printf("\n📦 ZARCH HUB PACKAGES\n");
                    printf("═══════════════════════════════════════════\n");
                    printf("%-20s %-12s %-15s %-10s\n", "NAME", "VERSION", "AUTHOR", "DOWNLOADS");
                    printf("───────────────────────────────────────────\n");
                    
                    for (int i = 0; i < len; i++) {
                        struct json_object *pkg = json_object_array_get_idx(results, i);
                        struct json_object *name, *ver, *author, *downloads;
                        
                        const char *n = "?", *v = "?", *a = "?";
                        int d = 0;
                        
                        if (json_object_object_get_ex(pkg, "name", &name))
                            n = json_object_get_string(name);
                        if (json_object_object_get_ex(pkg, "version", &ver))
                            v = json_object_get_string(ver);
                        if (json_object_object_get_ex(pkg, "author", &author))
                            a = json_object_get_string(author);
                        if (json_object_object_get_ex(pkg, "downloads", &downloads))
                            d = json_object_get_int(downloads);
                        
                        printf(" • %-20s %-12s %-15s %-10d\n", n, v, a, d);
                    }
                    
                    printf("═══════════════════════════════════════════\n");
                    printf(" Total: %d packages\n", len);
                }
                json_object_put(parsed);
            }
        }
        free(resp.data);
        return 0;
    }
    
    printf("[APSM] ❌ Failed to fetch package list\n");
    return -1;
}

// ============================================================================
// HELP
// ============================================================================

void print_help(void) {
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APSM - Zarch Hub Publisher v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("USAGE:\n");
    printf("  apsm <COMMAND> [OPTIONS]\n\n");
    printf("COMMANDS:\n");
    printf("  push <file>              Publish package to Zarch Hub\n");
    printf("  login <user> [pass]      Authenticate to Zarch Hub\n");
    printf("  status                   Check authentication status\n");
    printf("  list                     List packages on Zarch Hub\n");
    printf("\nOPTIONS:\n");
    printf("  -j, --json               JSON output\n");
    printf("\nEXAMPLES:\n");
    printf("  apsm login mauricio\n");
    printf("  apsm push build/apkm-v2.0.0-r1.x86_64.tar.bool\n");
    printf("  apsm list\n");
    printf("  apsm list --json\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }
    
    security_init();
    
    output_format_t format = OUTPUT_TEXT;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) {
            format = OUTPUT_JSON;
        }
    }
    
    if (strcmp(argv[1], "push") == 0) {
        if (argc < 3) {
            printf("[APSM] ❌ Missing file\n");
            return 1;
        }
        return publish_package(argv[2]);
    }
    else if (strcmp(argv[1], "login") == 0) {
        if (argc < 3) {
            printf("[APSM] ❌ Missing username\n");
            return 1;
        }
        
        char *username = argv[2];
        char *password = NULL;
        
        if (argc >= 4) {
            password = argv[3];
        } else {
            printf("Password: ");
            char pass[256];
            fgets(pass, sizeof(pass), stdin);
            pass[strcspn(pass, "\n")] = 0;
            password = pass;
        }
        
        return auth_command(username, password);
    }
    else if (strcmp(argv[1], "status") == 0) {
        return status_command();
    }
    else if (strcmp(argv[1], "list") == 0) {
        return list_command(format);
    }
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }
    else {
        printf("[APSM] ❌ Unknown command: %s\n", argv[1]);
        printf("Try 'apsm help'\n");
        return 1;
    }
    
    return 0;
}
