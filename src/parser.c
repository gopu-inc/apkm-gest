#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apkm.h"

void sync_alpine_db(output_format_t format) {
    FILE *fp = fopen(ALPINE_DB_PATH, "r");
    if (!fp) return;

    char line[1024];
    char current_name[128] = "";
    char current_ver[64] = "";
    int first = 1;

    if (format == OUTPUT_JSON) printf("[\n");
    else if (format == OUTPUT_TOML) printf("[packages]\n");

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "P:", 2) == 0) {
            strcpy(current_name, line + 2);
            current_name[strcspn(current_name, "\n")] = 0;
        }
        if (strncmp(line, "V:", 2) == 0) {
            strcpy(current_ver, line + 2);
            current_ver[strcspn(current_ver, "\n")] = 0;
            
            // Une fois qu'on a le nom ET la version, on affiche
            if (format == OUTPUT_JSON) {
                if (!first) printf(",\n");
                printf("  { \"package\": \"%s\", \"version\": \"%s\" }", current_name, current_ver);
                first = 0;
            } else if (format == OUTPUT_TOML) {
                printf("[[package]]\nname = \"%s\"\nversion = \"%s\"\n\n", current_name, current_ver);
            } else {
                printf("Paquet : %-20s | Version : %s\n", current_name, current_ver);
            }
        }
    }

    if (format == OUTPUT_JSON) printf("\n]\n");
    fclose(fp);
}

