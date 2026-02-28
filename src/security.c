#include "apkm.h"
#include "security.h"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <sys/stat.h>

// Télécharger le token depuis GitHub
int security_download_token(void) {
    printf("[SECURITY] 📥 Téléchargement du token de sécurité...\n");
    
    // Créer les répertoires
    mkdir(SECURITY_PATH, 0755);
    mkdir(SECURITY_PATH "/tokens", 0755);
    
    char url[512];
    snprintf(url, sizeof(url), "%s/.config.cfg", REPO_URL);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "wget -q -O %s %s 2>/dev/null || curl -s -o %s %s", 
             TOKEN_PATH, url, TOKEN_PATH, url);
    
    if (system(cmd) == 0 && access(TOKEN_PATH, F_OK) == 0) {
        printf("[SECURITY] ✅ Token téléchargé depuis GitHub\n");
        return 0;
    }
    
    printf("[SECURITY] ❌ Échec du téléchargement du token\n");
    return -1;
}

// Charger le token (déchiffré)
int security_load_token(security_token_t *token) {
    memset(token, 0, sizeof(security_token_t));
    
    FILE *f = fopen(TOKEN_PATH, "r");
    if (!f) {
        // Essayer de télécharger
        if (security_download_token() != 0) {
            return -1;
        }
        f = fopen(TOKEN_PATH, "r");
        if (!f) return -1;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *ptr = strstr(line, "TOKEN=");
        if (ptr) {
            strncpy(token->token, ptr + 6, sizeof(token->token) - 1);
            token->token[strcspn(token->token, "\n\r")] = 0;
            
            // Déchiffrer le token (btscrypt)
            btscrypt_process(token->token, 0);
            
            token->last_update = time(NULL);
            token->validated = 1;
        }
    }
    fclose(f);
    
    return strlen(token->token) > 0 ? 0 : -1;
}

// Sauvegarder le token (chiffré)
int security_save_token(const security_token_t *token) {
    mkdir(SECURITY_PATH "/tokens", 0755);
    
    // Copier le token pour le chiffrer
    char encrypted_token[512];
    strncpy(encrypted_token, token->token, sizeof(encrypted_token) - 1);
    
    // Chiffrer
    btscrypt_process(encrypted_token, 1);
    
    FILE *f = fopen(TOKEN_PATH, "w");
    if (!f) return -1;
    
    fprintf(f, "TOKEN=%s\n", encrypted_token);
    fprintf(f, "# Generated: %ld\n", token->last_update);
    fclose(f);
    
    chmod(TOKEN_PATH, 0600);  // Permissions strictes
    return 0;
}

// Calculer SHA256 d'un fichier
int calculate_sha256(const char *filepath, char *output) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;
    
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    
    unsigned char buffer[8192];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        SHA256_Update(&ctx, buffer, bytes);
    }
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[SHA256_DIGEST_LENGTH * 2] = '\0';
    
    fclose(f);
    return 0;
}

// Vérifier si un paquet existe déjà
int security_check_duplicate(const char *package_name, const char *version) {
    char url[512];
    snprintf(url, sizeof(url), 
             "https://api.github.com/repos/gopu-inc/apkm-gest/contents/%s", 
             METADATA_FILE);
    
    // Télécharger le metadata
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "curl -s -H 'Accept: application/vnd.github.v3.raw' %s > /tmp/DATA.db 2>/dev/null",
             url);
    
    if (system(cmd) != 0) {
        return 0; // Pas de metadata, on peut publier
    }
    
    // Lire le fichier
    FILE *f = fopen("/tmp/DATA.db", "r");
    if (!f) return 0;
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char name[256], ver[64];
        if (sscanf(line, "%[^|]|%[^|]", name, ver) == 2) {
            if (strcmp(name, package_name) == 0 && strcmp(ver, version) == 0) {
                fclose(f);
                return 1; // Déjà existant
            }
        }
    }
    
    fclose(f);
    return 0;
}

// Mettre à jour le fichier metadata
int security_update_metadata(const package_metadata_t *metadata) {
    // Télécharger le metadata actuel
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "curl -s -H 'Accept: application/vnd.github.v3.raw' "
             "https://api.github.com/repos/gopu-inc/apkm-gest/contents/%s > /tmp/DATA.current",
             METADATA_FILE);
    system(cmd);
    
    // Ajouter la nouvelle entrée
    FILE *in = fopen("/tmp/DATA.current", "r");
    FILE *out = fopen("/tmp/DATA.new", "w");
    
    if (!out) return -1;
    
    // Copier l'existant
    if (in) {
        char line[1024];
        while (fgets(line, sizeof(line), in)) {
            fputs(line, out);
        }
        fclose(in);
    }
    
    // Ajouter la nouvelle entrée
    fprintf(out, "%s|%s|%s|%ld|%s\n", 
            metadata->name, metadata->version, 
            metadata->sha256, metadata->timestamp,
            metadata->publisher);
    
    fclose(out);
    
    return 0;
}

// Signer un paquet avec GPG
int security_sign_package(const char *package_path, const char *key_path, char *signature) {
    char cmd[1024];
    char sig_file[512];
    snprintf(sig_file, sizeof(sig_file), "%s.sig", package_path);
    
    snprintf(cmd, sizeof(cmd), 
             "gpg --batch --yes --detach-sign --armor -u %s -o %s %s 2>/dev/null",
             key_path, sig_file, package_path);
    
    if (system(cmd) == 0) {
        FILE *f = fopen(sig_file, "r");
        if (f) {
            size_t len = fread(signature, 1, 4096, f);
            signature[len] = '\0';
            fclose(f);
            unlink(sig_file);
            return 0;
        }
    }
    
    return -1;
}
