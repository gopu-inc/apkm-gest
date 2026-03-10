#include "bool.h"
#include <dirent.h>
#include <sys/stat.h>

// Ajouter un fichier à l'archive
static int add_file_to_archive(selp_ctx_t *ctx, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return SELP_ERR_OPEN;
    
    // Lire le fichier
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *data = malloc(size);
    if (!data) {
        fclose(f);
        return SELP_ERR_MEMORY;
    }
    fread(data, 1, size, f);
    fclose(f);
    
    // Écrire l'entrée
    selp_file_entry_t entry;
    strncpy(entry.path, path, sizeof(entry.path) - 1);
    entry.size = size;
    entry.mtime = time(NULL);
    entry.permissions = 0644;
    
    // Calculer CRC32 simple
    uint32_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc += data[i];
    }
    entry.crc32 = crc;
    
    fwrite(&entry, sizeof(selp_file_entry_t), 1, ctx->fp);
    fwrite(data, 1, size, ctx->fp);
    
    free(data);
    return SELP_OK;
}

// Créer une archive depuis un dossier
int selp_create_archive(const char *dir_path, const char *output_path,
                        int compression, int encryption) {
    DIR *dir = opendir(dir_path);
    if (!dir) return SELP_ERR_OPEN;
    
    selp_ctx_t ctx;
    ctx.fp = fopen(output_path, "wb");
    if (!ctx.fp) {
        closedir(dir);
        return SELP_ERR_OPEN;
    }
    
    // Compter les fichiers
    int file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_type == DT_REG) file_count++;
    }
    rewinddir(dir);
    
    // Préparer l'en-tête
    memcpy(ctx.header.magic, SELP_MAGIC, 4);
    ctx.header.version = SELP_VERSION;
    ctx.header.compression = compression;
    ctx.header.encryption = encryption;
    ctx.header.file_count = file_count;
    ctx.header.timestamp = time(NULL);
    strcpy(ctx.header.comment, "BOOL SELP Archive");
    
    // Écrire l'en-tête (à mettre à jour plus tard)
    fwrite(&ctx.header, sizeof(selp_header_t), 1, ctx.fp);
    
    // Ajouter chaque fichier
    uint64_t total_size = 0;
    while ((entry = readdir(dir))) {
        if (entry->d_type == DT_REG) {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            
            add_file_to_archive(&ctx, full_path);
            
            struct stat st;
            stat(full_path, &st);
            total_size += st.st_size;
        }
    }
    
    closedir(dir);
    
    // Mettre à jour l'en-tête avec la taille totale
    fseek(ctx.fp, 0, SEEK_SET);
    ctx.header.original_size = total_size;
    fwrite(&ctx.header, sizeof(selp_header_t), 1, ctx.fp);
    
    fclose(ctx.fp);
    
    printf("[BOOL] Archive created: %s (%d files, %.2f KB)\n",
           output_path, file_count, total_size / 1024.0);
    
    return SELP_OK;
}

// Extraire une archive
int selp_extract_archive(const char *archive_path, const char *output_dir) {
    FILE *fp = fopen(archive_path, "rb");
    if (!fp) return SELP_ERR_OPEN;
    
    // Lire l'en-tête
    selp_header_t header;
    fread(&header, sizeof(selp_header_t), 1, fp);
    
    // Vérifier la signature
    if (memcmp(header.magic, SELP_MAGIC, 4) != 0) {
        fclose(fp);
        return SELP_ERR_MAGIC;
    }
    
    // Créer le dossier de sortie
    mkdir(output_dir, 0755);
    
    // Extraire chaque fichier
    for (uint64_t i = 0; i < header.file_count; i++) {
        selp_file_entry_t entry;
        fread(&entry, sizeof(selp_file_entry_t), 1, fp);
        
        uint8_t *data = malloc(entry.size);
        fread(data, 1, entry.size, fp);
        
        char output_path[1024];
        snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, entry.path);
        
        FILE *out = fopen(output_path, "wb");
        if (out) {
            fwrite(data, 1, entry.size, out);
            fclose(out);
        }
        
        free(data);
    }
    
    fclose(fp);
    return SELP_OK;
}
