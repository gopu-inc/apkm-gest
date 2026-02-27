#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include "apkm.h"

// Le chemin sera soit $HOME/.config.cfg soit /root/.config.cfg
void get_config_path(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        // Fallback si HOME n'est pas défini (rare sur Alpine)
        strncpy(path, "/root/.config.cfg", size);
    } else {
        snprintf(path, size, "%s/.config.cfg", home);
    }
}

// Fonction pour chiffrer et sauvegarder le token
void generate_config(const char *raw_token) {
    char config_path[512];
    char encrypted_token[256];
    
    get_config_path(config_path, sizeof(config_path));
    strncpy(encrypted_token, raw_token, 255);
    
    // Appel à BTSCRYPT (défini dans auth.c)
    btscrypt_process(encrypted_token, 1); 

    FILE *f = fopen(config_path, "w");
    if (!f) {
        perror("[APSM] ❌ Erreur fatale : Impossible de créer le fichier dans $HOME");
        return;
    }
    fprintf(f, "TOKEN=%s\n", encrypted_token);
    fclose(f);
    
    printf("[APSM] 🔐 Configuration sécurisée créée dans : %s\n", config_path);
}

// Charger et déchiffrer pour l'envoi
char* load_token_from_home() {
    char config_path[512];
    get_config_path(config_path, sizeof(config_path));

    FILE *f = fopen(config_path, "r");
    if (!f) return NULL;

    char line[512];
    char *token = NULL;
    if (fgets(line, sizeof(line), f)) {
        char *ptr = strstr(line, "TOKEN=");
        if (ptr) {
            token = strdup(ptr + 6);
            token[strcspn(token, "\n\r")] = 0;
            btscrypt_process(token, 0); // Déchiffrement BTSCRYPT
        }
    }
    fclose(f);
    return token;
}

void push_to_github(const char *filepath) {
    char *token = load_token_from_home();
    if (!token) {
        printf("[APSM] ❌ Erreur : Aucun token configuré. Lancez 'apsm auth [token]'.\n");
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
        
        // Configuration CURL
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
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  APSM - Advanced Package Storage Manager (Gopu.inc)\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("Usage:\n");
        printf("  apsm push              -> Publie le paquet .tar.bool\n\n");
        printf("Exemple:\n");
        return 1;
    }

    if (strcmp(argv[1], "auth") == 0 && argc == 3) {
        generate_config(argv[2]);
    } else if (strcmp(argv[1], "push") == 0) {
        // Chemin relatif au dossier de build généré par bool
        push_to_github("build/test-pkg-v1.2.0-r0.tar.bool");
    } else {
        printf("[APSM] Commande inconnue. Tapez './apsm' pour l'aide.\n");
    }

    return 0;
}
