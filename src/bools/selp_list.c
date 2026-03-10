#include "bool.h"

int selp_list(const char *archive) {
    FILE *fp = fopen(archive, "rb");
    if (!fp) return SELP_ERR_OPEN;
    
    selp_header_t header;
    fread(&header, sizeof(selp_header_t), 1, fp);
    
    if (memcmp(header.magic, SELP_MAGIC, 4) != 0) {
        fclose(fp);
        return SELP_ERR_MAGIC;
    }
    
    printf("\n📦 Archive: %s\n", archive);
    printf("══════════════════════════════════════════════\n");
    printf("Version:     %d\n", header.version);
    printf("Compression: %d\n", header.compression);
    printf("Encryption:  %d\n", header.encryption);
    printf("Files:       %llu\n", (unsigned long long)header.file_count);
    printf("Author:      %s\n", header.author);
    printf("Comment:     %s\n", header.comment);
    printf("Created:     %s", ctime(&header.timestamp));
    printf("Original:    %.2f KB\n", header.original_size / 1024.0);
    printf("Compressed:  %.2f KB\n", header.compressed_size / 1024.0);
    printf("Ratio:       %.1f%%\n", 100.0 * header.compressed_size / header.original_size);
    printf("══════════════════════════════════════════════\n\n");
    
    printf("Contents:\n");
    printf("──────────────────────────────────────────────\n");
    
    selp_file_entry_t entry;
    for (uint64_t i = 0; i < header.file_count; i++) {
        fread(&entry, sizeof(selp_file_entry_t), 1, fp);
        printf("  %s (%s, %.2f KB)\n", 
               entry.name, entry.path, entry.size / 1024.0);
    }
    
    fclose(fp);
    return SELP_OK;
}
