#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "apkm.h"

void push_package_auth(const char *filepath) {
    CURL *curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        char auth_header[300];
        char *token = get_gopu_token();

        if (!token) {
            fprintf(stderr, "[APSM] ❌ Erreur : Token introuvable.\n");
            return;
        }

        snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);
        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers, "User-Agent: APSM-Client-Gopu");

        // Utilisation de filepath ici pour uploader le bon fichier
        printf("[APSM] 🚀 Envoi de %s vers GitHub...\n", filepath);
        
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/gopu-inc/apkm-gest/contents/");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        // ... Logique d'upload ...

        free(token);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "push") == 0) {
        // On cible le build généré par bool
        push_package_auth("build/test-pkg-v1.2.0-r0.tar.bool");
    } else {
        printf("APSM - Gopu Registry Pusher\nUsage: ./apsm push\n");
    }
    return 0;
}

