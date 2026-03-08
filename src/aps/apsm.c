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
#define TOKEN_PATH "/usr/local/share/apkm/PROTOCOLE/security/tokens/auth.token"

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

// Fonction pour sauvegarder le token
static int save_token(const char *token) {
    // Créer le répertoire si nécessaire
    mkdir("/usr/local/share/apkm", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/tokens", 0755);
    
    FILE *f = fopen(TOKEN_PATH, "w");
    if (!f) return -1;
    
    fprintf(f, "%s\n", token);
    fclose(f);
    
    // Permissions strictes
    chmod(TOKEN_PATH, 0600);
    
    return 0;
}

// Fonction pour charger le token
static char* load_token(void) {
    FILE *f = fopen(TOKEN_PATH, "r");
    if (!f) return NULL;
    
    static char token[512];
    if (fgets(token, sizeof(token), f)) {
        // Enlever le retour à la ligne
        size_t len = strlen(token);
        if (len > 0 && token[len-1] == '\n') token[len-1] = '\0';
        fclose(f);
        return token;
    }
    
    fclose(f);
    return NULL;
}

// Login à Zarch Hub
int zarch_login(const char *username, const char *password) {
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
                const char *token = json_object_get_string(token_obj);
                save_token(token);
                success = 0;
                printf("[APSM] ✅ Authentication successful\n");
                printf("[APSM] 🔑 Token saved to %s\n", TOKEN_PATH);
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
    } else {
        printf("[APSM] ❌ Connection failed: %s\n", curl_easy_strerror(res));
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    
    return success;
}

// Upload du package vers Zarch Hub
int zarch_upload_package(const char *filepath, const char *name, const char *version,
                         const char *release, const char *arch) {
    // Charger le token
    char *token = load_token();
    if (!token) {
        printf("[APSM] ❌ Not authenticated. Please run 'apsm login' first.\n");
        return -1;
    }
    
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
    
    // Barre de progression simple
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    
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
    
    // Enlever l'extension .tar.bool
    char *ext = strstr(base, ".tar.bool");
    if (ext) *ext = '\0';
    
    // Format: nom-v1.0.0-r1.x86_64
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
        printf("Expected format: name-v1.0.0-r1.arch.tar.bool\n");
        return -1;
    }
    
    printf("📦 Package: %s\n", name);
    printf("📌 Version: %s\n", version);
    printf("🔖 Release: %s\n", release);
    printf("🔧 Arch:    %s\n", arch);
    
    return zarch_upload_package(filepath, name, version, release, arch);
}

int cmd_login(void) {
    char username[256];
    char password[256];
    
    printf("\n🔐 Zarch Hub Login\n");
    printf("━━━━━━━━━━━━━━━━━━\n\n");
    
    printf("Username: ");
    fflush(stdout);
    if (!fgets(username, sizeof(username), stdin)) return -1;
    username[strcspn(username, "\n")] = 0;
    
    printf("Password: ");
    fflush(stdout);
    
    // Désactiver l'écho pour le mot de passe (version simple)
    system("stty -echo");
    if (!fgets(password, sizeof(password), stdin)) {
        system("stty echo");
        return -1;
    }
    system("stty echo");
    printf("\n");
    
    password[strcspn(password, "\n")] = 0;
    
    return zarch_login(username, password);
}

int cmd_status(void) {
    char *token = load_token();
    if (token) {
        printf("[APSM] ✅ Authenticated\n");
        printf("[APSM] 🔑 Token: %s\n", token);
        
        // Optionnel: vérifier si le token est toujours valide
        printf("[APSM] ℹ️  Use 'apsm logout' to remove token\n");
        return 0;
    } else {
        printf("[APSM] ❌ Not authenticated\n");
        printf("[APSM] 💡 Run 'apsm login' to authenticate\n");
        return -1;
    }
}

int cmd_logout(void) {
    if (unlink(TOKEN_PATH) == 0) {
        printf("[APSM] ✅ Logged out successfully\n");
        return 0;
    } else {
        printf("[APSM] ❌ Not authenticated\n");
        return -1;
    }
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
        printf("  apsm logout          Remove saved token\n");
        printf("\nEXAMPLES:\n");
        printf("  apsm login\n");
        printf("  apsm push build/zarch-utils-v1.0.0-r1.x86_64.tar.bool\n");
        printf("  apsm status\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        return 1;
    }
    
    // Initialisation
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/tokens", 0755);
    
    if (strcmp(argv[1], "push") == 0) {
        if (argc < 3) {
            printf("[APSM] ❌ Missing file\n");
            return 1;
        }
        return publish_package(argv[2]);
    }
    else if (strcmp(argv[1], "login") == 0) {
        return cmd_login();
    }
    else if (strcmp(argv[1], "status") == 0) {
        return cmd_status();
    }
    else if (strcmp(argv[1], "logout") == 0) {
        return cmd_logout();
    }
    else {
        printf("[APSM] ❌ Unknown command: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}
