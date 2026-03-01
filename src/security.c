#include "apkm.h"
#include "security.h"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

// Fonctions de rotation BTSCRYPT
static unsigned char bts_rotate_left(unsigned char val, int n) {
    return (unsigned char)((val << n) | (val >> (8 - n)));
}

static unsigned char bts_rotate_right(unsigned char val, int n) {
    return (unsigned char)((val >> n) | (val << (8 - n)));
}

// Implémentation complète de BTSCRYPT
void btscrypt_process(char *data, int encrypt) {
    if (!data) return;
    
    size_t len = strlen(data);
    for (size_t i = 0; i < len; i++) {
        if (encrypt) {
            // Chiffrement: rotation left après XOR
            data[i] = (char)bts_rotate_left((unsigned char)(data[i] ^ 0x1B), 3);
        } else {
            // Déchiffrement: XOR après rotation right
            data[i] = (char)(bts_rotate_right((unsigned char)data[i], 3) ^ 0x1B);
        }
    }
}

int security_init(void) {
    // Créer les répertoires avec les permissions appropriées
    mkdir("/usr/local/share/apkm", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/keys", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/tokens", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/signatures", 0755);
    
    return 0;
}

int security_load_token(security_token_t *token) {
    if (!token) return -1;
    
    memset(token, 0, sizeof(security_token_t));
    
    FILE *f = fopen(TOKEN_PATH, "r");
    if (!f) return -1;
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *ptr = strstr(line, "TOKEN=");
        if (ptr) {
            // Copier le token chiffré
            strncpy(token->token, ptr + 6, sizeof(token->token) - 1);
            token->token[sizeof(token->token) - 1] = '\0';
            
            // Nettoyer les retours à la ligne
            size_t len = strlen(token->token);
            while (len > 0 && (token->token[len-1] == '\n' || token->token[len-1] == '\r')) {
                token->token[len-1] = '\0';
                len--;
            }
            
            // Déchiffrer le token
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
    if (!token_buffer || buffer_size == 0) return -1;
    
    security_token_t token;
    if (security_load_token(&token) != 0) {
        return -1;
    }
    
    strncpy(token_buffer, token.token, buffer_size - 1);
    token_buffer[buffer_size - 1] = '\0';
    
    // Effacer de la mémoire locale
    memset(&token, 0, sizeof(token));
    
    return 0;
}

int security_save_token(const security_token_t *token) {
    if (!token) return -1;
    
    // Créer le répertoire si nécessaire
    mkdir(SECURITY_PATH "/tokens", 0755);
    
    // Copier le token pour le chiffrer
    char encrypted_token[512];
    strncpy(encrypted_token, token->token, sizeof(encrypted_token) - 1);
    encrypted_token[sizeof(encrypted_token) - 1] = '\0';
    
    // Chiffrer le token
    btscrypt_process(encrypted_token, 1);
    
    FILE *f = fopen(TOKEN_PATH, "w");
    if (!f) return -1;
    
    fprintf(f, "TOKEN=%s\n", encrypted_token);
    fprintf(f, "# Generated: %lld\n", (long long)token->last_update);
    fclose(f);
    
    // Permissions strictes (lecture seule pour le propriétaire)
    chmod(TOKEN_PATH, 0600);
    
    return 0;
}

int security_download_token(void) {
    char url[512];
    snprintf(url, sizeof(url), "%s/.config.cfg", GITHUB_RAW_URL);
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    FILE *fp = fopen(TOKEN_PATH, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM-Security/2.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    if (res != CURLE_OK) {
        unlink(TOKEN_PATH);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (http_code != 200) {
        unlink(TOKEN_PATH);
        return -1;
    }
    
    chmod(TOKEN_PATH, 0600);
    return 0;
}

int calculate_sha256(const char *filepath, char *output) {
    if (!filepath || !output) return -1;
    
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