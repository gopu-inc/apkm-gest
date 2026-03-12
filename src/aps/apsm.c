#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <dirent.h>
#include <libgen.h>
#include <json-c/json.h>
#include "apkm.h"
#include "security.h"

// Configuration
/*
#define ZARCH_HUB_URL "https://gsql-badge.onrender.com"
#define ZARCH_API_URL ZARCH_HUB_URL "/v5.2"
    */
#define TOKEN_PATH "/usr/local/share/apkm/PROTOCOLE/security/tokens/auth.token"
#define MANIFEST_NAME "Manifest.toml"

struct curl_response {
    char *data;
    size_t size;
};

static int debug_mode = 0;
static int quiet_mode = 0;

// Structure pour Manifest.toml
typedef struct {
    char name[256];
    char version[64];
    char release[16];
    char arch[32];
    char description[1024];
    char maintainer[256];
    char license[64];
    char homepage[256];
    char repository[256];
    char dependencies[2048];
    char build_deps[1024];
    char tags[512];
} manifest_t;

// ============================================================================
// FONCTIONS UTILITAIRES
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

void debug_print(const char *format, ...) {
    if (!debug_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[90m[DEBUG] ");
    vprintf(format, args);
    printf("\033[0m\n");
    va_end(args);
    fflush(stdout);
}

void print_info(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[36mℹ️  \033[0m");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_success(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[32m✅ \033[0m");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "\033[31m❌ \033[0m");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void print_warning(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[33m⚠️  \033[0m");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_step(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[35m▶ \033[0m");
    vprintf(format, args);
    printf("...\n");
    va_end(args);
}

// ============================================================================
// PARSEUR MANIFEST.TOML (version améliorée)
// ============================================================================

int parse_manifest(const char *path, manifest_t *manifest) {
    FILE *f = fopen(path, "r");
    if (!f) {
        debug_print("Cannot open manifest: %s", path);
        return -1;
    }
    
    char line[1024];
    char section[64] = "";
    int in_deps = 0;
    int in_build_deps = 0;
    
    memset(manifest, 0, sizeof(manifest_t));
    strcpy(manifest->arch, "x86_64");  // Default
    strcpy(manifest->release, "r0");   // Default
    strcpy(manifest->license, "MIT");  // Default
    
    while (fgets(line, sizeof(line), f)) {
        // Enlever le saut de ligne
        line[strcspn(line, "\n")] = 0;
        
        // Ignorer les lignes vides
        if (line[0] == '\0') continue;
        
        // Commentaires
        if (line[0] == '#') continue;
        
        // Détection des sections
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = 0;
                strcpy(section, line + 1);
                in_deps = (strcmp(section, "dependencies") == 0);
                in_build_deps = (strcmp(section, "build-dependencies") == 0);
            }
            continue;
        }
        
        // Parser les clés/valeurs
        char *eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = 0;
        char *key = line;
        char *value = eq + 1;
        
        // Nettoyer les espaces
        while (*key == ' ') key++;
        char *kend = key + strlen(key) - 1;
        while (kend > key && (*kend == ' ' || *kend == '\t')) *kend-- = 0;
        
        while (*value == ' ' || *value == '\t') value++;
        if (*value == '"') {
            value++;
            char *vend = strchr(value, '"');
            if (vend) *vend = 0;
        }
        
        debug_print("Section: %s, Key: %s, Value: %s", section, key, value);
        
        if (strcmp(section, "metadata") == 0) {
            if (strcmp(key, "name") == 0) strcpy(manifest->name, value);
            else if (strcmp(key, "version") == 0) strcpy(manifest->version, value);
            else if (strcmp(key, "release") == 0) strcpy(manifest->release, value);
            else if (strcmp(key, "arch") == 0) strcpy(manifest->arch, value);
            else if (strcmp(key, "description") == 0) strcpy(manifest->description, value);
            else if (strcmp(key, "maintainer") == 0) strcpy(manifest->maintainer, value);
            else if (strcmp(key, "license") == 0) strcpy(manifest->license, value);
            else if (strcmp(key, "homepage") == 0) strcpy(manifest->homepage, value);
            else if (strcmp(key, "repository") == 0) strcpy(manifest->repository, value);
        }
        else if (in_deps) {
            if (strlen(manifest->dependencies) > 0)
                strcat(manifest->dependencies, ";");
            strcat(manifest->dependencies, key);
            strcat(manifest->dependencies, "=");
            strcat(manifest->dependencies, value);
        }
        else if (in_build_deps) {
            if (strlen(manifest->build_deps) > 0)
                strcat(manifest->build_deps, ";");
            strcat(manifest->build_deps, key);
            strcat(manifest->build_deps, "=");
            strcat(manifest->build_deps, value);
        }
    }
    
    fclose(f);
    debug_print("Manifest parsed: %s %s-%s", manifest->name, manifest->version, manifest->release);
    return 0;
}

// ============================================================================
// GESTION DU TOKEN
// ============================================================================

static int save_token(const char *token) {
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/tokens", 0755);
    
    FILE *f = fopen(TOKEN_PATH, "w");
    if (!f) return -1;
    
    fprintf(f, "%s\n", token);
    fclose(f);
    chmod(TOKEN_PATH, 0600);
    
    debug_print("Token saved to %s", TOKEN_PATH);
    return 0;
}

static char* load_token(void) {
    FILE *f = fopen(TOKEN_PATH, "r");
    if (!f) return NULL;
    
    static char token[512];
    if (fgets(token, sizeof(token), f)) {
        token[strcspn(token, "\n")] = 0;
        fclose(f);
        return token;
    }
    
    fclose(f);
    return NULL;
}

// ============================================================================
// LOGIN À ZARCH HUB
// ============================================================================

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
    
    print_step("Authenticating to Zarch Hub");
    
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
                print_success("Authentication successful");
            } else {
                struct json_object *error_obj;
                if (json_object_object_get_ex(parsed, "error", &error_obj)) {
                    print_error("Authentication failed: %s", json_object_get_string(error_obj));
                } else {
                    print_error("Authentication failed");
                }
            }
            json_object_put(parsed);
        }
    } else {
        print_error("Connection failed: %s", curl_easy_strerror(res));
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    
    return success;
}

// ============================================================================
// EXTRACTION DU MANIFEST DEPUIS L'ARCHIVE
// ============================================================================

int extract_manifest(const char *archive_path, manifest_t *manifest) {
    char extract_dir[512];
    snprintf(extract_dir, sizeof(extract_dir), "/tmp/apsm_extract_%d", getpid());
    
    mkdir(extract_dir, 0755);
    
    // Extraire avec tar
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -xf '%s' -C '%s' 2>/dev/null", archive_path, extract_dir);
    
    if (system(cmd) != 0) {
        debug_print("Failed to extract archive");
        rmdir(extract_dir);
        return -1;
    }
    
    // Chercher Manifest.toml
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s", extract_dir, MANIFEST_NAME);
    
    // Chercher aussi à la racine si pas trouvé
    if (access(manifest_path, F_OK) != 0) {
        snprintf(manifest_path, sizeof(manifest_path), "%s/Manifest.toml", extract_dir);
    }
    
    int result = -1;
    if (access(manifest_path, F_OK) == 0) {
        debug_print("Found manifest at %s", manifest_path);
        result = parse_manifest(manifest_path, manifest);
    } else {
        debug_print("No manifest found in archive");
    }
    
    // Nettoyer
    snprintf(cmd, sizeof(cmd), "rm -rf %s", extract_dir);
    system(cmd);
    
    return result;
}

// ============================================================================
// UPLOAD DU PACKAGE
// ============================================================================

static int zarch_upload_package(const char *filepath, manifest_t *manifest) {
    char *token = load_token();
    if (!token) {
        print_error("Not authenticated. Please run 'apsm login' first");
        return -1;
    }
    
    struct stat file_stat;
    if (stat(filepath, &file_stat) != 0) {
        print_error("File not found: %s", filepath);
        return -1;
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/upload/public/%s", ZARCH_API_URL, manifest->name);
    
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
                 CURLFORM_COPYCONTENTS, manifest->version,
                 CURLFORM_END);
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "release",
                 CURLFORM_COPYCONTENTS, manifest->release,
                 CURLFORM_END);
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "arch",
                 CURLFORM_COPYCONTENTS, manifest->arch,
                 CURLFORM_END);
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "description",
                 CURLFORM_COPYCONTENTS, manifest->description,
                 CURLFORM_END);
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "license",
                 CURLFORM_COPYCONTENTS, manifest->license,
                 CURLFORM_END);
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "maintainer",
                 CURLFORM_COPYCONTENTS, manifest->maintainer,
                 CURLFORM_END);
    
    curl_formadd(&formpost, &lastptr,
                 CURLFORM_COPYNAME, "dependencies",
                 CURLFORM_COPYCONTENTS, manifest->dependencies,
                 CURLFORM_END);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    
    printf("\n");
    print_info("Uploading package...");
    print_info("  Name:    %s", manifest->name);
    print_info("  Version: %s-%s", manifest->version, manifest->release);
    print_info("  Arch:    %s", manifest->arch);
    print_info("  Size:    %.2f KB", file_stat.st_size / 1024.0);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_formfree(formpost);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        print_error("Upload failed: %s", curl_easy_strerror(res));
        return -1;
    }
    
    if (http_code == 200) {
        print_success("Upload complete!");
        return 0;
    } else {
        print_error("Upload failed with HTTP %ld", http_code);
        return -1;
    }
}

// ============================================================================
// VALIDATION DU MANIFEST
// ============================================================================

int validate_manifest(manifest_t *manifest) {
    int errors = 0;
    
    if (strlen(manifest->name) == 0) {
        print_error("Missing 'name' in manifest");
        errors++;
    }
    if (strlen(manifest->version) == 0) {
        print_error("Missing 'version' in manifest");
        errors++;
    }
    if (strlen(manifest->release) == 0) {
        strcpy(manifest->release, "r0");
        print_warning("Using default release: r0");
    }
    if (strlen(manifest->arch) == 0) {
        strcpy(manifest->arch, "x86_64");
        print_warning("Using default arch: x86_64");
    }
    
    return errors == 0 ? 0 : -1;
}

// ============================================================================
// COMMANDE DE PUBLICATION
// ============================================================================

static int publish_package(const char *filepath) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║     APSM - Zarch Hub Publisher v2.0             ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
    
    if (access(filepath, F_OK) != 0) {
        print_error("File not found: %s", filepath);
        return -1;
    }
    
    // Vérifier l'extension
    if (!strstr(filepath, ".tar.bool")) {
        print_error("Not a .tar.bool package");
        return -1;
    }
    
    print_step("Analyzing package");
    
    // Extraire et lire le manifest
    manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    
    int manifest_result = extract_manifest(filepath, &manifest);
    
    if (manifest_result == 0) {
        print_success("Found Manifest.toml");
        
        // Afficher les infos du manifest
        printf("\n");
        printf("📦 Package Information:\n");
        printf("  Name:        %s\n", manifest.name);
        printf("  Version:     %s-%s\n", manifest.version, manifest.release);
        printf("  Arch:        %s\n", manifest.arch);
        printf("  Description: %s\n", manifest.description);
        printf("  Maintainer:  %s\n", manifest.maintainer);
        printf("  License:     %s\n", manifest.license);
        
        if (strlen(manifest.dependencies) > 0) {
            printf("  Dependencies: %s\n", manifest.dependencies);
        }
        
        // Valider le manifest
        if (validate_manifest(&manifest) != 0) {
            print_error("Invalid manifest");
            return -1;
        }
        
    } else {
        // Fallback sur l'ancien parsing
        print_warning("No Manifest.toml found, using legacy parsing");
        
        char name[256] = "", version[64] = "1.0.0", release[16] = "r0", arch[32] = "x86_64";
        
        // Parser le nom du fichier (format: name-version-release.arch.tar.bool)
        char temp[512];
        strcpy(temp, basename((char*)filepath));
        char *ext = strstr(temp, ".tar.bool");
        if (ext) *ext = 0;
        
        char *last_dash = strrchr(temp, '-');
        if (last_dash) {
            *last_dash = 0;
            strcpy(version, last_dash + 1);
            strcpy(name, temp);
        } else {
            strcpy(name, temp);
        }
        
        strcpy(manifest.name, name);
        strcpy(manifest.version, version);
        strcpy(manifest.release, release);
        strcpy(manifest.arch, arch);
        
        printf("\n");
        printf("📦 Package Information (from filename):\n");
        printf("  Name:    %s\n", manifest.name);
        printf("  Version: %s-%s\n", manifest.version, manifest.release);
        printf("  Arch:    %s\n", manifest.arch);
    }
    
    // Demander confirmation
    printf("\n");
    printf("Proceed with upload? [Y/n] ");
    fflush(stdout);
    
    char response = getchar();
    if (response == 'n' || response == 'N') {
        print_info("Upload cancelled");
        return 0;
    }
    
    return zarch_upload_package(filepath, &manifest);
}

// ============================================================================
// COMMANDE DE LOGIN
// ============================================================================

static int cmd_login(void) {
    char username[256];
    char password[256];
    char token[512];
    
    printf("\n");
    printf("🔐 Zarch Hub Login\n");
    printf("──────────────────\n\n");
    
    printf("Username: ");
    fflush(stdout);
    if (!fgets(username, sizeof(username), stdin)) return -1;
    username[strcspn(username, "\n")] = 0;
    
    printf("Password: ");
    fflush(stdout);
    
    // Désactiver l'écho
    system("stty -echo");
    if (!fgets(password, sizeof(password), stdin)) {
        system("stty echo");
        return -1;
    }
    system("stty echo");
    printf("\n");
    
    password[strcspn(password, "\n")] = 0;
    
    int result = zarch_login(username, password, token, sizeof(token));
    if (result == 0) {
        save_token(token);
        print_success("Token saved");
    }
    
    return result;
}

// ============================================================================
// COMMANDE DE STATUS
// ============================================================================

static int cmd_status(void) {
    char *token = load_token();
    if (token) {
        print_success("Authenticated");
        debug_print("Token: %s", token);
        return 0;
    } else {
        print_error("Not authenticated");
        print_info("Run 'apsm login' to authenticate");
        return -1;
    }
}

// ============================================================================
// COMMANDE DE LOGOUT
// ============================================================================

static int cmd_logout(void) {
    if (unlink(TOKEN_PATH) == 0) {
        print_success("Logged out successfully");
        return 0;
    } else {
        print_error("Not authenticated");
        return -1;
    }
}

// ============================================================================
// COMMANDE DE VERIFICATION DE MANIFEST
// ============================================================================

static int cmd_verify_manifest(const char *path) {
    if (access(path, F_OK) != 0) {
        print_error("File not found: %s", path);
        return -1;
    }
    
    manifest_t manifest;
    if (parse_manifest(path, &manifest) != 0) {
        print_error("Failed to parse manifest");
        return -1;
    }
    
    printf("\n");
    printf("📦 Manifest verification\n");
    printf("────────────────────────\n");
    
    if (strlen(manifest.name) > 0)
        printf("✅ name: %s\n", manifest.name);
    else
        printf("❌ name: missing\n");
    
    if (strlen(manifest.version) > 0)
        printf("✅ version: %s\n", manifest.version);
    else
        printf("❌ version: missing\n");
    
    if (strlen(manifest.release) > 0)
        printf("✅ release: %s\n", manifest.release);
    else
        printf("⚠️  release: using default (r0)\n");
    
    if (strlen(manifest.arch) > 0)
        printf("✅ arch: %s\n", manifest.arch);
    else
        printf("⚠️  arch: using default (x86_64)\n");
    
    if (strlen(manifest.description) > 0)
        printf("✅ description: %s\n", manifest.description);
    
    if (strlen(manifest.maintainer) > 0)
        printf("✅ maintainer: %s\n", manifest.maintainer);
    
    if (strlen(manifest.license) > 0)
        printf("✅ license: %s\n", manifest.license);
    
    printf("\n");
    
    if (validate_manifest(&manifest) == 0) {
        print_success("Manifest is valid");
        return 0;
    } else {
        print_error("Manifest is invalid");
        return -1;
    }
}

// ============================================================================
// COMMANDE D'EXTRACTION DE MANIFEST
// ============================================================================

static int cmd_extract_manifest(const char *archive_path) {
    manifest_t manifest;
    if (extract_manifest(archive_path, &manifest) != 0) {
        print_error("No manifest found in archive");
        return -1;
    }
    
    printf("\n");
    printf("📦 Manifest extracted from %s\n", archive_path);
    printf("────────────────────────────────\n");
    printf("Name:        %s\n", manifest.name);
    printf("Version:     %s-%s\n", manifest.version, manifest.release);
    printf("Arch:        %s\n", manifest.arch);
    printf("Description: %s\n", manifest.description);
    printf("Maintainer:  %s\n", manifest.maintainer);
    printf("License:     %s\n", manifest.license);
    
    if (strlen(manifest.dependencies) > 0) {
        printf("\nDependencies:\n");
        char *deps = manifest.dependencies;
        char *dep = strtok(deps, ";");
        while (dep) {
            printf("  • %s\n", dep);
            dep = strtok(NULL, ";");
        }
    }
    
    return 0;
}

// ============================================================================
// AIDE
// ============================================================================

void print_help(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║     APSM - Zarch Hub Publisher v2.0             ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
    
    printf("USAGE:\n");
    printf("  apsm <command> [arguments]\n\n");
    
    printf("COMMANDS:\n");
    printf("  push <file>             Publish package to Zarch Hub\n");
    printf("  login                    Authenticate to Zarch Hub\n");
    printf("  status                   Check authentication status\n");
    printf("  logout                   Remove saved token\n");
    printf("  manifest verify <file>   Validate Manifest.toml\n");
    printf("  manifest extract <file>  Extract manifest from archive\n");
    printf("  help                     Show this help\n\n");
    
    printf("EXAMPLES:\n");
    printf("  apsm login\n");
    printf("  apsm push build/mypkg-v1.0.0-r1.x86_64.tar.bool\n");
    printf("  apsm manifest verify Manifest.toml\n");
    printf("  apsm manifest extract mypkg.tar.bool\n\n");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }
    
    // Initialisation
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/tokens", 0755);
    
    // Parser les options globales
    int args_processed = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
            args_processed++;
        }
        else if (strcmp(argv[i], "--quiet") == 0) {
            quiet_mode = 1;
            args_processed++;
        }
        else {
            break;
        }
    }
    
    if (args_processed > 1) {
        argv += args_processed - 1;
        argc -= args_processed - 1;
    }
    
    // Initialisation globale
    curl_global_init(CURL_GLOBAL_ALL);
    
    int result = 0;
    
    if (strcmp(argv[1], "push") == 0) {
        if (argc < 3) {
            print_error("Missing file");
            result = 1;
        } else {
            result = publish_package(argv[2]);
        }
    }
    else if (strcmp(argv[1], "login") == 0) {
        result = cmd_login();
    }
    else if (strcmp(argv[1], "status") == 0) {
        result = cmd_status();
    }
    else if (strcmp(argv[1], "logout") == 0) {
        result = cmd_logout();
    }
    else if (strcmp(argv[1], "manifest") == 0 && argc >= 3) {
        if (strcmp(argv[2], "verify") == 0 && argc >= 4) {
            result = cmd_verify_manifest(argv[3]);
        }
        else if (strcmp(argv[2], "extract") == 0 && argc >= 4) {
            result = cmd_extract_manifest(argv[3]);
        }
        else {
            print_error("Unknown manifest command");
            result = 1;
        }
    }
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
    }
    else {
        print_error("Unknown command: %s", argv[1]);
        print_help();
        result = 1;
    }
    
    curl_global_cleanup();
    return result;
}
