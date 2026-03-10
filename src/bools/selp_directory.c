#include "bool.h"
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>

// Structure pour parcourir les dossiers
typedef struct {
    char path[MAX_PATH];
    struct stat st;
} file_info_t;

// Fonction récursive pour parcourir un dossier
static int scan_directory(const char *dir, file_info_t **files, 
                          int *count, int follow_links) {
    DIR *dp = opendir(dir);
    if (!dp) return SELP_ERR_OPEN;
    
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL && *count < MAX_FILES) {
        // Ignorer . et ..
        if (strcmp(entry->d_name, ".") == 0 || 
            strcmp(entry->d_name, "..") == 0) continue;
        
        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);
        
        struct stat st;
        int stat_result = follow_links ? stat(full_path, &st) : lstat(full_path, &st);
        
        if (stat_result == 0) {
            if (S_ISDIR(st.st_mode)) {
                // Scanner récursivement le sous-dossier
                scan_directory(full_path, files, count, follow_links);
            } else if (S_ISREG(st.st_mode)) {
                // Ajouter le fichier à la liste
                file_info_t *fi = malloc(sizeof(file_info_t));
                if (fi) {
                    strcpy(fi->path, full_path);
                    fi->st = st;
                    files[*count] = fi;
                    (*count)++;
                    
                    if (follow_links) {
                        printf("📄 Added: %s (%ld bytes)\n", full_path, st.st_size);
                    } else {
                        printf("📄 Added: %s (%ld bytes) [no follow]\n", full_path, st.st_size);
                    }
                }
            }
        }
    }
    
    closedir(dp);
    return SELP_OK;
}

// Fonction principale de compression de dossier
int selp_compress_directory(const char *dir, const char *output,
                           int level, int crypt, const char *author,
                           const char *comment, int follow_links) {
    printf("📁 Scanning directory: %s\n", dir);
    
    // Scanner le dossier
    file_info_t *files[MAX_FILES];
    int file_count = 0;
    
    int result = scan_directory(dir, files, &file_count, follow_links);
    if (result != SELP_OK || file_count == 0) {
        printf("❌ No files found in directory\n");
        return SELP_ERR_NOT_FOUND;
    }
    
    printf("✅ Found %d files\n", file_count);
    
    // Calculer la taille totale
    uint64_t total_size = 0;
    for (int i = 0; i < file_count; i++) {
        total_size += files[i]->st.st_size;
    }
    
    printf("📊 Total size: %.2f KB\n", total_size / 1024.0);
    
    // Créer l'en-tête
    selp_header_t header;
    memcpy(header.magic, SELP_MAGIC, 4);
    header.version = SELP_VERSION;
    header.compression = level;
    header.encryption = crypt;
    header.flags = follow_links ? 0x01 : 0x00;
    header.original_size = total_size;
    header.compressed_size = 0;
    header.file_count = file_count;
    header.timestamp = time(NULL);
    strncpy(header.author, author ? author : "Unknown", MAX_AUTHOR - 1);
    strncpy(header.comment, comment ? comment : "BOOL SELP Archive", MAX_COMMENT - 1);
    
    // Ouvrir le fichier de sortie
    FILE *out = fopen(output, "wb");
    if (!out) {
        printf("❌ Cannot create output file\n");
        return SELP_ERR_OPEN;
    }
    
    // Écrire l'en-tête (à mettre à jour plus tard)
    fwrite(&header, sizeof(selp_header_t), 1, out);
    
    // Table des offsets pour les entrées
    uint64_t *offsets = malloc(file_count * sizeof(uint64_t));
    
    // Écrire les entrées de fichiers
    for (int i = 0; i < file_count; i++) {
        selp_file_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        
        strncpy(entry.path, files[i]->path, MAX_PATH - 1);
        
        // Extraire le nom du fichier
        char *base = strrchr(files[i]->path, '/');
        if (base) base++; else base = files[i]->path;
        strncpy(entry.name, base, sizeof(entry.name) - 1);
        
        entry.size = files[i]->st.st_size;
        entry.permissions = files[i]->st.st_mode;
        entry.mtime = files[i]->st.st_mtime;
        entry.offset = ftell(out) + sizeof(selp_file_entry_t) * (file_count - i - 1);
        offsets[i] = entry.offset;
        
        fwrite(&entry, sizeof(selp_file_entry_t), 1, out);
    }
    
    // Écrire les données des fichiers
    for (int i = 0; i < file_count; i++) {
        FILE *in = fopen(files[i]->path, "rb");
        if (!in) continue;
        
        printf("📦 Writing: %s\n", files[i]->path);
        
        uint8_t buffer[8192];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), in)) > 0) {
            fwrite(buffer, 1, bytes, out);
        }
        fclose(in);
        
        free(files[i]);
    }
    
    // Mettre à jour l'en-tête avec la taille compressée
    header.compressed_size = ftell(out) - sizeof(selp_header_t);
    fseek(out, 0, SEEK_SET);
    fwrite(&header, sizeof(selp_header_t), 1, out);
    
    fclose(out);
    free(offsets);
    
    printf("\n✅ Directory compressed successfully!\n");
    printf("   Input:  %s (%d files, %.2f KB)\n", dir, file_count, total_size / 1024.0);
    printf("   Output: %s (%.2f KB)\n", output, header.compressed_size / 1024.0);
    printf("   Ratio:  %.1f%%\n", 100.0 * header.compressed_size / total_size);
    
    return SELP_OK;
}

// Fonction d'extraction avec reconstruction de l'arborescence
int selp_extract(const char *archive, const char *output_dir) {
    printf("📂 Extracting: %s\n", archive);
    
    FILE *in = fopen(archive, "rb");
    if (!in) return SELP_ERR_OPEN;
    
    // Lire l'en-tête
    selp_header_t header;
    if (fread(&header, sizeof(selp_header_t), 1, in) != 1) {
        fclose(in);
        return SELP_ERR_READ;
    }
    
    // Vérifier le magic
    if (memcmp(header.magic, SELP_MAGIC, 4) != 0) {
        fclose(in);
        return SELP_ERR_MAGIC;
    }
    
    printf("📦 Archive contains %llu files\n", (unsigned long long)header.file_count);
    
    // Lire les entrées
    selp_file_entry_t *entries = malloc(header.file_count * sizeof(selp_file_entry_t));
    fread(entries, sizeof(selp_file_entry_t), header.file_count, in);
    
    // Créer le dossier de sortie
    mkdir(output_dir, 0755);
    
    // Extraire chaque fichier
    for (uint64_t i = 0; i < header.file_count; i++) {
        // Construire le chemin de sortie
        char out_path[MAX_PATH];
        const char *relative = entries[i].path;
        
        // Enlever le chemin absolu si présent
        if (relative[0] == '/') relative++;
        
        snprintf(out_path, sizeof(out_path), "%s/%s", output_dir, relative);
        
        // Créer les sous-dossiers nécessaires
        char *last_slash = strrchr(out_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkdir(out_path, 0755);
            *last_slash = '/';
        }
        
        printf("📄 Extracting: %s\n", relative);
        
        // Lire les données
        uint8_t *data = malloc(entries[i].size);
        fread(data, 1, entries[i].size, in);
        
        // Écrire le fichier
        FILE *out = fopen(out_path, "wb");
        if (out) {
            fwrite(data, 1, entries[i].size, out);
            fclose(out);
            
            // Restaurer les permissions
            chmod(out_path, entries[i].permissions);
            
            // Restaurer le timestamp
            struct timespec times[2] = {
                {.tv_sec = entries[i].mtime, .tv_nsec = 0},
                {.tv_sec = entries[i].mtime, .tv_nsec = 0}
            };
            utimensat(AT_FDCWD, out_path, times, 0);
        }
        
        free(data);
    }
    
    fclose(in);
    free(entries);
    
    printf("\n✅ Extraction complete!\n");
    printf("   %llu files extracted to %s\n", 
           (unsigned long long)header.file_count, output_dir);
    
    return SELP_OK;
}
