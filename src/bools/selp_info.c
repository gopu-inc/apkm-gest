#include "bool.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

int selp_info(const char *archive) {
    FILE *fp = fopen(archive, "rb");
    if (!fp) {
        printf("❌ Cannot open file: %s\n", archive);
        return SELP_ERR_OPEN;
    }
    
    // Lire l'en-tête
    selp_header_t header;
    if (fread(&header, sizeof(selp_header_t), 1, fp) != 1) {
        printf("❌ Cannot read header\n");
        fclose(fp);
        return SELP_ERR_READ;
    }
    
    // Vérifier le magic
    if (memcmp(header.magic, SELP_MAGIC, 4) != 0) {
        printf("❌ Invalid SELP magic number\n");
        fclose(fp);
        return SELP_ERR_MAGIC;
    }
    
    printf("\n📦 SELP Archive Information\n");
    printf("═══════════════════════════\n");
    printf("File:         %s\n", archive);
    printf("Magic:        %.4s\n", header.magic);
    printf("Version:      %d\n", header.version);
    printf("Compression:  %d (%s)\n", header.compression,
           header.compression == 0 ? "none" :
           header.compression == 1 ? "fast" :
           header.compression == 2 ? "best" : "ultra");
    printf("Encryption:   %d (%s)\n", header.encryption,
           header.encryption == 0 ? "none" :
           header.encryption == 1 ? "light" :
           header.encryption == 2 ? "medium" : "strong");
    printf("Flags:        0x%02x\n", header.flags);
    printf("Files:        %llu\n", (unsigned long long)header.file_count);
    printf("Original:     %llu bytes (%.2f KB / %.2f MB)\n", 
           (unsigned long long)header.original_size,
           header.original_size / 1024.0,
           header.original_size / (1024.0 * 1024.0));
    printf("Compressed:   %llu bytes (%.2f KB / %.2f MB)\n",
           (unsigned long long)header.compressed_size,
           header.compressed_size / 1024.0,
           header.compressed_size / (1024.0 * 1024.0));
    printf("Ratio:        %.1f%%\n",
           100.0 * header.compressed_size / header.original_size);
    printf("Timestamp:    %s", ctime(&header.timestamp));
    printf("Author:       %s\n", header.author);
    printf("Comment:      %s\n", header.comment);
    
    // Lire les entrées de fichiers pour plus de détails
    if (header.file_count > 0) {
        printf("\n📄 File entries:\n");
        printf("────────────────────────────────\n");
        
        selp_file_entry_t entry;
        for (uint64_t i = 0; i < header.file_count; i++) {
            if (fread(&entry, sizeof(selp_file_entry_t), 1, fp) != 1) {
                break;
            }
            
            char time_str[64];
            struct tm *tm = localtime(&entry.mtime);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm);
            
            printf("  %2llu. %s\n", (unsigned long long)i + 1, entry.name);
            printf("      Path:  %s\n", entry.path);
            printf("      Size:  %llu bytes (%.2f KB)\n", 
                   (unsigned long long)entry.size, entry.size / 1024.0);
            printf("      Perm:  %o\n", entry.permissions);
            printf("      Mtime: %s\n", time_str);
            printf("      CRC32: %08x\n", entry.crc32);
            printf("\n");
        }
    }
    
    // Afficher la signature
    printf("🔐 Signature: ");
    for (int i = 0; i < 8; i++) {
        printf("%08x ", header.signature[i]);
    }
    printf("\n");
    
    fclose(fp);
    return SELP_OK;
}

int selp_magic_info(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return SELP_ERR_OPEN;
    
    selp_header_t header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return SELP_ERR_READ;
    }
    fclose(fp);
    
    if (memcmp(header.magic, "SELP", 4) != 0) return SELP_ERR_MAGIC;
    
    printf("[SELP PASSIB LAB SOCKET 2001x006]\n");
    printf("Version: %d\n", header.version);
    printf("Compression: %s\n", 
           header.compression == 0 ? "none" :
           header.compression == 1 ? "fast" :
           header.compression == 2 ? "best" : "ultra");
    printf("Encryption: %s\n",
           header.encryption == 0 ? "none" :
           header.encryption == 1 ? "light" :
           header.encryption == 2 ? "medium" : "strong");
    printf("Original: %llu bytes (%.2f KB)\n", 
           (unsigned long long)header.original_size,
           header.original_size / 1024.0);
    printf("Compressed: %llu bytes (%.2f KB)\n",
           (unsigned long long)header.compressed_size,
           header.compressed_size / 1024.0);
    printf("Ratio: %.1f%%\n", 100.0 * header.compressed_size / header.original_size);
    printf("Files: %llu\n", (unsigned long long)header.file_count);
    printf("Author: %s\n", header.author);
    printf("Comment: %s\n", header.comment);
    
    return SELP_OK;
}
