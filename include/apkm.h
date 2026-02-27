#ifndef APKM_H
#define APKM_H

#include <stdio.h>

#define ALPINE_DB_PATH "/lib/apk/db/installed"

// Formats de sortie pris en charge par gopu.inc
typedef enum {
    OUTPUT_TEXT,
    OUTPUT_JSON,
    OUTPUT_TOML
} output_format_t;

// Prototypes des fonctions
void sync_alpine_db(output_format_t format);

char* get_gopu_token();
void gopu_crypt(char *data);

#endif

