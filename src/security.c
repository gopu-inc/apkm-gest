#include "apkm.h"
#include "security.h"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int security_init(void) {
    // Créer les répertoires un par un
    mkdir("/usr/local/share/apkm", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/keys", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/tokens", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/signatures", 0755);
    return 0;
}

int security_load_token(security_token_t *token) {
    memset(token, 0, sizeof(security_token_t));
    
    FILE *f = fopen(TOKEN_PATH, "r");
    if (!f) return -1;
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *ptr = strstr(line, "TOKEN=");
        if (ptr) {
            strncpy(token->token, ptr + 6, sizeof(token->token) - 1);
            token->token[sizeof(token->token) - 1] = '\0';
            // Enlever le retour à la ligne
            size_t len = strlen(token->token);
            if (len > 0 && (token->token[len-1] == '\n' || token->token[len-1] == '\r')) {
                token->token[len-1] = '\0';
            }
            btscrypt_process(token->token, 0);
            token->last_update = time(NULL);
            token->validated = 1;
            break;
        }
    }
    fclose(f);
    return (strlen(token->token) > 0) ? 0 : -1;
}

int security_get_token(char *token_buffer, size_t buffer_size) {
    security_token_t token;
    if (security_load_token(&token) != 0) {
        return -1;
    }
    strncpy(token_buffer, token.token, buffer_size - 1);
    token_buffer[buffer_size - 1] = '\0';
    return 0;
}

int security_save_token(const security_token_t *token) {
    mkdir(SECURITY_PATH "/tokens", 0755);
    
    char encrypted_token[512];
    strncpy(encrypted_token, token->token, sizeof(encrypted_token) - 1);
    encrypted_token[sizeof(encrypted_token) - 1] = '\0';
    btscrypt_process(encrypted_token, 1);
    
    FILE *f = fopen(TOKEN_PATH, "w");
    if (!f) return -1;
    
    fprintf(f, "TOKEN=%s\n", encrypted_token);
    fprintf(f, "# Generated: %lld\n", (long long)token->last_update);
    fclose(f);
    chmod(TOKEN_PATH, 0600);
    return 0;
}

int security_download_token(void) {
    char url[512];
    snprintf(url, sizeof(url), "%s/.config.cfg", GITHUB_RAW_URL);
    
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

void btscrypt_process(char *data, int encrypt) {
    (void)encrypt; // Pour éviter le warning
    // Implémentation réelle à faire si nécessaire
    // Pour l'instant, ne rien faire
}