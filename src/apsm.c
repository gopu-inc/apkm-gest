#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "apkm.h"

// Configuration BTSCRYPT interne
#define BTS_SALT 0x1B 

// Moteur de rotation de bits propriétaire
unsigned char bts_rotate_left(unsigned char val, int n) {
    return (val << n) | (val >> (8 - n));
}

unsigned char bts_rotate_right(unsigned char val, int n) {
    return (val >> n) | (val << (8 - n));
}

// Chiffrement/Déchiffrement BTSCRYPT
void btscrypt_process(char *data, int encrypt) {
    int len = strlen(data);
    for(int i = 0; i < len; i++) {
        if (encrypt) {
            // XOR puis Rotation Gauche
            data[i] = bts_rotate_left(data[i] ^ BTS_SALT, 3);
        } else {
            // Rotation Droite puis XOR
            data[i] = bts_rotate_right((unsigned char)data[i], 3) ^ BTS_SALT;
        }
    }
}

// Fonction pour initialiser le fichier .config.cfg (appelée manuellement)
void generate_config(const char *raw_token) {
    char encrypted_token[256];
    strncpy(encrypted_token, raw_token, 255);
    
    btscrypt_process(encrypted_token, 1); // Chiffrement

    FILE *f = fopen(".config.cfg", "w");
    if (!f) {
        perror("[APSM] Erreur création config");
        return;
    }
    fprintf(f, "TOKEN=%s\n", encrypted_token);
    fclose(f);
    printf("[APSM] 🔐 .config.cfg généré avec succès (BTSCRYPT actif).\n");
}

// Récupération du token pour l'envoi API
char* load_token_from_config() {
    FILE *f = fopen(".config.cfg", "r");
    if (!f) return NULL;

    char line[512];
    char *token = NULL;
    if (fgets(line, sizeof(line), f)) {
        char *ptr = strstr(line, "TOKEN=");
        if (ptr) {
            token = strdup(ptr + 6);
            token[strcspn(token, "\n\r")] = 0;
            btscrypt_process(token, 0); // Déchiffrement en mémoire
        }
    }
    fclose(f);
    return token;
}

void push_to_github(const char *filepath) {
    char *token = load_token_from_config();
    if (!token) {
        printf("[APSM] ❌ Erreur : Aucun token valide trouvé. Utilisez 'apsm auth [token]'.\n");
        return;
    }

    CURL *curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);

        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers, "User-Agent: APSM-Gopu-Client");

        printf("[APSM] 🚀 Publication de %s via API sécurisée...\n", filepath);
        
        // Configuration CURL (Upload binaire)
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/gopu-inc/apkm-gest/contents/");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        // ... Logique curl_easy_perform ...

        free(token);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("APSM - Advanced Package Storage Manager (Gopu.inc)\n");
        printf("Usage:\n  ./apsm auth [ghp_token]  -> Chiffre et crée .config.cfg\n");
        printf("  ./apsm push             -> Publie le paquet vers GitHub\n");



        


        print(" EXEMPLE                   apsm push --stat");


        return 1;
    }

    if (strcmp(argv[1], "auth") == 0 && argc == 3) {
        generate_config(argv[2]);
    } else if (strcmp(argv[1], "push") == 0) {
        push_to_github("build/test-pkg-v1.2.0-r0.tar.bool");
    } else {
        printf("[APSM] Commande non reconnue.\n");
    }

    return 0;
}
