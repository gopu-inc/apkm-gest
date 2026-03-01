#include "apkm.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char *data;
    size_t size;
} zarch_response_t;

static size_t zarch_write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    zarch_response_t *resp = (zarch_response_t *)userdata;
    size_t total = size * nmemb;
    
    resp->data = realloc(resp->data, resp->size + total + 1);
    if (!resp->data) return 0;
    
    memcpy(resp->data + resp->size, ptr, total);
    resp->size += total;
    resp->data[resp->size] = '\0';
    
    return total;
}

int zarch_login(const char *username, const char *password, char *token, size_t token_size) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    zarch_response_t resp = {0};
    struct curl_slist *headers = NULL;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/auth/login", ZARCH_API_URL);
    
    char post_data[1024];
    snprintf(post_data, sizeof(post_data),
             "{\"username\":\"%s\",\"password\":\"%s\"}",
             username, password);
    
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, zarch_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    int success = -1;
    if (res == CURLE_OK && resp.data) {
        struct json_object *parsed = json_tokener_parse(resp.data);
        if (parsed) {
            struct json_object *token_obj;
            if (json_object_object_get_ex(parsed, "token", &token_obj)) {
                const char *token_str = json_object_get_string(token_obj);
                strncpy(token, token_str, token_size - 1);
                token[token_size - 1] = '\0';
                success = 0;
            }
            json_object_put(parsed);
        }
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (resp.data) free(resp.data);
    
    return success;
}

int zarch_search(const char *query, zarch_package_t *results, int max_results) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    zarch_response_t resp = {0};
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/search?q=%s", ZARCH_API_URL, query);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, zarch_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    int count = 0;
    if (res == CURLE_OK && resp.data) {
        struct json_object *parsed = json_tokener_parse(resp.data);
        if (parsed) {
            struct json_object *results_obj;
            if (json_object_object_get_ex(parsed, "results", &results_obj)) {
                int len = json_object_array_length(results_obj);
                for (int i = 0; i < len && i < max_results; i++) {
                    struct json_object *pkg = json_object_array_get_idx(results_obj, i);
                    
                    struct json_object *name_obj, *version_obj, *author_obj, *downloads_obj;
                    
                    memset(&results[count], 0, sizeof(zarch_package_t));
                    
                    if (json_object_object_get_ex(pkg, "name", &name_obj))
                        strcpy(results[count].name, json_object_get_string(name_obj));
                    if (json_object_object_get_ex(pkg, "version", &version_obj))
                        strcpy(results[count].version, json_object_get_string(version_obj));
                    if (json_object_object_get_ex(pkg, "author", &author_obj))
                        strcpy(results[count].author, json_object_get_string(author_obj));
                    if (json_object_object_get_ex(pkg, "downloads", &downloads_obj))
                        results[count].downloads = json_object_get_int(downloads_obj);
                    
                    count++;
                }
            }
            json_object_put(parsed);
        }
    }
    
    curl_easy_cleanup(curl);
    if (resp.data) free(resp.data);
    
    return count;
}

int zarch_download(const char *name, const char *version, const char *arch, const char *output_path) {
    (void)arch; // Unused for now
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/download/public/%s/%s", 
             ZARCH_HUB_URL, name, version);
    
    printf("[ZARCH] Downloading %s %s...\n", name, version);
    
    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "APKM/2.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[ZARCH] Download failed: %s\n", curl_easy_strerror(res));
        unlink(output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (http_code != 200) {
        fprintf(stderr, "[ZARCH] HTTP error: %ld\n", http_code);
        unlink(output_path);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    printf("[ZARCH] Download complete\n");
    curl_easy_cleanup(curl);
    
    return 0;
}

int zarch_list_repos(output_format_t format) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    zarch_response_t resp = {0};
    
    char url[512];
    snprintf(url, sizeof(url), "%s/package/search?q=", ZARCH_API_URL);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, zarch_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK && resp.data) {
        if (format == OUTPUT_JSON) {
            printf("%s\n", resp.data);
        } else {
            struct json_object *parsed = json_tokener_parse(resp.data);
            if (parsed) {
                struct json_object *results_obj;
                if (json_object_object_get_ex(parsed, "results", &results_obj)) {
                    int len = json_object_array_length(results_obj);
                    
                    printf("\n📦 ZARCH HUB PACKAGES\n");
                    printf("═══════════════════════════════════════════\n");
                    printf("%-20s %-12s %-15s %-10s\n", "NAME", "VERSION", "AUTHOR", "DOWNLOADS");
                    printf("───────────────────────────────────────────\n");
                    
                    for (int i = 0; i < len; i++) {
                        struct json_object *pkg = json_object_array_get_idx(results_obj, i);
                        struct json_object *name, *version, *author, *downloads;
                        
                        const char *n = "?", *v = "?", *a = "?";
                        int d = 0;
                        
                        if (json_object_object_get_ex(pkg, "name", &name))
                            n = json_object_get_string(name);
                        if (json_object_object_get_ex(pkg, "version", &version))
                            v = json_object_get_string(version);
                        if (json_object_object_get_ex(pkg, "author", &author))
                            a = json_object_get_string(author);
                        if (json_object_object_get_ex(pkg, "downloads", &downloads))
                            d = json_object_get_int(downloads);
                        
                        printf(" • %-20s %-12s %-15s %-10d\n", n, v, a, d);
                    }
                    
                    printf("═══════════════════════════════════════════\n");
                    printf(" Total: %d packages\n", len);
                }
                json_object_put(parsed);
            }
        }
        free(resp.data);
        return 0;
    }
    
    if (resp.data) free(resp.data);
    return -1;
}
