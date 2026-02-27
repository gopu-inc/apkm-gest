#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "apkm.h"

#define BTS_SALT 0x1B 

// Fonctions de rotation (internes à auth.c)
static unsigned char bts_rotate_left(unsigned char val, int n) {
    return (val << n) | (val >> (8 - n));
}

static unsigned char bts_rotate_right(unsigned char val, int n) {
    return (val >> n) | (val << (8 - n));
}

// Le moteur BTSCRYPT
void btscrypt_process(char *data, int encrypt) {
    int len = strlen(data);
    for(int i = 0; i < len; i++) {
        if (encrypt) {
            data[i] = bts_rotate_left(data[i] ^ BTS_SALT, 3);
        } else {
            data[i] = bts_rotate_right((unsigned char)data[i], 3) ^ BTS_SALT;
        }
    }
}

void get_config_path(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        strncpy(path, "/root/.config.cfg", size);
    } else {
        snprintf(path, size, "%s/.config.cfg", home);
    }
}

char* load_token_from_home() {
    char config_path[512];
    get_config_path(config_path, sizeof(config_path));

    FILE *f = fopen(config_path, "r");
    if (!f) return NULL;

    char line[512];
    char *token = NULL;
    if (fgets(line, sizeof(line), f)) {
        char *ptr = strstr(line, "TOKEN=");
        if (ptr) {
            token = strdup(ptr + 6);
            token[strcspn(token, "\n\r")] = 0;
            btscrypt_process(token, 0); // Déchiffrement
            fclose(f);
            return token;
        }
    }
    fclose(f);
    return NULL;
}

