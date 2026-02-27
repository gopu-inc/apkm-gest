#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include "apkm.h"

// Cette fonction utilise btscrypt_process qui est dans auth.c
void generate_config(const char *raw_token) {
    char config_path[512];
    char encrypted_token[256];
    
    get_config_path(config_path, sizeof(config_path));
    strncpy(encrypted_token, raw_token, 255);
    
    btscrypt_process(encrypted_token, 1); // Chiffrement

    FILE *f = fopen(config_path, "w");
    if (!f) {
        perror("[APSM] ❌ Erreur config");
        return;
    }
    fprintf(f, "TOKEN=%s\n", encrypted_token);
    fclose(f);
    printf("[APSM] 🔐 Configuration sécurisée dans : %s\n", config_path);
}

void push_to_github(const char *filepath) {
    char *token = load_token_from_home(); // Appelle la version de auth.c
    if (!token) {
        printf("[APSM] ❌ Erreur : Aucun token. Lancez 'apsm auth [token]'.\n");
        return;
    }

    CURL *curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);
        headers = curl_slist_append(headers, auth_header);
        
        printf("[APSM] 🚀 Publication de %s...\n", filepath);
        // ... Logique CURL ...

        free(token);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("APSM - Gopu.inc\nUsage: ./apsm auth [token] ou ./apsm push\n");
        return 1;
    }
    if (strcmp(argv[1], "auth") == 0 && argc == 3) {
        generate_config(argv[2]);
    } else if (strcmp(argv[1], "push") == 0) {
        push_to_github("build/test-pkg-v1.2.0-r0.tar.bool");
    }
    return 0;
}

