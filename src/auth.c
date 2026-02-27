#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apkm.h"

// Clé de salage Gopu.inc
#define BTS_SALT 0x1B // 201B inspiration

// Fonction de rotation de bits pour le BTSCRYPT
unsigned char bts_rotate_left(unsigned char val, int n) {
    return (val << n) | (val >> (8 - n));
}

unsigned char bts_rotate_right(unsigned char val, int n) {
    return (val >> n) | (val << (8 - n));
}

// Chiffrement / Déchiffrement BTSCRYPT
void btscrypt_process(char *data, int encrypt) {
    for(int i = 0; i < (int)strlen(data); i++) {
        if (encrypt) {
            data[i] = bts_rotate_left(data[i] ^ BTS_SALT, 3);
        } else {
            data[i] = bts_rotate_right(data[i], 3) ^ BTS_SALT;
        }
    }
}

char* get_gopu_token() {
    FILE *f = fopen(".config.ini", "r");
    if (!f) return NULL;
    
    char *token = malloc(256);
    if (fgets(token, 256, f)) {
        token[strcspn(token, "\n\r")] = 0;
        // On déchiffre avec BTSCRYPT
        btscrypt_process(token, 0); 
        fclose(f);
        return token;
    }
    fclose(f);
    return NULL;
}
