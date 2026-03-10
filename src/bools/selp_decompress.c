#include "bool.h"
#include <zlib.h>

static int decompress_fast(const uint8_t *input, size_t in_size,
                           uint8_t *output, size_t *out_size) {
    size_t in_pos = 0, out_pos = 0;
    
    while (in_pos < in_size && out_pos < *out_size) {
        if (input[in_pos] == 0xFF && in_pos + 2 < in_size) {
            // Séquence RLE
            uint8_t value = input[in_pos + 1];
            uint8_t count = input[in_pos + 2];
            
            for (int i = 0; i < count && out_pos < *out_size; i++) {
                output[out_pos++] = value;
            }
            in_pos += 3;
        } else {
            // Donnée normale
            output[out_pos++] = input[in_pos++];
        }
    }
    
    *out_size = out_pos;
    return SELP_OK;
}

static int decompress_medium(const uint8_t *input, size_t in_size,
                             uint8_t *output, size_t *out_size) {
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    
    inflateInit(&strm);
    
    strm.avail_in = in_size;
    strm.next_in = (Bytef*)input;
    strm.avail_out = *out_size;
    strm.next_out = output;
    
    int ret = inflate(&strm, Z_FINISH);
    *out_size = strm.total_out;
    
    inflateEnd(&strm);
    
    return (ret == Z_STREAM_END) ? SELP_OK : SELP_ERR_DECOMPRESS;
}

static int decompress_best(const uint8_t *input, size_t in_size,
                           uint8_t *output, size_t *out_size) {
    // Décompresser d'abord
    int result = decompress_medium(input, in_size, output, out_size);
    if (result != SELP_OK) return result;
    
    // Post-traitement : inverse du delta encoding
    for (size_t i = *out_size - 1; i > 0; i--) {
        output[i] += output[i-1];
    }
    
    return SELP_OK;
}

// Point d'entrée principal de décompression
int selp_decompress(const char *input_path, const char *output_path) {
    FILE *in = fopen(input_path, "rb");
    if (!in) return SELP_ERR_OPEN;
    
    // Lire l'en-tête
    selp_header_t header;
    if (fread(&header, sizeof(selp_header_t), 1, in) != 1) {
        fclose(in);
        return SELP_ERR_READ;
    }
    
    // Vérifier la signature
    if (memcmp(header.magic, SELP_MAGIC, 4) != 0) {
        fclose(in);
        return SELP_ERR_MAGIC;
    }
    
    // Lire les données compressées
    uint8_t *compressed = malloc(header.compressed_size);
    if (!compressed) {
        fclose(in);
        return SELP_ERR_MEMORY;
    }
    fread(compressed, 1, header.compressed_size, in);
    fclose(in);
    
    // Vérifier checksum
    uint32_t checksum = 0;
    for (size_t i = 0; i < header.compressed_size; i++) {
        checksum += compressed[i];
    }
    if (checksum != header.checksum) {
        free(compressed);
        return SELP_ERR_CHECKSUM;
    }
    
    // Décompresser
    uint8_t *decompressed = malloc(header.original_size);
    if (!decompressed) {
        free(compressed);
        return SELP_ERR_MEMORY;
    }
    
    size_t decompressed_size = header.original_size;
    int result;
    
    switch (header.compression) {
        case SELP_COMPRESS_FAST:
            result = decompress_fast(compressed, header.compressed_size,
                                     decompressed, &decompressed_size);
            break;
        case SELP_COMPRESS_BEST:
        case SELP_COMPRESS_ULTRA:
            result = decompress_best(compressed, header.compressed_size,
                                     decompressed, &decompressed_size);
            break;
        default:
            memcpy(decompressed, compressed, header.original_size);
            result = SELP_OK;
    }
    
    if (result == SELP_OK) {
        FILE *out = fopen(output_path, "wb");
        if (out) {
            fwrite(decompressed, 1, decompressed_size, out);
            fclose(out);
            printf("[BOOL] Decompressed: %.2f KB -> %.2f KB\n",
                   header.compressed_size / 1024.0,
                   decompressed_size / 1024.0);
        } else {
            result = SELP_ERR_OPEN;
        }
    }
    
    free(compressed);
    free(decompressed);
    return result;
}
