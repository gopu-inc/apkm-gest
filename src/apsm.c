#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <json-c/json.h>
#include "apkm.h"
#include "security.h"

#define ZARCH_HUB_URL "https://gsql-badge.onrender.com"
#define ZARCH_API_URL ZARCH_HUB_URL "/v5.2"
#define ZARCH_PACKAGE_URL ZARCH_HUB_URL "/package/download"

struct curl_response {
    char *data;
    size_t size;
};

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
    
    printf("[APSM] 🔐 Authenticating to Zarch Hub...\n");
    
    CURLcode res = curl_easy_perform(curl);
    
    int success = -1;
    if (res == CURLE_OK && resp.data) {
        struct json_object *parsed = json_tokener_parse(resp.data);
        if (parsed) {
            struct json_object *success_obj, *token_obj;
            if (json_object_object_get_ex(parsed, "success", &success_obj) &&
                json_object_get_boolean(success_obj) &&
                json_object_object_get_ex(parsed, "token", &token_obj)) {
                const char *token_str = json_object_get_string(token_obj);
                strncpy(token, token_str, token_size - 1);
                token[token_size - 1] = '\0';
                success = 0;
                printf("[APSM] ✅ Authentication successful\n");
            } else {
                struct json_object *error_obj;
                if (json_object_object_get_ex(parsed, "error", &error_obj)) {
                    printf("[APSM] ❌ Authentication failed: %s\n", 
                           json_object_get_string(error_obj));
                } else {
                    printf("[APSM] ❌ Authentication failed\n");
                }
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
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/upload/public/%s", ZARCH_API_URL, name);
    
    struct curl_slist *headers = NULL;
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, auth_header);
    
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
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    
    printf("[APSM] 📤 Uploading %s %s-%s (%s) - %.2f KB\n", 
           name, version, release, arch, file_stat.st_size / 1024.0);
    
    CURLcode res = curl_easy_perform(curl);
    
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

// Parse le nom du fichier
void parse_filename(const char *filename, char *name, char *version, 
                    char *release, char *arch) {
    char temp[512];
    strncpy(temp, filename, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    char *base = strrchr(temp, '/');
    if (base) base++; else base = temp;
    
    char *ext = strstr(base, ".tar.bool");
    if (ext) *ext = '\0';
    
    char *v = strstr(base, "-v");
    if (v) {
        int name_len = v - base;
        strncpy(name, base, name_len);
        name[name_len] = '\0';
        
        char *r = strstr(v + 2, "-r");
        if (r) {
            int ver_len = r - (v + 2);
            strncpy(version, v + 2, ver_len);
            version[ver_len] = '\0';
            
            char *a = strchr(r + 2, '.');
            if (a) {
                int rel_len = a - (r + 2);
                strncpy(release, r + 2, rel_len);
                release[rel_len] = '\0';
                
                strncpy(arch, a + 1, 31);
                arch[31] = '\0';
            }
        }
    }
}

int publish_package(const char *filepath) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APSM - Zarch Hub Publisher v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    if (access(filepath, F_OK) != 0) {
        printf("[APSM] ❌ File not found: %s\n", filepath);
        return -1;
    }
    
    char name[256] = "", version[64] = "1.0.0", release[16] = "r0", arch[32] = "x86_64";
    parse_filename(filepath, name, version, release, arch);
    
    if (strlen(name) == 0) {
        printf("[APSM] ❌ Could not parse package name from filename\n");
        return -1;
    }
    
    printf("📦 Package: %s\n", name);
    printf("📌 Version: %s\n", version);
    printf("🔖 Release: %s\n", release);
    printf("🔧 Arch:    %s\n", arch);
    
    // Vérifier si token existe
    char token[512];
    security_token_t sec_token;
    if (security_load_token(&sec_token) == 0) {
        strncpy(token, sec_token.token, sizeof(token) - 1);
        printf("[APSM] 🔐 Using saved token\n");
        
        // Upload direct avec token
        if (zarch_upload_package(token, filepath, name, version, release, arch) == 0) {
            printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
            printf("✅ Publication completed successfully!\n");
            printf("📦 Package: %s %s-%s (%s)\n", name, version, release, arch);
            printf("🔗 Zarch Hub: %s/package/%s\n", ZARCH_HUB_URL, name);
            printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
            return 0;
        }
        return -1;
    }
    
    // Sinon demander login
    printf("\n🔐 Zarch Hub Login\n");
    printf("Username: ");
    char username[256];
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;
    
    printf("Password: ");
    char password[256];
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = 0;
    
    if (zarch_login(username, password, token, sizeof(token)) != 0) {
        printf("[APSM] ❌ Authentication failed\n");
        return -1;
    }
    
    // Sauvegarder token
    security_token_t new_token;
    strncpy(new_token.token, token, sizeof(new_token.token) - 1);
    new_token.last_update = time(NULL);
    new_token.validated = 1;
    security_save_token(&new_token);
    
    if (zarch_upload_package(token, filepath, name, version, release, arch) != 0) {
        return -1;
    }
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("✅ Publication completed successfully!\n");
    printf("📦 Package: %s %s-%s (%s)\n", name, version, release, arch);
    printf("🔗 Zarch Hub: %s/package/%s\n", ZARCH_HUB_URL, name);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  APSM - Zarch Hub Publisher v2.0\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
        printf("USAGE:\n");
        printf("  apsm push <file>     Publish package to Zarch Hub\n");
        printf("  apsm login           Authenticate to Zarch Hub\n");
        printf("  apsm status          Check authentication status\n");
        printf("\nEXAMPLES:\n");
        printf("  apsm push build/zarch-utils-v1.0.0-r1.x86_64.tar.bool\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        return 1;
    }
    
    security_init();
    
    if (strcmp(argv[1], "push") == 0) {
        if (argc < 3) {
            printf("[APSM] ❌ Missing file\n");
            return 1;
        }
        return publish_package(argv[2]);
    }
    else if (strcmp(argv[1], "login") == 0) {
        printf("[APSM] Use 'apsm push' to login when publishing\n");
        return 0;
    }
    else if (strcmp(argv[1], "status") == 0) {
        security_token_t token;
        if (security_load_token(&token) == 0) {
            printf("[APSM] ✅ Authenticated\n");
        } else {
            printf("[APSM] ❌ Not authenticated\n");
        }
        return 0;
    }
    else {
        printf("[APSM] ❌ Unknown command: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}
