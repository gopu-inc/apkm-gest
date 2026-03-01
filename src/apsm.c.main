#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <libgen.h>
#include "apkm.h"
#include "security.h"

#define MAX_DOC_SIZE 16384
#define MAX_ASSETS 50

// Prototypes des fonctions
int create_github_release(const char *token, const char *tag, const char *name,
                          const char *version, const char *release,
                          const char *arch, const char *sha256,
                          const char *doc_content, char *upload_url, size_t upload_url_size);
int upload_asset(const char *token, const char *upload_url, const char *filepath, 
                 const char *filename, const char *content_type);
int update_database(const char *token, const char *name, const char *version,
                    const char *release, const char *arch, const char *sha256,
                    const char *tag);

// Structure pour la réponse curl
struct curl_response {
    char *data;
    size_t size;
};

// Structure pour les assets
typedef struct {
    char name[256];
    char url[1024];
    char arch[32];
    char release[16];
    char version[64];
    char pkg_name[128];
} github_asset_t;

// Callback pour écrire la réponse
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

// Nettoyer une chaîne
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

// Charger la documentation depuis APKMBUILD ou README
char* load_documentation(char *buffer, size_t buffer_size) {
    buffer[0] = '\0';
    
    // Chercher APKMBUILD
    if (access("APKMBUILD", F_OK) == 0) {
        FILE *fp = fopen("APKMBUILD", "r");
        if (fp) {
            char line[1024];
            int in_doc_block = 0;
            char doc_content[8192] = "";
            
            while (fgets(line, sizeof(line), fp)) {
                line[strcspn(line, "\n")] = 0;
                
                if (strstr(line, "$APKMDOC::")) {
                    char *val = strstr(line, "::") + 2;
                    strcpy(doc_content, val);
                    clean_string(doc_content);
                    
                    // Vérifier le format spécial [%OPEN+==fichier]
                    char *open_marker = strstr(doc_content, "[%OPEN+==");
                    if (open_marker) {
                        char *file_start = open_marker + 9;
                        char *file_end = strchr(file_start, ']');
                        if (file_end) {
                            int file_len = file_end - file_start;
                            char filepath[512];
                            strncpy(filepath, file_start, file_len);
                            filepath[file_len] = '\0';
                            
                            FILE *rf = fopen(filepath, "r");
                            if (rf) {
                                size_t total = 0;
                                char rline[256];
                                while (fgets(rline, sizeof(rline), rf) && total < buffer_size - 1) {
                                    size_t len = strlen(rline);
                                    if (total + len < buffer_size - 1) {
                                        strcpy(buffer + total, rline);
                                        total += len;
                                    }
                                }
                                fclose(rf);
                                fclose(fp);
                                return buffer;
                            }
                        }
                    } else {
                        strncpy(buffer, doc_content, buffer_size - 1);
                        fclose(fp);
                        return buffer;
                    }
                }
                
                if (strstr(line, "$APKMDOC::{")) {
                    in_doc_block = 1;
                    continue;
                }
                if (in_doc_block) {
                    if (strstr(line, "}")) {
                        in_doc_block = 0;
                    } else {
                        if (strlen(buffer) + strlen(line) + 1 < buffer_size) {
                            strcat(buffer, line);
                            strcat(buffer, "\n");
                        }
                    }
                }
            }
            fclose(fp);
        }
    }
    
    // Si pas trouvé, chercher README.md
    if (strlen(buffer) == 0) {
        const char *readme_files[] = {
            "README.md", "README", "readme.md",
            "Readme.md", "README.txt", "README.rst",
            "docs/README.md", "doc/README.md",
            NULL
        };
        
        for (int i = 0; readme_files[i] != NULL; i++) {
            if (access(readme_files[i], F_OK) == 0) {
                FILE *rf = fopen(readme_files[i], "r");
                if (rf) {
                    size_t total = 0;
                    char rline[256];
                    
                    snprintf(buffer, buffer_size, "## Documentation from %s\n\n", readme_files[i]);
                    total = strlen(buffer);
                    
                    while (fgets(rline, sizeof(rline), rf) && total < buffer_size - 1) {
                        size_t len = strlen(rline);
                        if (total + len < buffer_size - 1) {
                            strcpy(buffer + total, rline);
                            total += len;
                        }
                    }
                    fclose(rf);
                    break;
                }
            }
        }
    }
    
    if (strlen(buffer) == 0) {
        snprintf(buffer, buffer_size, 
                 "No documentation provided.\n"
                 "Please refer to the project website for more information.\n");
    }
    
    return buffer;
}

// Extraire les infos du package depuis le nom de fichier
void parse_package_filename(const char *filename, char *name, char *version, 
                            char *release, char *arch, char *suffix) {
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
    
    // Chercher le format avec suffixe ( -r1, -r2, etc.)
    char *release_start = strstr(version_start + 2, "-r");
    if (release_start) {
        int ver_len = release_start - (version_start + 2);
        strncpy(version, version_start + 2, ver_len);
        version[ver_len] = '\0';
        
        suffix[0] = 'r';
        suffix[1] = '\0';
        
        char *arch_start = strchr(release_start + 2, '.');
        if (arch_start) {
            int rel_len = arch_start - (release_start + 2);
            strncpy(release, release_start + 2, rel_len);
            release[rel_len] = '\0';
            
            strncpy(arch, arch_start + 1, 31);
            arch[31] = '\0';
        }
    } else {
        // Format sans suffixe
        char *arch_start = strchr(version_start + 2, '.');
        if (arch_start) {
            int ver_len = arch_start - (version_start + 2);
            
            // Vérifier si la version contient déjà un tiret (comme 2.0.0-1)
            char *dash_in_version = strstr(version_start + 2, "-");
            if (dash_in_version && dash_in_version < arch_start) {
                int dash_pos = dash_in_version - (version_start + 2);
                strncpy(version, version_start + 2, dash_pos);
                version[dash_pos] = '\0';
                
                strcpy(suffix, "");
                int rel_len = arch_start - (dash_in_version + 1);
                strncpy(release, dash_in_version + 1, rel_len);
                release[rel_len] = '\0';
            } else {
                strncpy(version, version_start + 2, ver_len);
                version[ver_len] = '\0';
                strcpy(release, "0");
                strcpy(suffix, "");
            }
            
            strncpy(arch, arch_start + 1, 31);
            arch[31] = '\0';
        }
    }
}

// Créer une release sur GitHub
int create_github_release(const char *token, const char *tag, const char *name,
                          const char *version, const char *release,
                          const char *arch, const char *sha256,
                          const char *doc_content, char *upload_url, size_t upload_url_size) {
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct curl_response resp = {0};
    struct curl_slist *headers = NULL;
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);
    
    char url[512];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/releases",
             REPO_OWNER, REPO_NAME);
    
    // Échapper la documentation pour JSON
    char doc_escaped[MAX_DOC_SIZE * 2] = "";
    escape_json(doc_content, doc_escaped, sizeof(doc_escaped));
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Construire le body
    char body_text[MAX_DOC_SIZE * 2];
    snprintf(body_text, sizeof(body_text),
             "# %s %s\n\n"
             "## Package Information\n"
             "- **Version:** %s\n"
             "- **Release:** %s\n"
             "- **Architecture:** %s\n"
             "- **SHA256:** `%s`\n"
             "- **Published:** %s\n\n"
             "## Documentation\n"
             "%s\n\n"
             "## Installation\n"
             "```bash\n"
             "apkm install %s@%s\n"
             "```",
             name, tag, version, release, arch, sha256, date_str,
             doc_escaped, name, version);
    
    // Échapper le body
    char body_escaped[MAX_DOC_SIZE * 4] = "";
    escape_json(body_text, body_escaped, sizeof(body_escaped));
    
    // Construire le JSON final
    char post_data[MAX_DOC_SIZE * 8];
    snprintf(post_data, sizeof(post_data),
             "{"
             "\"tag_name\": \"%s\","
             "\"target_commitish\": \"main\","
             "\"name\": \"%s %s\","
             "\"body\": \"%s\","
             "\"draft\": false,"
             "\"prerelease\": false"
             "}", tag, name, tag, body_escaped);
    
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
        if (resp.data) {
            fprintf(stderr, "Response: %s\n", resp.data);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(resp.data);
        return -1;
    }
    
    // Extraire l'upload URL
    char *upload_url_ptr = strstr(resp.data, "\"upload_url\":\"");
    if (upload_url_ptr) {
        upload_url_ptr += 14;
        char *end = strchr(upload_url_ptr, '"');
        if (end) {
            int len = (int)(end - upload_url_ptr);
            if (len < (int)upload_url_size - 1) {
                strncpy(upload_url, upload_url_ptr, len);
                upload_url[len] = '\0';
                char *brace = strchr(upload_url, '{');
                if (brace) *brace = '\0';
            }
        }
    }
    
    int release_id = 0;
    char *id_ptr = strstr(resp.data, "\"id\":");
    if (id_ptr) {
        id_ptr += 5;
        release_id = atoi(id_ptr);
    }
    
    printf("[APSM] ✅ Release created (ID: %d)\n", release_id);
    printf("[APSM] 🔗 https://github.com/%s/%s/releases/tag/%s\n", 
           REPO_OWNER, REPO_NAME, tag);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    
    return release_id;
}

// Upload un asset vers une release
int upload_asset(const char *token, const char *upload_url, const char *filepath, 
                 const char *filename, const char *content_type) {
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
    snprintf(url, sizeof(url), "%s?name=%s", upload_url, filename);
    
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, content_type);
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

// Mettre à jour DATA.db
int update_database(const char *token, const char *name, const char *version,
                    const char *release, const char *arch, const char *sha256,
                    const char *tag) {
    
    char package_url[1024];
    
    // Construire l'URL avec le bon format (garder le r)
    if (release[0] == 'r' || (release[0] >= '0' && release[0] <= '9')) {
        snprintf(package_url, sizeof(package_url),
                 "https://github.com/%s/%s/releases/download/%s/%s-v%s-%s.%s.tar.bool",
                 REPO_OWNER, REPO_NAME, tag, name, version, release, arch);
    } else {
        snprintf(package_url, sizeof(package_url),
                 "https://github.com/%s/%s/releases/download/%s/%s-v%s.%s.tar.bool",
                 REPO_OWNER, REPO_NAME, tag, name, version, arch);
    }
    
    // Télécharger DATA.db actuel
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "curl -s %s/DATA.db > /tmp/DATA.db 2>/dev/null", REPO_RAW);
    system(cmd);
    
    // Lire le fichier existant pour vérifier les doublons
    FILE *old_db = fopen("/tmp/DATA.db", "r");
    FILE *new_db = fopen("/tmp/DATA.new", "w");
    
    if (!new_db) {
        if (old_db) fclose(old_db);
        return -1;
    }
    
    // Copier les anciennes entrées (sauf si c'est un doublon)
    if (old_db) {
        char line[1024];
        int found = 0;
        
        while (fgets(line, sizeof(line), old_db)) {
            char n[256], v[64];
            if (sscanf(line, "%[^|]|%[^|]", n, v) == 2) {
                if (strcmp(n, name) == 0 && strcmp(v, version) == 0) {
                    found = 1;
                    continue;  // Ne pas copier l'ancienne version
                }
            }
            fputs(line, new_db);
        }
        fclose(old_db);
        
        if (found) {
            printf("[APSM] 📝 Updating existing entry for %s %s\n", name, version);
        }
    }
    
    // Ajouter la nouvelle entrée
    fprintf(new_db, "%s|%s|%s|%s|%lld|%s|%s\n", 
            name, version, release, arch, (long long)time(NULL), sha256, package_url);
    fclose(new_db);
    
    // Remplacer l'ancien fichier
    rename("/tmp/DATA.new", "/tmp/DATA.db");
    
    // Lire le fichier et l'encoder en base64
    FILE *f = fopen("/tmp/DATA.db", "rb");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char *buffer = malloc(fsize + 1);
    if (!buffer) {
        fclose(f);
        return -1;
    }
    
    fread(buffer, 1, fsize, f);
    buffer[fsize] = '\0';
    fclose(f);
    
    // Encoder en base64 (version simplifiée)
    char *base64 = calloc(fsize * 2 + 1, 1);
    if (!base64) {
        free(buffer);
        return -1;
    }
    
    for (long i = 0; i < fsize; i += 3) {
        sprintf(base64 + strlen(base64), "%02x%02x%02x", 
                buffer[i], 
                i+1 < fsize ? buffer[i+1] : 0,
                i+2 < fsize ? buffer[i+2] : 0);
    }
    
    // Upload vers GitHub
    CURL *curl = curl_easy_init();
    if (!curl) {
        free(buffer);
        free(base64);
        return -1;
    }
    
    struct curl_response resp = {0};
    struct curl_slist *headers = NULL;
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);
    
    char api_url[512];
    snprintf(api_url, sizeof(api_url), 
             "https://api.github.com/repos/%s/%s/contents/DATA.db",
             REPO_OWNER, REPO_NAME);
    
    char put_data[8192];
    snprintf(put_data, sizeof(put_data),
             "{"
             "\"message\": \"Update package database for %s %s\","
             "\"content\": \"%s\","
             "\"branch\": \"main\""
             "}", name, version, base64);
    
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, put_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    
    free(buffer);
    free(base64);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (resp.data) free(resp.data);
    
    unlink("/tmp/DATA.db");
    
    if (res == CURLE_OK) {
        printf("[APSM] 📊 Database updated for %s %s\n", name, version);
        return 0;
    } else {
        printf("[APSM] ❌ Failed to update database\n");
        return -1;
    }
}

// Commande de publication
int publish_package(const char *filepath) {
    char token[512];
    
    // Charger le token à chaque utilisation
    if (security_get_token(token, sizeof(token)) != 0) {
        printf("[APSM] ❌ Not authenticated. Run 'apsm auth <token>'\n");
        return -1;
    }
    
    printf("[APSM] 🔐 Token loaded securely\n");
    
    if (access(filepath, F_OK) != 0) {
        printf("[APSM] ❌ File not found: %s\n", filepath);
        memset(token, 0, sizeof(token));
        return -1;
    }
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APSM - GitHub Publisher v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    char name[256] = "", version[64] = "", release[16] = "", arch[32] = "", suffix[8] = "";
    parse_package_filename(filepath, name, version, release, arch, suffix);
    
    printf("📦 Package: %s\n", name);
    printf("🏷️  Version: %s\n", version);
    printf("🔧 Release: %s\n", release);
    printf("🔧 Arch:    %s\n", arch);
    printf("🔑 Suffix:  %s\n", suffix);
    
    char sha256[128];
    if (calculate_sha256(filepath, sha256) != 0) {
        printf("[APSM] ❌ Failed to calculate SHA256\n");
        memset(token, 0, sizeof(token));
        return -1;
    }
    printf("🔏 SHA256:  %.32s...\n", sha256);
    
    char doc_content[MAX_DOC_SIZE] = "";
    load_documentation(doc_content, sizeof(doc_content));
    
    if (strlen(doc_content) > 0) {
        printf("📚 Documentation loaded (%zu bytes)\n", strlen(doc_content));
    } else {
        printf("📚 No documentation found\n");
    }
    
    char tag[64];
    // Format du tag: v2.0.0-r1
    if (release[0] == 'r' || (release[0] >= '0' && release[0] <= '9')) {
        snprintf(tag, sizeof(tag), "v%s-%s", version, release);
    } else {
        snprintf(tag, sizeof(tag), "v%s", version);
    }
    
    char upload_url[512];
    int release_id = create_github_release(token, tag, name, version, release,
                                           arch, sha256, doc_content,
                                           upload_url, sizeof(upload_url));
    
    // Effacer le token de la mémoire après utilisation
    memset(token, 0, sizeof(token));
    
    if (release_id <= 0) {
        return -1;
    }
    
    // Préparer le nom du fichier avec le bon format
    char filename[256];
    if (release[0] == 'r' || (release[0] >= '0' && release[0] <= '9')) {
        snprintf(filename, sizeof(filename), "%s-v%s-%s.%s.tar.bool", 
                 name, version, release, arch);
    } else {
        snprintf(filename, sizeof(filename), "%s-v%s.%s.tar.bool", 
                 name, version, arch);
    }
    
    if (upload_asset(token, upload_url, filepath, filename,
                     "Content-Type: application/octet-stream") != 0) {
        return -1;
    }
    
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "build/%s.manifest", name);
    if (access(manifest_path, F_OK) == 0) {
        char manifest_name[256];
        snprintf(manifest_name, sizeof(manifest_name), "%s.manifest", name);
        upload_asset(token, upload_url, manifest_path, manifest_name,
                     "Content-Type: text/plain");
    }
    
    char sha_path[512];
    snprintf(sha_path, sizeof(sha_path), "%s.sha256", filepath);
    if (access(sha_path, F_OK) == 0) {
        char sha_name[256];
        if (release[0] == 'r' || (release[0] >= '0' && release[0] <= '9')) {
            snprintf(sha_name, sizeof(sha_name), "%s-v%s-%s.%s.tar.bool.sha256",
                     name, version, release, arch);
        } else {
            snprintf(sha_name, sizeof(sha_name), "%s-v%s.%s.tar.bool.sha256",
                     name, version, arch);
        }
        upload_asset(token, upload_url, sha_path, sha_name,
                     "Content-Type: text/plain");
    }
    
    // Recharger le token pour la mise à jour de la base de données
    if (security_get_token(token, sizeof(token)) != 0) {
        printf("[APSM] ⚠️ Cannot update database: token lost\n");
    } else {
        update_database(token, name, version, release, arch, sha256, tag);
        memset(token, 0, sizeof(token));
    }
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("✅ Publication completed successfully!\n");
    printf("📦 Release: https://github.com/%s/%s/releases/tag/%s\n", 
           REPO_OWNER, REPO_NAME, tag);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    return 0;
}

// Commande d'authentification
int auth_command(const char *raw_token) {
    security_token_t token_struct;
    strncpy(token_struct.token, raw_token, sizeof(token_struct.token) - 1);
    token_struct.last_update = time(NULL);
    token_struct.validated = 1;
    
    if (security_save_token(&token_struct) == 0) {
        printf("[APSM] 🔐 Token saved securely in %s\n", TOKEN_PATH);
        
        // Effacer de la mémoire
        memset(&token_struct, 0, sizeof(token_struct));
        return 0;
    }
    
    printf("[APSM] ❌ Failed to save token\n");
    return -1;
}

// Commande de statut
int status_command(void) {
    char token[512];
    
    if (security_get_token(token, sizeof(token)) == 0) {
        printf("[APSM] ✅ Authenticated\n");
        printf("[APSM] 📁 Token file: %s\n", TOKEN_PATH);
        printf("[APSM] 🔐 Token is securely stored\n");
        
        // Vérifier l'âge du fichier
        struct stat st;
        if (stat(TOKEN_PATH, &st) == 0) {
            time_t now = time(NULL);
            if (now - st.st_mtime < 86400) {
                printf("[APSM] ✅ Token file is recent (<24h)\n");
            } else {
                printf("[APSM] ⚠️ Token file is old (>24h), consider syncing\n");
            }
        }
        
        memset(token, 0, sizeof(token));
        return 0;
    } else {
        printf("[APSM] ❌ Not authenticated\n");
        printf("[APSM] 👉 Run 'apsm auth <token>' to configure\n");
        return -1;
    }
}

// Commande de synchronisation
int sync_command(void) {
    printf("[APSM] 🔄 Syncing token from GitHub...\n");
    
    if (security_download_token() == 0) {
        printf("[APSM] ✅ Token synced successfully\n");
        return status_command();
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
             "curl -s %s/DATA.db 2>/dev/null | column -t -s '|'", REPO_RAW);
    int ret = system(cmd);
    
    if (ret != 0) {
        printf("[APSM] No packages found or database unavailable\n");
    }
    
    return 0;
}

// Commande d'aide
void print_help(void) {
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APSM - GitHub Publisher for APKM v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("USAGE:\n");
    printf("  apsm <COMMAND> [OPTIONS]\n\n");
    printf("COMMANDS:\n");
    printf("  auth <token>              Save GitHub token (encrypted with BTSCRYPT)\n");
    printf("  push <file>                Publish package to GitHub Releases\n");
    printf("  status                     Check authentication status\n");
    printf("  sync                       Sync token from GitHub\n");
    printf("  list                       List published packages\n");
    printf("  help                       Show this help\n\n");
    printf("EXAMPLES:\n");
    printf("  apsm auth ghp_xxxxxxxxxxxx\n");
    printf("  apsm push build/apkm-v2.0.0-r1.x86_64.tar.bool\n");
    printf("  apsm status\n");
    printf("  apsm list\n\n");
    printf("DOCUMENTATION:\n");
    printf("  Use $APKMDOC::[%%OPEN+==README.md] in APKMBUILD to include docs\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
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
