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

// Structure pour la réponse curl
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

// Extraire les infos du package depuis le nom de fichier
void parse_package_filename(const char *filename, char *name, char *version, char *release, char *arch) {
    // Format: super-app-v1.0.0-r1.x86_64.tar.bool
    char temp[512];
    strncpy(temp, filename, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    char *base = strrchr(temp, '/');
    if (base) base++; else base = temp;
    
    // Enlever .tar.bool
    char *ext = strstr(base, ".tar.bool");
    if (ext) *ext = '\0';
    
    // Parser le nom
    char *version_start = strstr(base, "-v");
    if (version_start) {
        int name_len = version_start - base;
        strncpy(name, base, name_len);
        name[name_len] = '\0';
        
        char *arch_start = strstr(version_start + 2, ".");
        if (arch_start) {
            int ver_len = arch_start - (version_start + 2);
            strncpy(version, version_start + 2, ver_len);
            version[ver_len] = '\0';
            
            char *release_start = strstr(version, "-r");
            if (release_start) {
                *release_start = '\0';
                strcpy(release, release_start + 1);
            } else {
                strcpy(release, "r0");
            }
            
            strcpy(arch, arch_start + 1);
        }
    }
}

// Créer une release sur GitHub
int create_github_release(const char *token, const char *tag, const char *name, 
                          const char *body, const char *commitish) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct curl_response resp = {0};
    struct curl_slist *headers = NULL;
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/repos/%s/%s/releases", REPO_API, REPO_OWNER, REPO_NAME);
    
    // Créer le JSON
    struct json_object *release_json = json_object_new_object();
    json_object_object_add(release_json, "tag_name", json_object_new_string(tag));
    json_object_object_add(release_json, "target_commitish", json_object_new_string(commitish));
    json_object_object_add(release_json, "name", json_object_new_string(name));
    json_object_object_add(release_json, "body", json_object_new_string(body));
    json_object_object_add(release_json, "draft", json_object_new_boolean(0));
    json_object_object_add(release_json, "prerelease", json_object_new_boolean(0));
    
    const char *post_data = json_object_to_json_string(release_json);
    
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "User-Agent: APSM-Publisher/2.0");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    printf("[APSM] 📦 Creating release %s...\n", tag);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (res != CURLE_OK || http_code != 201) {
        fprintf(stderr, "[APSM] ❌ Failed to create release (HTTP %ld)\n", http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(resp.data);
        json_object_put(release_json);
        return -1;
    }
    
    // Extraire l'upload URL
    struct json_object *resp_json = json_tokener_parse(resp.data);
    const char *upload_url = NULL;
    int release_id = 0;
    
    if (resp_json) {
        struct json_object *upload_url_obj, *id_obj;
        if (json_object_object_get_ex(resp_json, "upload_url", &upload_url_obj)) {
            upload_url = json_object_get_string(upload_url_obj);
        }
        if (json_object_object_get_ex(resp_json, "id", &id_obj)) {
            release_id = json_object_get_int(id_obj);
        }
        json_object_put(resp_json);
    }
    
    printf("[APSM] ✅ Release created: https://github.com/%s/%s/releases/tag/%s\n", 
           REPO_OWNER, REPO_NAME, tag);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    json_object_put(release_json);
    
    return release_id;
}

// Upload un asset vers une release
int upload_asset(const char *token, int release_id, const char *filepath, const char *filename) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct curl_response resp = {0};
    struct curl_slist *headers = NULL;
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
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);
    
    char url[1024];
    snprintf(url, sizeof(url), 
             "https://uploads.github.com/repos/%s/%s/releases/%d/assets?name=%s",
             REPO_OWNER, REPO_NAME, release_id, filename);
    
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "User-Agent: APSM-Publisher/2.0");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_READDATA, file);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)file_stat.st_size);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    printf("[APSM] 📤 Uploading %s (%.2f KB)...\n", 
           filename, file_stat.st_size / 1024.0);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(file);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (res != CURLE_OK || http_code != 201) {
        fprintf(stderr, "[APSM] ❌ Upload failed (HTTP %ld)\n", http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(resp.data);
        return -1;
    }
    
    printf("[APSM] ✅ Uploaded: %s\n", filename);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    
    return 0;
}

// Mettre à jour le fichier DATA.db
int update_database(const char *token, const package_metadata_t *metadata) {
    // Télécharger DATA.db actuel
    char url[512];
    snprintf(url, sizeof(url), "%s/DATA.db", REPO_RAW);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "curl -s %s > /tmp/DATA.db", url);
    system(cmd);
    
    // Ajouter la nouvelle entrée
    FILE *db = fopen("/tmp/DATA.db", "a");
    if (!db) return -1;
    
    fprintf(db, "%s|%s|%s|%s|%lld|%s\n", 
            metadata->name, metadata->version, metadata->release,
            metadata->sha256, (long long)metadata->timestamp,
            metadata->url);
    fclose(db);
    
    // Upload vers GitHub
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct curl_response resp = {0};
    struct curl_slist *headers = NULL;
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);
    
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/contents/DATA.db",
             REPO_OWNER, REPO_NAME);
    
    // Lire le fichier et l'encoder en base64
    FILE *f = fopen("/tmp/DATA.db", "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char *buffer = malloc(fsize);
    fread(buffer, 1, fsize, f);
    fclose(f);
    
    char *base64 = calloc(fsize * 2, 1);
    EVP_EncodeBlock((unsigned char*)base64, buffer, fsize);
    
    struct json_object *update_json = json_object_new_object();
    json_object_object_add(update_json, "message", 
                          json_object_new_string("Update package database"));
    json_object_object_add(update_json, "content", json_object_new_string(base64));
    json_object_object_add(update_json, "branch", json_object_new_string("main"));
    
    const char *put_data = json_object_to_json_string(update_json);
    
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, put_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    
    free(buffer);
    free(base64);
    json_object_put(update_json);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    
    unlink("/tmp/DATA.db");
    
    return (res == CURLE_OK) ? 0 : -1;
}

// Commande de publication
int publish_package(const char *filepath) {
    security_token_t token;
    
    if (security_load_token(&token) != 0) {
        printf("[APSM] ❌ Not authenticated. Run 'apsm auth <token>'\n");
        return -1;
    }
    
    if (access(filepath, F_OK) != 0) {
        printf("[APSM] ❌ File not found: %s\n", filepath);
        return -1;
    }
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APSM - GitHub Publisher v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    // Extraire les infos du package
    char name[256] = "", version[64] = "", release[16] = "", arch[32] = "";
    parse_package_filename(filepath, name, version, release, arch);
    
    printf("📦 Package: %s\n", name);
    printf("🏷️  Version: %s-%s\n", version, release);
    printf("🔧 Arch:    %s\n", arch);
    
    // Calculer SHA256
    char sha256[128];
    if (calculate_sha256(filepath, sha256) != 0) {
        printf("[APSM] ❌ Failed to calculate SHA256\n");
        return -1;
    }
    printf("🔏 SHA256:  %.32s...\n", sha256);
    
    // Créer le tag
    char tag[64];
    snprintf(tag, sizeof(tag), "v%s-%s", version, release);
    
    // Créer le body de la release
    char body[2048];
    snprintf(body, sizeof(body),
             "# %s %s-%s\n\n"
             "## Package Information\n"
             "- **Name:** %s\n"
             "- **Version:** %s\n"
             "- **Release:** %s\n"
             "- **Architecture:** %s\n"
             "- **SHA256:** `%s`\n\n"
             "## Installation\n"
             "```bash\n"
             "apkm install %s@%s\n"
             "```\n",
             name, version, release,
             name, version, release, arch, sha256,
             name, version);
    
    // Créer la release
    int release_id = create_github_release(token.token, tag, name, body, "main");
    if (release_id <= 0) {
        return -1;
    }
    
    // Upload le package
    char filename[256];
    snprintf(filename, sizeof(filename), "%s-v%s-%s.%s.tar.bool", 
             name, version, release, arch);
    
    if (upload_asset(token.token, release_id, filepath, filename) != 0) {
        return -1;
    }
    
    // Upload le manifeste s'il existe
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "build/%s.manifest", name);
    if (access(manifest_path, F_OK) == 0) {
        char manifest_name[256];
        snprintf(manifest_name, sizeof(manifest_name), "%s.manifest", name);
        upload_asset(token.token, release_id, manifest_path, manifest_name);
    }
    
    // Upload le fichier SHA256
    char sha_path[512];
    snprintf(sha_path, sizeof(sha_path), "%s.sha256", filepath);
    if (access(sha_path, F_OK) == 0) {
        char sha_name[256];
        snprintf(sha_name, sizeof(sha_name), "%s-v%s-%s.%s.tar.bool.sha256",
                 name, version, release, arch);
        upload_asset(token.token, release_id, sha_path, sha_name);
    }
    
    // Mettre à jour la base de données
    package_metadata_t metadata;
    strcpy(metadata.name, name);
    strcpy(metadata.version, version);
    strcpy(metadata.release, release);
    strcpy(metadata.arch, arch);
    strcpy(metadata.sha256, sha256);
    metadata.timestamp = time(NULL);
    strcpy(metadata.publisher, "apsm-bot");
    snprintf(metadata.url, sizeof(metadata.url),
             "https://github.com/%s/%s/releases/download/%s/%s",
             REPO_OWNER, REPO_NAME, tag, filename);
    
    update_database(token.token, &metadata);
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("✅ Publication completed successfully!\n");
    printf("📦 Release: https://github.com/%s/%s/releases/tag/%s\n", 
           REPO_OWNER, REPO_NAME, tag);
    printf("📊 Database: https://github.com/%s/%s/blob/main/DATA.db\n",
           REPO_OWNER, REPO_NAME);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return 0;
}

// Commande d'authentification
int auth_command(const char *raw_token) {
    security_token_t token;
    strncpy(token.token, raw_token, sizeof(token.token) - 1);
    token.last_update = time(NULL);
    token.validated = 1;
    
    if (security_save_token(&token) == 0) {
        printf("[APSM] 🔐 Token saved securely in %s\n", TOKEN_PATH);
        return 0;
    }
    
    printf("[APSM] ❌ Failed to save token\n");
    return -1;
}

// Commande de statut
int status_command(void) {
    security_token_t token;
    
    if (security_load_token(&token) == 0) {
        printf("[APSM] ✅ Authenticated (token: %.10s...)\n", token.token);
        printf("[APSM] 📁 Token: %s\n", TOKEN_PATH);
        return 0;
    } else {
        printf("[APSM] ❌ Not authenticated\n");
        printf("[APSM] 👉 Run 'apsm auth <token>' to configure\n");
        return -1;
    }
}

// Commande de synchronisation
int sync_command(void) {
    printf("[APSM] 🔄 Syncing token...\n");
    
    if (security_download_token() == 0) {
        printf("[APSM] ✅ Token synced from GitHub\n");
        return 0;
    }
    
    printf("[APSM] ❌ Sync failed\n");
    return -1;
}

// Commande de liste
int list_command(void) {
    printf("[APSM] 📋 Published packages:\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "curl -s %s/DATA.db | column -t -s '|'", REPO_RAW);
    system(cmd);
    
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  APSM - GitHub Publisher for APKM\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
        printf("Usage:\n");
        printf("  apsm auth <token>     Save GitHub token\n");
        printf("  apsm push <file>      Publish package to GitHub Releases\n");
        printf("  apsm status           Check authentication status\n");
        printf("  apsm sync             Sync token from GitHub\n");
        printf("  apsm list             List published packages\n\n");
        return 1;
    }
    
    security_init();
    
    if (strcmp(argv[1], "auth") == 0) {
        if (argc < 3) {
            printf("[APSM] ❌ Missing token\n");
            return 1;
        }
        return auth_command(argv[2]);
    }
    else if (strcmp(argv[1], "push") == 0) {
        if (argc < 3) {
            printf("[APSM] ❌ Missing file\n");
            return 1;
        }
        return publish_package(argv[2]);
    }
    else if (strcmp(argv[1], "status") == 0) {
        return status_command();
    }
    else if (strcmp(argv[1], "sync") == 0) {
        return sync_command();
    }
    else if (strcmp(argv[1], "list") == 0) {
        return list_command();
    }
    else {
        printf("[APSM] ❌ Unknown command: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}
