#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
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

// Vérifier si le token est valide
int check_token_valid(security_token_t *token) {
    if (security_load_token(token) != 0) {
        printf("[APSM] ❌ Token non trouvé. Téléchargement...\n");
        if (security_download_token() != 0) {
            printf("[APSM] ❌ Impossible d'obtenir le token\n");
            return -1;
        }
        security_load_token(token);
    }
    
    printf("[APSM] 🔐 Token chargé (%.10s...)\n", token->token);
    return 0;
}

// Publier sur GitHub
int publish_to_github(const char *filepath, security_token_t *token) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    // Vérifier si le paquet existe déjà
    char sha256[128];
    calculate_sha256(filepath, sha256);
    
    printf("[APSM] 🔍 Vérification du paquet...\n");
    
    // Extraire le nom et version
    char pkg_name[256] = "unknown";
    char pkg_version[64] = "0.0.0";
    
    char *basename = strrchr(filepath, '/');
    if (basename) basename++; else basename = (char*)filepath;
    
    char *version_start = strstr(basename, "-v");
    if (version_start) {
        int name_len = version_start - basename;
        strncpy(pkg_name, basename, name_len);
        pkg_name[name_len] = '\0';
        
        char *ext_start = strstr(version_start + 2, ".tar.bool");
        if (ext_start) {
            int ver_len = ext_start - (version_start + 2);
            strncpy(pkg_version, version_start + 2, ver_len);
            pkg_version[ver_len] = '\0';
        }
    }
    
    // Vérifier les doublons
    if (security_check_duplicate(pkg_name, pkg_version)) {
        printf("[APSM] ❌ Le paquet %s version %s existe déjà!\n", 
               pkg_name, pkg_version);
        return -1;
    }
    
    // Préparer l'upload
    struct curl_response resp = {0};
    struct curl_slist *headers = NULL;
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token->token);
    
    // Uploader le fichier
    char url[512];
    snprintf(url, sizeof(url), 
             "https://uploads.github.com/repos/gopu-inc/apkm-gest/releases/1/assets?name=%s",
             basename);
    
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    
    FILE *file = fopen(filepath, "rb");
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_READDATA, file);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)file_size);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    printf("[APSM] 📤 Publication de %s (%.2f MB)...\n", 
           basename, file_size / (1024.0 * 1024.0));
    
    CURLcode res = curl_easy_perform(curl);
    fclose(file);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[APSM] ❌ Erreur upload: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(resp.data);
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (http_code != 201) {
        fprintf(stderr, "[APSM] ❌ Erreur GitHub (HTTP %ld)\n", http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(resp.data);
        return -1;
    }
    
    printf("[APSM] ✅ Fichier publié avec succès!\n");
    
    // Mettre à jour le metadata
    package_metadata_t metadata;
    strcpy(metadata.name, pkg_name);
    strcpy(metadata.version, pkg_version);
    strcpy(metadata.sha256, sha256);
    metadata.timestamp = time(NULL);
    strcpy(metadata.publisher, "apkm-bot");
    
    security_update_metadata(&metadata);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(resp.data);
    
    return 0;
}

// Commande d'authentification
int auth_command(const char *raw_token) {
    security_token_t token;
    strncpy(token.token, raw_token, sizeof(token.token) - 1);
    token.last_update = time(NULL);
    token.validated = 1;
    
    if (security_save_token(&token) == 0) {
        printf("[APSM] 🔐 Token sauvegardé sécurisé dans %s\n", TOKEN_PATH);
        return 0;
    }
    
    printf("[APSM] ❌ Erreur lors de la sauvegarde\n");
    return -1;
}

// Commande de push
int push_command(const char *filepath) {
    security_token_t token;
    
    if (check_token_valid(&token) != 0) {
        printf("[APSM] ❌ Authentification requise. Lancez 'apsm auth [token]'\n");
        return -1;
    }
    
    if (!filepath) {
        filepath = "build/test-pkg-v1.2.0-r0.tar.bool";
    }
    
    if (access(filepath, F_OK) != 0) {
        printf("[APSM] ❌ Fichier introuvable: %s\n", filepath);
        return -1;
    }
    
    return publish_to_github(filepath, &token);
}

// Commande de statut
int status_command(void) {
    security_token_t token;
    
    if (security_load_token(&token) == 0) {
        printf("[APSM] ✅ Authentifié (token: %.10s...)\n", token.token);
        printf("[APSM] 📁 Token: %s\n", TOKEN_PATH);
        
        // Vérifier la validité
        time_t now = time(NULL);
        if (now - token.last_update < 86400) { // Moins de 24h
            printf("[APSM] ✅ Token récent\n");
        } else {
            printf("[APSM] ⚠️ Token ancien (>24h)\n");
        }
    } else {
        printf("[APSM] ❌ Non authentifié\n");
        printf("[APSM] 👉 Utilisez 'apsm auth <token>' pour configurer\n");
    }
    
    return 0;
}

// Commande de synchronisation du token
int sync_command(void) {
    printf("[APSM] 🔄 Synchronisation du token...\n");
    
    if (security_download_token() == 0) {
        printf("[APSM] ✅ Token synchronisé depuis GitHub\n");
        return 0;
    }
    
    printf("[APSM] ❌ Échec de la synchronisation\n");
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  APSM - GitHub Publisher for APKM\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
        printf("Usage:\n");
        printf("  apsm auth <token>     Configurer le token GitHub\n");
        printf("  apsm push [fichier]   Publier un paquet\n");
        printf("  apsm status            Vérifier l'authentification\n");
        printf("  apsm sync              Synchroniser le token\n");
        printf("  apsm list              Lister les paquets publiés\n\n");
        return 1;
    }
    
    // Initialiser la sécurité
    security_init();
    
    if (strcmp(argv[1], "auth") == 0) {
        if (argc < 3) {
            printf("[APSM] ❌ Token manquant\n");
            return 1;
        }
        return auth_command(argv[2]);
    }
    else if (strcmp(argv[1], "push") == 0) {
        return push_command(argc > 2 ? argv[2] : NULL);
    }
    else if (strcmp(argv[1], "status") == 0) {
        return status_command();
    }
    else if (strcmp(argv[1], "sync") == 0) {
        return sync_command();
    }
    else if (strcmp(argv[1], "list") == 0) {
        printf("[APSM] 📋 Liste des paquets publiés:\n");
        system("curl -s https://raw.githubusercontent.com/gopu-inc/apkm-gest/master/DATA.db 2>/dev/null | column -t -s '|'");
    }
    else {
        printf("[APSM] ❌ Commande inconnue: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}
