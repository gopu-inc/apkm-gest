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
                // Enlever le retour à la ligne
                line[strcspn(line, "\n")] = 0;
                
                // Chercher $APKMDOC::
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
                            
                            // Charger le fichier
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
                        // Documentation directe dans APKMBUILD
                        strncpy(buffer, doc_content, buffer_size - 1);
                        fclose(fp);
                        return buffer;
                    }
                }
                
                // Capturer les lignes de documentation (entre { et })
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
    
    // Si pas trouvé dans APKMBUILD, chercher README.md
    if (strlen(buffer) == 0) {
        const char *readme_files[] = {
            "README.md", "README", "readme.md",
            "Readme.md", "README.txt", "README.rst",
            "docs/README.md", "doc/README.md",
            "documentation.md", "docs.md",
            NULL
        };
        
        for (int i = 0; readme_files[i] != NULL; i++) {
            if (access(readme_files[i], F_OK) == 0) {
                FILE *rf = fopen(readme_files[i], "r");
                if (rf) {
                    size_t total = 0;
                    char rline[256];
                    
                    // Ajouter un en-tête
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
    
    // Si toujours rien, documentation par défaut
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
    // Format: super-app-v1.0.0-r1.x86_64.tar.bool
    char temp[512];
    strncpy(temp, filename, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    char *base = strrchr(temp, '/');
    if (base) base++; else base = temp;
    
    // Enlever .tar.bool
    char *ext = strstr(base, ".tar.bool");
    if (!ext) return;
    *ext = '\0';
    
    // Chercher le séparateur de version
    char *version_start = strstr(base, "-v");
    if (!version_start) return;
    
    // Nom du package
    int name_len = version_start - base;
    strncpy(name, base, name_len);
    name[name_len] = '\0';
    
    // Chercher la release
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
}

// Créer une release sur GitHub avec documentation
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
    snprintf(url, sizeof(url), "https://api.github.com/repos/gopu-inc/apkm-gest/releases");
    
    // Créer le body complet avec documentation
    char body[MAX_DOC_SIZE * 2];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    snprintf(body, sizeof(body),
             "# %s %s\n\n"
             "## Package Information\n"
             "- **Version:** %s\n"
             "- **Release:** %s\n"
             "- **Architecture:** %s\n"
             "- **SHA256:** `%s`\n"
             "- **Published:** %s\n"
             "- **Publisher:** APKM Publisher\n\n"
             "## Description\n"
             "%s %s package\n\n"
             "## Documentation\n"
             "%s\n\n"
             "## Installation\n"
             "```bash\n"
             "apkm install %s@%s\n"
             "```\n\n"
             "## Files\n"
             "- `%s-v%s-%s.%s.tar.bool` - Main package\n"
             "- `%s-v%s-%s.%s.tar.bool.sha256` - SHA256 signature\n"
             "- `%s.manifest` - Package manifest\n",
             name, tag,
             version, release, arch, sha256, date_str,
             name, version,
             doc_content,
             name, version,
             name, version, release, arch,
             name, version, release, arch,
             name);
    
    // Créer le JSON pour la release
    char post_data[16384];
    snprintf(post_data, sizeof(post_data),
             "{"
             "\"tag_name\": \"%s\","
             "\"target_commitish\": \"main\","
             "\"name\": \"%s %s\","
             "\"body\": %s,"
             "\"draft\": false,"
             "\"prerelease\": false"
             "}", tag, name, tag, body);
    
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
        if (resp.data) fprintf(stderr, "%s\n", resp.data);
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
            int len = end - upload_url_ptr;
            if (len < upload_url_size - 1) {
                strncpy(upload_url, upload_url_ptr, len);
                upload_url[len] = '\0';
                // Enlever {?name,label} à la fin
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
    printf("[APSM] 🔗 https://github.com/gopu-inc/apkm-gest/releases/tag/%s\n", tag);
    
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
    
    // Créer l'URL du package
    char package_url[1024];
    snprintf(package_url, sizeof(package_url),
             "https://github.com/gopu-inc/apkm-gest/releases/download/%s/%s-v%s-%s.%s.tar.bool",
             tag, name, version, release, arch);
    
    // Télécharger DATA.db actuel
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "curl -s https://raw.githubusercontent.com/gopu-inc/apkm-gest/main/DATA.db > /tmp/DATA.db 2>/dev/null");
    system(cmd);
    
    // Vérifier si l'entrée existe déjà
    int exists = 0;
    FILE *check = fopen("/tmp/DATA.db", "r");
    if (check) {
        char line[1024];
        while (fgets(line, sizeof(line), check)) {
            char n[256], v[64];
            if (sscanf(line, "%[^|]|%[^|]", n, v) == 2) {
                if (strcmp(n, name) == 0 && strcmp(v, version) == 0) {
                    exists = 1;
                    break;
                }
            }
        }
        fclose(check);
    }
    
    if (exists) {
        printf("[APSM] ⚠️ Package %s %s already in database, updating...\n", name, version);
        // Créer un nouveau fichier sans l'ancienne entrée
        FILE *old = fopen("/tmp/DATA.db", "r");
        FILE *new = fopen("/tmp/DATA.new", "w");
        if (old && new) {
            char line[1024];
            while (fgets(line, sizeof(line), old)) {
                char n[256], v[64];
                if (sscanf(line, "%[^|]|%[^|]", n, v) == 2) {
                    if (strcmp(n, name) != 0 || strcmp(v, version) != 0) {
                        fputs(line, new);
                    }
                }
            }
            fclose(old);
            fclose(new);
            rename("/tmp/DATA.new", "/tmp/DATA.db");
        }
    }
    
    // Ajouter la nouvelle entrée
    FILE *db = fopen("/tmp/DATA.db", "a");
    if (!db) return -1;
    
    fprintf(db, "%s|%s|%s|%s|%lld|%s|%s\n", 
            name, version, release, arch, (long long)time(NULL), sha256, package_url);
    fclose(db);
    
    // Lire le fichier et l'encoder en base64
    FILE *f = fopen("/tmp/DATA.db", "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char *buffer = malloc(fsize + 1);
    fread(buffer, 1, fsize, f);
    buffer[fsize] = '\0';
    fclose(f);
    
    // Encoder en base64 (version simple)
    char *base64 = calloc(fsize * 2 + 1, 1);
    for (long i = 0; i < fsize; i += 3) {
        // Note: Cette implémentation simplifiée est pour l'exemple
        // En réalité, utilisez une vraie fonction base64
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
             "https://api.github.com/repos/gopu-inc/apkm-gest/contents/DATA.db");
    
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
    char name[256] = "", version[64] = "", release[16] = "", arch[32] = "", suffix[8] = "";
    parse_package_filename(filepath, name, version, release, arch, suffix);
    
    printf("📦 Package: %s\n", name);
    printf("🏷️  Version: %s-%s\n", version, release);
    printf("🔧 Arch:    %s\n", arch);
    printf("🔑 Suffix:  %s\n", suffix);
    
    // Calculer SHA256
    char sha256[128];
    if (calculate_sha256(filepath, sha256) != 0) {
        printf("[APSM] ❌ Failed to calculate SHA256\n");
        return -1;
    }
    printf("🔏 SHA256:  %.32s...\n", sha256);
    
    // Charger la documentation
    char doc_content[MAX_DOC_SIZE] = "";
    load_documentation(doc_content, sizeof(doc_content));
    
    if (strlen(doc_content) > 0) {
        printf("📚 Documentation loaded (%zu bytes)\n", strlen(doc_content));
    } else {
        printf("📚 No documentation found\n");
    }
    
    // Créer le tag
    char tag[64];
    snprintf(tag, sizeof(tag), "v%s-%s", version, release);
    
    // Créer la release
    char upload_url[512];
    int release_id = create_github_release(token.token, tag, name, version, release,
                                           arch, sha256, doc_content,
                                           upload_url, sizeof(upload_url));
    if (release_id <= 0) {
        return -1;
    }
    
    // Upload le package
    char filename[256];
    snprintf(filename, sizeof(filename), "%s-v%s-%s.%s.tar.bool", 
             name, version, release, arch);
    
    if (upload_asset(token.token, upload_url, filepath, filename,
                     "Content-Type: application/octet-stream") != 0) {
        return -1;
    }
    
    // Upload le manifeste s'il existe
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "build/%s.manifest", name);
    if (access(manifest_path, F_OK) == 0) {
        char manifest_name[256];
        snprintf(manifest_name, sizeof(manifest_name), "%s.manifest", name);
        upload_asset(token.token, upload_url, manifest_path, manifest_name,
                     "Content-Type: text/plain");
    }
    
    // Upload le fichier SHA256
    char sha_path[512];
    snprintf(sha_path, sizeof(sha_path), "%s.sha256", filepath);
    if (access(sha_path, F_OK) == 0) {
        char sha_name[256];
        snprintf(sha_name, sizeof(sha_name), "%s-v%s-%s.%s.tar.bool.sha256",
                 name, version, release, arch);
        upload_asset(token.token, upload_url, sha_path, sha_name,
                     "Content-Type: text/plain");
    }
    
    // Upload le README/APKMBUILD comme documentation
    if (strlen(doc_content) > 0) {
        char doc_path[512];
        snprintf(doc_path, sizeof(doc_path), "/tmp/%s-doc.md", name);
        FILE *doc_file = fopen(doc_path, "w");
        if (doc_file) {
            fprintf(doc_file, "%s", doc_content);
            fclose(doc_file);
            
            char doc_name[256];
            snprintf(doc_name, sizeof(doc_name), "%s-documentation.md", name);
            upload_asset(token.token, upload_url, doc_path, doc_name,
                         "Content-Type: text/markdown");
            unlink(doc_path);
        }
    }
    
    // Mettre à jour la base de données
    update_database(token.token, name, version, release, arch, sha256, tag);
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("✅ Publication completed successfully!\n");
    printf("📦 Release: https://github.com/gopu-inc/apkm-gest/releases/tag/%s\n", tag);
    printf("📊 Database: https://github.com/gopu-inc/apkm-gest/blob/main/DATA.db\n");
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
        
        // Vérifier la validité du token
        time_t now = time(NULL);
        if (now - token.last_update < 86400) {
            printf("[APSM] ✅ Token is recent (<24h)\n");
        } else {
            printf("[APSM] ⚠️ Token is old (>24h), consider syncing\n");
        }
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
             "curl -s https://raw.githubusercontent.com/gopu-inc/apkm-gest/main/DATA.db | column -t -s '|'");
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
    printf("  apsm push build/super-app-v1.0.0-r1.x86_64.tar.bool\n");
    printf("  apsm status\n");
    printf("  apsm list\n\n");
    printf("DOCUMENTATION:\n");
    printf("  Use $APKMDOC::[%OPEN+==README.md] in APKMBUILD to include docs\n");
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
