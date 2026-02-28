#ifndef APKM_SECURITY_H
#define APKM_SECURITY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#define SECURITY_PATH "/usr/local/share/apkm/PROTOCOLE/security"
#define TOKEN_PATH SECURITY_PATH "/tokens/.config.cfg"
#define KEYRING_PATH SECURITY_PATH "/keys"
#define SIGNATURE_PATH SECURITY_PATH "/signatures"
#define CONFIG_FILE SECURITY_PATH "/APKM.apkm"
#define REPO_OWNER "gopu-inc"
#define REPO_NAME "apkm-gest"
#define REPO_API "https://api.github.com/repos/gopu-inc/apkm-gest"
#define REPO_RAW "https://raw.githubusercontent.com/gopu-inc/apkm-gest/main"
#define REPO_RELEASES "https://api.github.com/repos/gopu-inc/apkm-gest/releases"

typedef struct {
    char token[512];
    char sha256[128];
    time_t last_update;
    int validated;
} security_token_t;

typedef struct {
    char name[256];
    char version[64];
    char release[16];
    char arch[32];
    char sha256[128];
    char url[512];
    time_t timestamp;
    char publisher[256];
} package_metadata_t;

// Prototypes
int security_init(void);
int security_load_token(security_token_t *token);
int security_save_token(const security_token_t *token);
int security_download_token(void);
int calculate_sha256(const char *filepath, char *output);
void btscrypt_process(char *data, int encrypt);

#endif
