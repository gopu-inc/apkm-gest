#include "apkm.h"
#include "security.h"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

// Initialisation de la sécurité
int security_init(void) {
    // Créer les répertoires un par un
    mkdir("/usr/local/share/apkm", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/keys", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/tokens", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/signatures", 0755);
    
    // Vérifier si le token existe
    if (access(TOKEN_PATH, F_OK) != 0) {
        security_download_token();
    }
    
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

// Télécharger le token depuis GitHub
int security_download_token(void) {
    char url[512];
    snprintf(url, sizeof(url), "%s/.config.cfg", REPO_RAW);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "wget -q -O %s %s 2>/dev/null || curl -s -o %s %s", 
             TOKEN_PATH, url, TOKEN_PATH, url);
    
    if (system(cmd) == 0 && access(TOKEN_PATH, F_OK) == 0) {
        chmod(TOKEN_PATH, 0600);
        return 0;
    }
    return -1;
}

// Charger le token (déchiffré)
int security_load_token(security_token_t *token) {
    memset(token, 0, sizeof(security_token_t));
    
    FILE *f = fopen(TOKEN_PATH, "r");
    if (!f) {
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
            
            // Déchiffrer le token
            btscrypt_process(token->token, 0);
            
            token->last_update = time(NULL);
            token->validated = 1;
            break;
        }
    }
    fclose(f);
    
    return strlen(token->token) > 0 ? 0 : -1;
}

// Sauvegarder le token (chiffré)
int security_save_token(const security_token_t *token) {
    mkdir(SECURITY_PATH "/tokens", 0755);
    
    char encrypted_token[512];
    strncpy(encrypted_token, token->token, sizeof(encrypted_token) - 1);
    encrypted_token[sizeof(encrypted_token) - 1] = '\0';
    
    // Chiffrer
    btscrypt_process(encrypted_token, 1);
    
    FILE *f = fopen(TOKEN_PATH, "w");
    if (!f) return -1;
    
    fprintf(f, "TOKEN=%s\n", encrypted_token);
    fprintf(f, "# Generated: %lld\n", (long long)token->last_update);
    fclose(f);
    
    chmod(TOKEN_PATH, 0600);
    return 0;
}
