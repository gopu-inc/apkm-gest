#ifndef ZARCH_H
#define ZARCH_H

#define ZARCH_HUB_URL "https://zenv-hub.onrender.com"
#define ZARCH_API_URL ZARCH_HUB_URL "/api"
#define ZARCH_PACKAGE_URL ZARCH_HUB_URL "/package/download"

typedef struct {
    char name[256];
    char version[64];
    char scope[64];
    char author[256];
    char sha256[128];
    long size;
    int downloads;
    char updated_at[32];
} zarch_package_t;

int zarch_login(const char *username, const char *password, char *token, size_t token_size);
int zarch_search(const char *query, zarch_package_t *results, int max_results);
int zarch_download(const char *scope, const char *name, const char *version, const char *output_path);

#endif
