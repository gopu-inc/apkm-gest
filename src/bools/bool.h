#ifndef BOOL_H
#define BOOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#define BOOL_VERSION "2.1.0"
#define SELP_MAGIC "SELP"
#define SELP_VERSION 1
#define MAX_FILES 4096
#define MAX_PATH 1024
#define MAX_COMMENT 256
#define MAX_AUTHOR 128

// Types de compression
#define SELP_COMPRESS_NONE  0
#define SELP_COMPRESS_FAST   1
#define SELP_COMPRESS_BEST   2
#define SELP_COMPRESS_ULTRA  3

// Niveaux de chiffrement
#define SELP_CRYPT_NONE   0
#define SELP_CRYPT_LIGHT  1
#define SELP_CRYPT_MEDIUM 2
#define SELP_CRYPT_STRONG 3

// Structure d'en-tête SELP (version étendue)
typedef struct {
    char magic[4];              // "SELP"
    uint8_t version;            // Version du format
    uint8_t compression;        // Type de compression
    uint8_t encryption;         // Type de chiffrement
    uint8_t flags;              // Flags divers
    uint64_t original_size;      // Taille originale totale
    uint64_t compressed_size;    // Taille compressée totale
    uint64_t file_count;         // Nombre de fichiers
    uint32_t checksum;           // Checksum simple
    uint32_t signature[8];       // Signature 256-bit
    time_t timestamp;            // Timestamp
    char author[MAX_AUTHOR];     // Auteur
    char comment[MAX_COMMENT];   // Commentaire
} selp_header_t;

// Structure d'entrée de fichier (améliorée)
typedef struct {
    char path[MAX_PATH];         // Chemin complet
    char name[256];              // Nom du fichier
    uint64_t size;                // Taille du fichier
    uint32_t crc32;               // CRC32 du fichier
    uint8_t hash[32];             // SHA256 du fichier
    uint32_t permissions;         // Permissions Unix
    time_t mtime;                 // Modification time
    uint64_t offset;               // Offset dans l'archive
} selp_file_entry_t;

// Structure de contexte
typedef struct {
    FILE *fp;
    selp_header_t header;
    selp_file_entry_t *files;
    uint64_t current_pos;
    char error[256];
    int verbose;
} selp_ctx_t;

// Codes d'erreur
#define SELP_OK              0
#define SELP_ERR_OPEN        -1
#define SELP_ERR_READ        -2
#define SELP_ERR_WRITE       -3
#define SELP_ERR_MAGIC       -4
#define SELP_ERR_VERSION     -5
#define SELP_ERR_CHECKSUM    -6
#define SELP_ERR_SIGNATURE   -7
#define SELP_ERR_MEMORY      -8
#define SELP_ERR_COMPRESS    -9
#define SELP_ERR_DECOMPRESS  -10
#define SELP_ERR_CRYPTO      -11
#define SELP_ERR_NOT_FOUND   -12
#define SELP_ERR_PERMISSION  -13

// Prototypes
int selp_compress_files(int argc, char **argv, const char *output, 
                        int level, int crypt, const char *author, 
                        const char *comment);
int selp_compress_directory(const char *dir, const char *output,
                           int level, int crypt, const char *author,
                           const char *comment, int follow_links);
int selp_extract(const char *archive, const char *output_dir);
int selp_list(const char *archive);
int selp_verify(const char *archive);
int selp_info(const char *archive);

#endif
