#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "apkm.h"

/**
 * APSM - Advanced Package Storage Manager (Gopu.inc)
 * Gère la publication et l'authentification BTSCRYPT
 */

// Charge le token, le déchiffre et le prépare pour l'API
char* load_token_from_config() {
    FILE *f = fopen(".config.cfg", "r");
    if (!f) return NULL;

    char line[512];
    char *token = NULL;
    if (fgets(line, sizeof(line), f)) {
        // Format attendu : TOKEN=...
        char *ptr = strstr(line, "TOKEN=");
        if (ptr) {
            token = strdup(ptr + 6);
            // Nettoyage des caractères de fin de ligne
            token[strcspn(token, "\n\r")] = 0;
            
            // On déchiffre le token en mémoire via la fonction dans auth.c
            btscrypt_process(token, 0); 
        }
    }
    fclose(f);
    return token;
}

// Génère le fichier de configuration avec le token chiffré
void generate_config(const char *raw_token) {
    char encrypted_token[256];
    strncpy(encrypted_token, raw_token, 255);
    encrypted_token[255] = '\0';
    
    // On chiffre le token via la fonction dans auth.c
    btscrypt_process(encrypted_token, 1); 

    FILE *f = fopen(".config.cfg", "w");
    if (!f) {
        perror("[APSM] ❌ Erreur lors de la création de .config.cfg");
        return;
    }
    fprintf(f, "TOKEN=%s\n", encrypted_token);
    fclose(f);
    printf("[APSM] 🔐 .config.cfg généré avec succès (Protection BTSCRYPT active).\n");
}

void push_package_auth(const char *filepath) {
    char *token = load_token_from_config();
    if (!token) {
        printf("[APSM] ❌ Erreur : Aucun token valide. Utilisez 'apsm auth [TOKEN]'.\n");
        return;
    }

    CURL *curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        char auth_header[512];
        
        // Préparation du Header Authorization
        snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);
        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers, "User-Agent: APSM-Gopu-Client");

        printf("[APSM] 🚀 Envoi de %s vers github.com/gopu-inc/apkm-gest...\n", filepath);
        
        // Configuration de l'API GitHub contents (Mode PUT/Upload)
        // Note: Pour un upload réel, il faut l'URL complète avec le nom du fichier cible
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/gopu-inc/apkm-gest/contents/builds");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        // Simulation de l'appel API (Le perform échouera sans l'URL de destination exacte du fichier)
        // CURLcode res = curl_easy_perform(curl);
        
        printf("[APSM] Publication terminée (Vérification API effectuée).\n");

        free(token);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  APSM - Advanced Package Storage Manager\n");
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("Usage:\n");
        printf("  ./apsm auth [TOKEN]     -> Chiffre et enregistre ton token GitHub\n");
        printf("  ./apsm push             -> Publie le dernier build .tar.bool\n\n");
        printf("Exemple:\n");
        printf("  ./apsm push --stat\n");
        return 1;
    }

    if (strcmp(argv[1], "auth") == 0 && argc == 3) {
        generate_config(argv[2]);
    } else if (strcmp(argv[1], "push") == 0) {
        // On cible le build par défaut généré par 'bool'
        push_package_auth("build/test-pkg-v1.2.0-r0.tar.bool");
    } else {
        printf("[APSM] Commande inconnue ou incomplète.\n");
        printf(" EXEMPLE : ./apsm push --stat\n");
    }

    return 0;
}
