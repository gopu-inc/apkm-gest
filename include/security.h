#ifndef SECURITY_H
#define SECURITY_H

#include "apkm.h"

#define SECURITY_PATH "/usr/local/share/apkm/PROTOCOLE/security"
#define TOKEN_PATH SECURITY_PATH "/tokens/.config.cfg"
#define KEYRING_PATH SECURITY_PATH "/keys"
#define SIGNATURE_PATH SECURITY_PATH "/signatures"
#define CONFIG_FILE SECURITY_PATH "/APKM.apkm"

int security_init(void);
int security_load_token(security_token_t *token);
int security_get_token(char *token_buffer, size_t buffer_size);
int security_save_token(const security_token_t *token);
int security_download_token(void);
int calculate_sha256(const char *filepath, char *output);
void btscrypt_process(char *data, int encrypt);

#endif
