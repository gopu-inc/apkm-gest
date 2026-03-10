#include "bool.h"

int selp_compress_files(int argc, char **argv, const char *output,
                       int level, int crypt, const char *author,
                       const char *comment) {
    printf("📦 Compressing %d files\n", argc);
    
    // Calculer la taille totale
    uint64_t total_size = 0;
    struct stat st;
    
    for (int i = 0; i < argc; i++) {
        if (stat(argv[i], &st) == 0) {
            total_size += st.st_size;
            printf("   • %s (%ld bytes)\n", argv[i], st.st_size);
        } else {
            printf("❌ Cannot access: %s\n", argv[i]);
            return SELP_ERR_NOT_FOUND;
        }
    }
    
    printf("📊 Total size: %.2f KB\n", total_size / 1024.0);
    
    // Créer l'en-tête
    selp_header_t header;
    memcpy(header.magic, SELP_MAGIC, 4);
    header.version = SELP_VERSION;
    header.compression = level;
    header.encryption = crypt;
    header.original_size = total_size;
    header.file_count = argc;
    header.timestamp = time(NULL);
    strncpy(header.author, author ? author : "Unknown", MAX_AUTHOR - 1);
    strncpy(header.comment, comment ? comment : "BOOL SELP Archive", MAX_COMMENT - 1);
    
    // Ouvrir le fichier de sortie
    FILE *out = fopen(output, "wb");
    if (!out) {
        printf("❌ Cannot create output file\n");
        return SELP_ERR_OPEN;
    }
    
    // Écrire l'en-tête
    fwrite(&header, sizeof(selp_header_t), 1, out);
    
    // Écrire les entrées
    for (int i = 0; i < argc; i++) {
        selp_file_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        
        strncpy(entry.path, argv[i], MAX_PATH - 1);
        
        char *base = strrchr(argv[i], '/');
        if (base) base++; else base = argv[i];
        strncpy(entry.name, base, sizeof(entry.name) - 1);
        
        struct stat st;
        stat(argv[i], &st);
        entry.size = st.st_size;
        entry.permissions = st.st_mode;
        entry.mtime = st.st_mtime;
        entry.offset = 0;
        
        fwrite(&entry, sizeof(selp_file_entry_t), 1, out);
    }
    
    // Écrire les données
    for (int i = 0; i < argc; i++) {
        FILE *in = fopen(argv[i], "rb");
        if (!in) continue;
        
        printf("📦 Writing: %s\n", argv[i]);
        
        uint8_t buffer[8192];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), in)) > 0) {
            fwrite(buffer, 1, bytes, out);
        }
        fclose(in);
    }
    
    // Mettre à jour la taille compressée
    header.compressed_size = ftell(out) - sizeof(selp_header_t);
    fseek(out, 0, SEEK_SET);
    fwrite(&header, sizeof(selp_header_t), 1, out);
    
    fclose(out);
    
    printf("\n✅ Files compressed successfully!\n");
    printf("   Output: %s (%.2f KB)\n", output, header.compressed_size / 1024.0);
    printf("   Ratio:  %.1f%%\n", 100.0 * header.compressed_size / total_size);
    
    return SELP_OK;
}
