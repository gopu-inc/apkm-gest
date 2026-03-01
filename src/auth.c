#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "apkm.h"
#include "security.h"

#define BTS_SALT 0x1B 

// ============================================================================
// BTSCRYPT - Implémentation complète
// ============================================================================

static unsigned char bts_rotate_left(unsigned char val, int n) {
    return (unsigned char)((val << n) | (val >> (8 - n)));
}

static unsigned char bts_rotate_right(unsigned char val, int n) {
    return (unsigned char)((val >> n) | (val << (8 - n)));
}

void btscrypt_process(char *data, int encrypt) {
    if (!data) return;
    
    size_t len = strlen(data);
    for (size_t i = 0; i < len; i++) {
        if (encrypt) {
            data[i] = (char)bts_rotate_left((unsigned char)(data[i] ^ BTS_SALT), 3);
        } else {
            data[i] = (char)(bts_rotate_right((unsigned char)data[i], 3) ^ BTS_SALT);
        }
    }
}

// ============================================================================
// GESTION DU CHEMIN DE CONFIGURATION
// ============================================================================

void get_config_path(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (home == NULL) {
        strncpy(path, "/root/.config.cfg", size);
    } else {
        snprintf(path, size, "%s/.config.cfg", home);
    }
}

// ============================================================================
// CHARGEMENT DU TOKEN
// ============================================================================

char* load_token_from_home(void) {
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
            if (token) {
                // Nettoyer les retours à la ligne
                size_t len = strlen(token);
                while (len > 0 && (token[len-1] == '\n' || token[len-1] == '\r')) {
                    token[len-1] = '\0';
                    len--;
                }
                
                // Déchiffrer le token
                btscrypt_process(token, 0);
            }
        }
    }
    fclose(f);
    return token;
}