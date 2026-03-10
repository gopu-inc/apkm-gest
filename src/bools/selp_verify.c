#include "bool.h"
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>

// Fonction de vérification de signature SELP
int selp_verify(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("❌ Cannot open file: %s\n", path);
        return SELP_ERR_OPEN;
    }
    
    // Lire l'en-tête
    selp_header_t header;
    if (fread(&header, sizeof(selp_header_t), 1, fp) != 1) {
        printf("❌ Cannot read header\n");
        fclose(fp);
        return SELP_ERR_READ;
    }
    
    // Vérifier le magic number
    if (memcmp(header.magic, "SELP", 4) != 0) {
        printf("❌ Invalid SELP magic number\n");
        fclose(fp);
        return SELP_ERR_MAGIC;
    }
    
    // Calculer la signature attendue
    uint32_t expected[8];
    
    // Lire les données pour calculer la signature
    fseek(fp, 0, SEEK_END);
    long data_size = ftell(fp) - sizeof(selp_header_t);
    fseek(fp, sizeof(selp_header_t), SEEK_SET);
    
    uint8_t *data = malloc(data_size);
    if (!data) {
        printf("❌ Memory allocation failed\n");
        fclose(fp);
        return SELP_ERR_MEMORY;
    }
    
    fread(data, 1, data_size, fp);
    fclose(fp);
    
    // Calculer SHA256 des données
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, data_size);
    
    uint8_t hash[32];
    SHA256_Final(hash, &sha256);
    
    // Convertir en 8 uint32_t
    for (int i = 0; i < 8; i++) {
        expected[i] = (hash[i*4] << 24) | (hash[i*4+1] << 16) |
                      (hash[i*4+2] << 8) | hash[i*4+3];
    }
    
    // Vérifier la signature
    int match = 1;
    for (int i = 0; i < 8; i++) {
        if (header.signature[i] != expected[i]) {
            match = 0;
            break;
        }
    }
    
    free(data);
    
    if (match) {
        printf("✅ Signature valid\n");
        printf("   Version: %d\n", header.version);
        printf("   Compression: %d\n", header.compression);
        printf("   Encryption: %d\n", header.encryption);
        printf("   Original size: %llu bytes\n", (unsigned long long)header.original_size);
        printf("   Compressed size: %llu bytes\n", (unsigned long long)header.compressed_size);
        printf("   Ratio: %.1f%%\n", 
               100.0 * header.compressed_size / header.original_size);
        return SELP_OK;
    } else {
        printf("❌ Invalid signature\n");
        return SELP_ERR_SIGNATURE;
    }
}

// Fonction pour afficher les infos de l'archive
int selp_info(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("❌ Cannot open file: %s\n", path);
        return SELP_ERR_OPEN;
    }
    
    selp_header_t header;
    if (fread(&header, sizeof(selp_header_t), 1, fp) != 1) {
        printf("❌ Cannot read header\n");
        fclose(fp);
        return SELP_ERR_READ;
    }
    
    fclose(fp);
    
    printf("📦 SELP Archive Information\n");
    printf("═══════════════════════════\n");
    printf("Magic:       %.4s\n", header.magic);
    printf("Version:     %d\n", header.version);
    printf("Compression: %d (%s)\n", header.compression,
           header.compression == 0 ? "none" :
           header.compression == 1 ? "fast" :
           header.compression == 2 ? "best" : "ultra");
    printf("Encryption:  %d (%s)\n", header.encryption,
           header.encryption == 0 ? "none" :
           header.encryption == 1 ? "light" :
           header.encryption == 2 ? "medium" : "strong");
    printf("Files:       %llu\n", (unsigned long long)header.file_count);
    printf("Original:    %llu bytes (%.2f KB)\n", 
           (unsigned long long)header.original_size,
           header.original_size / 1024.0);
    printf("Compressed:  %llu bytes (%.2f KB)\n",
           (unsigned long long)header.compressed_size,
           header.compressed_size / 1024.0);
    printf("Ratio:       %.1f%%\n",
           100.0 * header.compressed_size / header.original_size);
    printf("Timestamp:   %s", ctime(&header.timestamp));
    printf("Comment:     %s\n", header.comment);
    
    return SELP_OK;
}
