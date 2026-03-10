#include "bool.h"
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int selp_verify(const char *path) {
    printf("🔐 Vérification de %s\n", path);
    
    FILE *fp = fopen(path, "rb");
    if (!fp) return SELP_ERR_OPEN;
    
    selp_header_t header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return SELP_ERR_READ;
    }
    
    // Lire les données
    fseek(fp, 0, SEEK_END);
    long data_size = ftell(fp) - sizeof(header);
    fseek(fp, sizeof(header), SEEK_SET);
    
    uint8_t *data = malloc(data_size);
    fread(data, 1, data_size, fp);
    fclose(fp);
    
    // Calculer la signature
    SHA256_CTX sha;
    SHA256_Init(&sha);
    SHA256_Update(&sha, data, data_size);
    uint8_t hash[32];
    SHA256_Final(hash, &sha);
    
    uint32_t signature[8];
    for (int i = 0; i < 8; i++) {
        signature[i] = (hash[i*4] << 24) | (hash[i*4+1] << 16) |
                       (hash[i*4+2] << 8) | hash[i*4+3];
    }
    
    // Comparer
    int valid = 1;
    for (int i = 0; i < 8; i++) {
        if (header.signature[i] != signature[i]) {
            valid = 0;
            break;
        }
    }
    
    free(data);
    
    if (valid) {
        printf("✅ Signature valide\n");
        printf("   Version: %d\n", header.version);
        printf("   Compression: %d\n", header.compression);
        printf("   Original: %llu octets\n", (unsigned long long)header.original_size);
        printf("   Compressé: %llu octets\n", (unsigned long long)header.compressed_size);
        printf("   Ratio: %.1f%%\n", 100.0 * header.compressed_size / header.original_size);
        return SELP_OK;
    } else {
        printf("❌ Signature invalide !\n");
        return SELP_ERR_SIGNATURE;
    }
}
