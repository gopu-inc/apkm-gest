#ifndef SELP_H
#define SELP_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define SELP_MAGIC "SELP"
#define SELP_VERSION 1

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

// Structure d'en-tête SELP
typedef struct {
    char magic[4];
    uint8_t version;
    uint8_t compression;
    uint8_t encryption;
    uint8_t flags;
    uint64_t original_size;
    uint64_t compressed_size;
    uint64_t file_count;
    uint32_t checksum;
    uint32_t signature[8];
    time_t timestamp;
    char comment[256];
} selp_header_t;

// Codes d'erreur
#define SELP_OK              0
#define SELP_ERR_OPEN        -1
#define SELP_ERR_READ        -2
#define SELP_ERR_WRITE       -3
#define SELP_ERR_MAGIC       -4
#define SELP_ERR_MEMORY      -5
#define SELP_ERR_COMPRESS    -6
#define SELP_ERR_DECOMPRESS  -7
#define SELP_ERR_CRC         -8
#define SELP_ERR_SIGNATURE   -9

// Prototypes
int selp_compress(const char *input, const char *output, int level, int crypt);
int selp_decompress(const char *input, const char *output);
int selp_create_archive(const char *dir, const char *output, int level, int crypt);
int selp_extract_archive(const char *archive, const char *output);
int selp_verify(const char *path);

#endif
