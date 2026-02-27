#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../include/apkm.h"

// Vérifie si un paquet spécifique est déjà sur le système Alpine
int is_dep_installed(const char *pkg_name) {
    FILE *fp = fopen(ALPINE_DB_PATH, "r");
    if (!fp) return 0;

    char line[1024];
    char search_pattern[130];
    snprintf(search_pattern, sizeof(search_pattern), "P:%s\n", pkg_name);

    while (fgets(line, sizeof(line), fp)) {
        if (strcmp(line, search_pattern) == 0) {
            fclose(fp);
            return 1; // Trouvé !
        }
    }
    fclose(fp);
    return 0; // Manquant
}

// Analyse le bloc $APKMDEP du manifeste
void resolve_dependencies(const char *staging_path) {
    printf("[APKM] 🧠 Analyse des dépendances...\n");
    
    char build_file[512];
    snprintf(build_file, sizeof(build_file), "%s/APKMBUILD", staging_path);
    
    FILE *fp = fopen(build_file, "r");
    if (!fp) return;

    char line[1024];
    int in_dep_block = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "$APKMDEP")) in_dep_block = 1;
        if (in_dep_block && strstr(line, "}")) in_dep_block = 0;

        if (in_dep_block && strstr(line, ";")) {
            char *dep = strtok(line, "; \t\n");
            while (dep) {
                if (dep[0] != '{' && dep[0] != '$') {
                    if (is_dep_installed(dep)) {
                        printf("  [OK] %s est déjà présent.\n", dep);
                    } else {
                        printf("  [!] %s est manquant. Installation requise.\n", dep);
                        // Ici, on pourrait appeler 'apk add' ou 'apkm install'
                    }
                }
                dep = strtok(NULL, "; \t\n");
            }
        }
    }
    fclose(fp);
}

