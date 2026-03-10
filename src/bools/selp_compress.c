#include "bool.h"
#include <zlib.h>

// Compression ultra-rapide (RLE + Huffman simplifié)
static int compress_fast(const uint8_t *input, size_t in_size, 
                         uint8_t *output, size_t *out_size) {
    size_t in_pos = 0, out_pos = 0;
    
    while (in_pos < in_size && out_pos < *out_size - 8) {
        uint8_t current = input[in_pos];
        size_t run = 1;
        
        // Compter les répétitions
        while (in_pos + run < in_size && run < 255 && 
               input[in_pos + run] == current) {
            run++;
        }
        
        if (run >= 4) {
            // Run-length encoding
            output[out_pos++] = 0xFF;  // Marqueur RLE
            output[out_pos++] = current;
            output[out_pos++] = run;
        } else {
            // Copie directe
            for (size_t i = 0; i < run; i++) {
                output[out_pos++] = current;
            }
        }
        in_pos += run;
    }
    
    *out_size = out_pos;
    return SELP_OK;
}

// Compression moyenne (zlib)
static int compress_medium(const uint8_t *input, size_t in_size,
                           uint8_t *output, size_t *out_size) {
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    
    deflateInit(&strm, Z_BEST_COMPRESSION);
    
    strm.avail_in = in_size;
    strm.next_in = (Bytef*)input;
    strm.avail_out = *out_size;
    strm.next_out = output;
    
    int ret = deflate(&strm, Z_FINISH);
    *out_size = strm.total_out;
    
    deflateEnd(&strm);
    
    return (ret == Z_STREAM_END) ? SELP_OK : SELP_ERR_COMPRESS;
}

// Compression best (zlib + pré-traitement)
static int compress_best(const uint8_t *input, size_t in_size,
                         uint8_t *output, size_t *out_size) {
    // Pré-traitement : delta encoding pour les données similaires
    uint8_t *delta = malloc(in_size);
    if (!delta) return SELP_ERR_MEMORY;
    
    delta[0] = input[0];
    for (size_t i = 1; i < in_size; i++) {
        delta[i] = input[i] - input[i-1];
    }
    
    // Compression zlib
    int result = compress_medium(delta, in_size, output, out_size);
    free(delta);
    
    return result;
}

// Point d'entrée principal de compression
int selp_compress(const char *input_path, const char *output_path,
                  int compression_level, int encryption_level) {
    FILE *in = fopen(input_path, "rb");
    if (!in) return SELP_ERR_OPEN;
    
    // Lire tout le fichier
    fseek(in, 0, SEEK_END);
    size_t in_size = ftell(in);
    fseek(in, 0, SEEK_SET);
    
    uint8_t *in_data = malloc(in_size);
    if (!in_data) {
        fclose(in);
        return SELP_ERR_MEMORY;
    }
    fread(in_data, 1, in_size, in);
    fclose(in);
    
    // Buffer pour compression
    uint8_t *compressed = malloc(in_size * 2);  // Marge de sécurité
    if (!compressed) {
        free(in_data);
        return SELP_ERR_MEMORY;
    }
    
    size_t compressed_size = in_size * 2;
    int result;
    
    switch (compression_level) {
        case SELP_COMPRESS_FAST:
            result = compress_fast(in_data, in_size, compressed, &compressed_size);
            break;
        case SELP_COMPRESS_BEST:
            result = compress_best(in_data, in_size, compressed, &compressed_size);
            break;
        case SELP_COMPRESS_ULTRA:
            // Ultra : compression multiple passes
            result = compress_best(in_data, in_size, compressed, &compressed_size);
            // TODO: ajouter BWT + MTF pour ultra
            break;
        default:
            // Pas de compression
            memcpy(compressed, in_data, in_size);
            compressed_size = in_size;
            result = SELP_OK;
    }
    
    if (result == SELP_OK) {
        // Écrire avec en-tête SELP
        selp_ctx_t ctx;
        ctx.fp = fopen(output_path, "wb");
        if (!ctx.fp) {
            free(in_data);
            free(compressed);
            return SELP_ERR_OPEN;
        }
        
        // Préparer l'en-tête
        memcpy(ctx.header.magic, SELP_MAGIC, 4);
        ctx.header.version = SELP_VERSION;
        ctx.header.compression = compression_level;
        ctx.header.encryption = encryption_level;
        ctx.header.original_size = in_size;
        ctx.header.compressed_size = compressed_size;
        ctx.header.file_count = 1;
        ctx.header.timestamp = time(NULL);
        strcpy(ctx.header.comment, "BOOL SELP Archive");
        
        // Calculer checksum
        uint32_t checksum = 0;
        for (size_t i = 0; i < compressed_size; i++) {
            checksum += compressed[i];
        }
        ctx.header.checksum = checksum;
        
        // Écrire l'en-tête
        fwrite(&ctx.header, sizeof(selp_header_t), 1, ctx.fp);
        
        // Écrire les données compressées
        fwrite(compressed, 1, compressed_size, ctx.fp);
        
        fclose(ctx.fp);
        
        printf("[BOOL] Compressed: %.2f KB -> %.2f KB (%.1f%%)\n",
               in_size / 1024.0,
               compressed_size / 1024.0,
               100.0 * compressed_size / in_size);
    }
    
    free(in_data);
    free(compressed);
    return result;
}
