#include "bool.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int selp_compress(const char *input, const char *output, int level, int crypt) {
    printf("📦 Compressing %s -> %s (level %d, crypt %d)\n", input, output, level, crypt);
    
    FILE *in = fopen(input, "rb");
    if (!in) return SELP_ERR_OPEN;
    
    // Lire le fichier
    fseek(in, 0, SEEK_END);
    size_t in_size = ftell(in);
    fseek(in, 0, SEEK_SET);
    
    uint8_t *data = malloc(in_size);
    if (!data) {
        fclose(in);
        return SELP_ERR_MEMORY;
    }
    fread(data, 1, in_size, in);
    fclose(in);
    
    // Créer l'en-tête SELP
    selp_header_t header;
    memcpy(header.magic, "SELP", 4);
    header.version = 1;
    header.compression = level;
    header.encryption = crypt;
    header.original_size = in_size;
    header.file_count = 1;
    header.timestamp = time(NULL);
    strcpy(header.comment, "BOOL SELP Archive");
    
    // Compresser avec zlib
    uLongf compressed_size = compressBound(in_size);
    uint8_t *compressed = malloc(compressed_size);
    
    compress2(compressed, &compressed_size, data, in_size, 
              level == 0 ? Z_NO_COMPRESSION : 
              level == 1 ? Z_BEST_SPEED : 
              level == 2 ? Z_DEFAULT_COMPRESSION : Z_BEST_COMPRESSION);
    
    header.compressed_size = compressed_size;
    
    // Calculer la signature
    SHA256_CTX sha;
    SHA256_Init(&sha);
    SHA256_Update(&sha, compressed, compressed_size);
    uint8_t hash[32];
    SHA256_Final(hash, &sha);
    
    for (int i = 0; i < 8; i++) {
        header.signature[i] = (hash[i*4] << 24) | (hash[i*4+1] << 16) |
                              (hash[i*4+2] << 8) | hash[i*4+3];
    }
    
    // Écrire le fichier
    FILE *out = fopen(output, "wb");
    fwrite(&header, sizeof(header), 1, out);
    fwrite(compressed, 1, compressed_size, out);
    fclose(out);
    
    printf("✅ Compression réussie: %.2f KB -> %.2f KB (%.1f%%)\n",
           in_size/1024.0, compressed_size/1024.0,
           100.0 * compressed_size / in_size);
    
    free(data);
    free(compressed);
    return SELP_OK;
}
