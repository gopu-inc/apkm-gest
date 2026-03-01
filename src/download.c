#include "apkm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>

// ============================================================================
// STRUCTURES
// ============================================================================

struct FileData {
    FILE *fp;
};

struct DownloadProgress {
    double last_progress;
    char filename[256];
    time_t last_time;
    curl_off_t last_dlnow;
    double speed;
};

// ============================================================================
// CALLBACKS STATIC
// ============================================================================

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream) {
    struct FileData *out = (struct FileData *)stream;
    return fwrite(ptr, size, nmemb, out->fp);
}

static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, 
                              curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    
    struct DownloadProgress *prog = (struct DownloadProgress *)clientp;
    
    if (dltotal > 0) {
        double percentage = (double)dlnow / (double)dltotal * 100.0;
        
        time_t now = time(NULL);
        if (now > prog->last_time) {
            curl_off_t diff = dlnow - prog->last_dlnow;
            prog->speed = (double)diff / (now - prog->last_time);
            prog->last_dlnow = dlnow;
            prog->last_time = now;
        }
        
        if (percentage - prog->last_progress >= 1.0 || percentage >= 100.0) {
            int bar_width = 50;
            int pos = (int)(percentage * bar_width / 100.0);
            
            printf("\r[");
            for (int i = 0; i < bar_width; i++) {
                if (i < pos) printf("=");
                else if (i == pos && percentage < 100.0) printf(">");
                else printf(" ");
            }
            
            if (percentage >= 100.0) {
                printf("] %3.0f%% %s - Complete        \n", percentage, prog->filename);
            } else {
                printf("] %3.0f%% %s - %.1f KB/s      ", 
                       percentage, prog->filename, prog->speed / 1024.0);
            }
            fflush(stdout);
            
            prog->last_progress = percentage;
        }
    }
    return 0;
}

// ============================================================================
// FONCTION DE TÉLÉCHARGEMENT (STATIC POUR ÉVITER LES DUPLICATIONS)
// ============================================================================

static int download_from_url(const char *url, const char *output_path, const char *display_name) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    struct FileData file_data;
    file_data.fp = fopen(output_path, "wb");
    if (!file_data.fp) {
        curl_easy_cleanup(curl);
        return -1;
    }
    
    struct DownloadProgress prog = {
        .last_progress = 0,
        .last_time = time(NULL),
        .last_dlnow = 0,
        .speed = 0
    };
    strncpy(prog.filename, display_name, sizeof(prog.filename) - 1);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file_data);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM-Installer/2.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(file_data.fp);
    
    if (res != CURLE_OK) {
        unlink(output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    return (http_code == 200) ? 0 : -1;
}

// ============================================================================
// API PUBLIQUE (UNE SEULE FONCTION EXPORTÉE)
// ============================================================================

int download_package(const char *name, const char *version, const char *output_path) {
    char url[512];
    
    // Construire l'URL de téléchargement (à adapter selon votre dépôt)
    snprintf(url, sizeof(url), 
             "https://github.com/gopu-inc/apkm-gest/releases/download/v%s/%s-v%s-r1.x86_64.tar.bool",
             version, name, version);
    
    printf("[DOWNLOAD] Fetching %s %s...\n", name, version);
    
    return download_from_url(url, output_path, name);
}