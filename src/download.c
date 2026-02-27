// src/download.c corrigé
#include "apkm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Structure pour écrire le fichier téléchargé
struct FileData {
    FILE *fp;
};

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream) {
    struct FileData *out = (struct FileData *)stream;
    return fwrite(ptr, size, nmemb, out->fp);
}

void download_package(const char *pkg_name) {
    CURL *curl;
    CURLcode res;
    char url[512];
    char output_file[256];
    char auth_header[512];
    struct curl_slist *headers = NULL;

    // 1. Charger le token déchiffré
    extern char* load_token_from_home();  // Déclaration externe
    char *token = load_token_from_home();

    // 2. Préparer le chemin de sortie
    snprintf(output_file, sizeof(output_file), "/tmp/%s.tar.bool", pkg_name);
    
    // URL
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/gopu-inc/apkm-gest/main/build/%s.tar.bool", pkg_name);

    curl = curl_easy_init();
    if (curl) {
        struct FileData file_data;
        file_data.fp = fopen(output_file, "wb");
        if (!file_data.fp) {
            perror("[APKM] Erreur d'ouverture du fichier");
            if (token) free(token);
            return;
        }

        if (token) {
            snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", token);
            headers = curl_slist_append(headers, auth_header);
            printf("[APKM] Authentification active.\n");
        }

        headers = curl_slist_append(headers, "User-Agent: APKM-Installer");

        printf("[APKM] Récupération de %s...\n", pkg_name);
        
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file_data);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        res = curl_easy_perform(curl);
        
        fclose(file_data.fp);

        if (res != CURLE_OK) {
            fprintf(stderr, "[APKM] Échec du téléchargement: %s\n", curl_easy_strerror(res));
            remove(output_file);
        } else {
            printf("[APKM] Paquet téléchargé: %s\n", output_file);
        }

        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (token) free(token);
    }
}
