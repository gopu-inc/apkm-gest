#include "bool.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int selp_decompress(const char *input, const char *output) {
    printf("📂 Extracting %s -> %s\n", input, output);
    
    FILE *in = fopen(input, "rb");
    if (!in) return SELP_ERR_OPEN;
    
    // Lire l'en-tête
    selp_header_t header;
    if (fread(&header, sizeof(header), 1, in) != 1) {
        fclose(in);
        return SELP_ERR_READ;
    }
    
    // Vérifier le magic
    if (memcmp(header.magic, "SELP", 4) != 0) {
        fclose(in);
        return SELP_ERR_MAGIC;
    }
    
    // Lire les données compressées
    uint8_t *compressed = malloc(header.compressed_size);
    fread(compressed, 1, header.compressed_size, in);
    fclose(in);
    
    // Décompresser
    uLongf decompressed_size = header.original_size;
    uint8_t *decompressed = malloc(decompressed_size);
    
    uncompress(decompressed, &decompressed_size, compressed, header.compressed_size);
    
    // Écrire le fichier
    FILE *out = fopen(output, "wb");
    fwrite(decompressed, 1, decompressed_size, out);
    fclose(out);
    
    printf("✅ Décompression réussie: %.2f KB -> %.2f KB\n",
           header.compressed_size/1024.0, decompressed_size/1024.0);
    
    free(compressed);
    free(decompressed);
    return SELP_OK;
}
