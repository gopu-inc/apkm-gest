#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Une simple fonction d'obfuscation (XOR) pour rendre le token illisible
void gopu_crypt(char *data) {
    char key = 0x47; // 'G' pour Gopu
    for(int i = 0; i < strlen(data); i++) {
        data[i] ^= key;
    }
}

// Fonction pour lire le token depuis .config.ini
char* get_gopu_token() {
    FILE *f = fopen(".config.ini", "r");
    if (!f) return NULL;
    
    char *token = malloc(256);
    if (fgets(token, 256, f)) {
        token[strcspn(token, "\n\r")] = 0;
        gopu_crypt(token); // On le décode en mémoire pour l'usage
        fclose(f);
        return token;
    }
    fclose(f);
    return NULL;
}

