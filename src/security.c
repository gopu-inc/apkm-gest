#include "apkm.h"
#include "security.h"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <sys/stat.h>

// Initialisation de la sécurité
int security_init(void) {
    printf("[SECURITY] 🔐 Initialisation du système de sécurité...\n");
    
    // Créer les répertoires un par un
    mkdir("/usr/local/share/apkm", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/keys", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/tokens", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/signatures", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/security/cache", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/repository", 0755);
    mkdir("/usr/local/share/apkm/PROTOCOLE/metadata", 0755);
    
    // Vérifier si le token existe, sinon le télécharger
    if (access(TOKEN_PATH, F_OK) != 0) {
        security_download_token();
    }
    
    printf("[SECURITY] ✅ Système de sécurité initialisé\n");
    return 0;
}

// Calculer SHA256 d'un fichier
int calculate_sha256(const char *filepath, char *output) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        printf("[SECURITY] ❌ Impossible d'ouvrir %s\n", filepath);
        return -1;
    }
    
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
    printf("[SECURITY] 🔒 SHA256: %.16s...\n", output);
    return 0;
}

// Télécharger le token depuis GitHub
int security_download_token(void) {
    printf("[SECURITY] 📥 Téléchargement du token de sécurité...\n");
    
    char url[512];
    snprintf(url, sizeof(url), "%s/.config.cfg", REPO_URL);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "wget -q -O %s %s 2>/dev/null || curl -s -o %s %s", 
             TOKEN_PATH, url, TOKEN_PATH, url);
    
    if (system(cmd) == 0 && access(TOKEN_PATH, F_OK) == 0) {
        chmod(TOKEN_PATH, 0600);
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
            break;
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

// Vérifier si un paquet existe déjà
int security_check_duplicate(const char *package_name, const char *version) {
    char url[512];
    snprintf(url, sizeof(url), 
             "https://raw.githubusercontent.com/gopu-inc/apkm-gest/master/%s", 
             METADATA_FILE);
    
    // Télécharger le metadata
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "curl -s %s > /tmp/DATA.db 2>/dev/null || wget -q -O /tmp/DATA.db %s",
             url, url);
    system(cmd);
    
    // Lire le fichier
    FILE *f = fopen("/tmp/DATA.db", "r");
    if (!f) return 0;
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char name[256], ver[64];
        if (sscanf(line, "%[^|]|%[^|]", name, ver) == 2) {
            if (strcmp(name, package_name) == 0 && strcmp(ver, version) == 0) {
                fclose(f);
                unlink("/tmp/DATA.db");
                return 1; // Déjà existant
            }
        }
    }
    
    fclose(f);
    unlink("/tmp/DATA.db");
    return 0; // Pas de doublon
}

// Mettre à jour le fichier metadata
int security_update_metadata(const package_metadata_t *metadata) {
    // Télécharger le metadata actuel
    char url[512];
    snprintf(url, sizeof(url), 
             "https://raw.githubusercontent.com/gopu-inc/apkm-gest/master/%s",
             METADATA_FILE);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "curl -s %s > /tmp/DATA.current 2>/dev/null", url);
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
    fprintf(out, "%s|%s|%s|%lld|%s\n", 
            metadata->name, metadata->version, 
            metadata->sha256, (long long)metadata->timestamp,
            metadata->publisher);
    
    fclose(out);
    
    // Ici, il faudrait uploader le fichier sur GitHub avec le token
    printf("[SECURITY] 📝 Metadata mis à jour pour %s %s\n", 
           metadata->name, metadata->version);
    
    unlink("/tmp/DATA.current");
    return 0;
}
