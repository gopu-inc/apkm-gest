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
#define REPO_URL "https://raw.githubusercontent.com/gopu-inc/apkm-gest/master"
#define REPO_API "https://api.github.com/repos/gopu-inc/apkm-gest"
#define METADATA_FILE "DATA.db"

typedef struct {
    char token[512];
    char sha256[128];
    char pgp_key[4096];
    char ssh_key[4096];
    time_t last_update;
    int validated;
} security_token_t;

typedef struct {
    char name[256];
    char version[64];
    char sha256[128];
    char signature[1024];
    char publisher[256];
    time_t timestamp;
} package_metadata_t;

// Prototypes
int security_init(void);
int security_load_token(security_token_t *token);
int security_save_token(const security_token_t *token);
int security_download_token(void);
int security_validate_token(const char *token);
int security_sign_package(const char *package_path, const char *key_path, char *signature);
int security_verify_package(const char *package_path, const char *signature);
int security_check_duplicate(const char *package_name, const char *version);
int security_update_metadata(const package_metadata_t *metadata);
int security_generate_keypair(const char *name, const char *email);
int calculate_sha256(const char *filepath, char *output);
void btscrypt_process(char *data, int encrypt);

#endif
