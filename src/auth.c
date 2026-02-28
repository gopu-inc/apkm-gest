#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "apkm.h"

#define BTS_SALT 0x1B
#define APKM_ROOT "/usr/local/apkm"
#define TOKEN_PATH APKM_ROOT "/PROTOCOLE/SECURITY/SIGNATURBOOL/.config.cfg"

// Fonctions de rotation
static unsigned char bts_rotate_left(unsigned char val, int n) {
    return (unsigned char)((val << n) | (val >> (8 - n)));
}

static unsigned char bts_rotate_right(unsigned char val, int n) {
    return (unsigned char)((val >> n) | (val << (8 - n)));
}

// Créer toute la structure de dossiers
void create_apkm_structure(void) {
    // Créer chaque dossier un par un (pas de {} avec mkdir -p)
    mkdir(APKM_ROOT, 0755);
    mkdir(APKM_ROOT "/PROTOCOLE", 0755);
    mkdir(APKM_ROOT "/PROTOCOLE/SECURITY", 0750);
    mkdir(APKM_ROOT "/PROTOCOLE/SECURITY/SIGNATURBOOL", 0750);
    mkdir(APKM_ROOT "/PROTOCOLE/TOKENS", 0750);
    mkdir(APKM_ROOT "/PROTOCOLE/CONFIG", 0755);
    mkdir(APKM_ROOT "/PROTOCOLE/CACHE", 0755);
    mkdir(APKM_ROOT "/PROTOCOLE/LOGS", 0755);
    mkdir(APKM_ROOT "/SERVICE", 0755);
    mkdir(APKM_ROOT "/WEBHOOKS", 0755);
    mkdir(APKM_ROOT "/GITHUB", 0755);
    mkdir(APKM_ROOT "/GITHUB/REPOS", 0755);
    mkdir(APKM_ROOT "/GITHUB/REPOS/apkm-gest", 0755);
}

// Le moteur BTSCRYPT amélioré
void btscrypt_process(char *data, int encrypt) {
    if (!data) return;
    size_t len = strlen(data);
    for(size_t i = 0; i < len; i++) {
        if (encrypt) {
            data[i] = (char)bts_rotate_left((unsigned char)data[i] ^ BTS_SALT, 3);
        } else {
            data[i] = (char)(bts_rotate_right((unsigned char)data[i], 3) ^ BTS_SALT);
        }
    }
}

// Obtenir le chemin du token (toujours le même)
void get_token_path(char *path, size_t size) {
    strncpy(path, TOKEN_PATH, size);
    path[size - 1] = '\0';
}

// Sauvegarder le token de façon sécurisée
int save_token_secure(const char *raw_token) {
    create_apkm_structure();
    
    char token_path[512];
    get_token_path(token_path, sizeof(token_path));
    
    // Chiffrer le token
    char encrypted_token[1024];
    strncpy(encrypted_token, raw_token, sizeof(encrypted_token) - 1);
    encrypted_token[sizeof(encrypted_token) - 1] = '\0';
    
    btscrypt_process(encrypted_token, 1); // Chiffrement
    
    // Sauvegarder
    FILE *f = fopen(token_path, "w");
    if (!f) {
        perror("[APSM] Erreur création token");
        return -1;
    }
    
    fprintf(f, "APKM_TOKEN=%s\n", encrypted_token);
    fprintf(f, "# Protocole SECURITY/SIGNATURBOOL v1.0\n");
    fprintf(f, "# Ne pas modifier ce fichier\n");
    fclose(f);
    
    // Protéger le fichier
    chmod(token_path, 0640);
    
    printf("[APSM] 🔐 Token installé dans: %s\n", token_path);
    return 0;
}

// Charger le token depuis la structure sécurisée
char* load_token_from_secure(void) {
    char token_path[512];
    get_token_path(token_path, sizeof(token_path));
    
    FILE *f = fopen(token_path, "r");
    if (!f) return NULL;
    
    char line[1024];
    char *token = NULL;
    
    while (fgets(line, sizeof(line), f)) {
        char *ptr = strstr(line, "APKM_TOKEN=");
        if (ptr) {
            token = strdup(ptr + 11); // "APKM_TOKEN=" fait 11 chars
            token[strcspn(token, "\n\r")] = 0;
            btscrypt_process(token, 0); // Déchiffrement
            break;
        }
    }
    fclose(f);
    return token;
}

// Vérifier si le token est valide
int verify_token_exists(void) {
    char token_path[512];
    get_token_path(token_path, sizeof(token_path));
    return access(token_path, F_OK) == 0;
}

// Obtenir les infos du repository GitHub
void get_github_repo_path(char *path, size_t size, const char *repo) {
    snprintf(path, size, APKM_ROOT "/GITHUB/REPOS/%s", repo ? repo : "apkm-gest");
}

// Créer la structure pour un repo spécifique
int setup_github_repo(const char *repo_name) {
    char repo_path[512];
    snprintf(repo_path, sizeof(repo_path), APKM_ROOT "/GITHUB/REPOS/%s", repo_name);
    
    mkdir(repo_path, 0755);
    mkdir(APKM_ROOT "/GITHUB/REPOS", 0755);
    
    printf("[APSM] 📁 Repository configuré: %s\n", repo_path);
    return 0;
}
