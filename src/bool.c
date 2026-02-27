#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    char name[128];
    char version[64];
} apkm_build_t;

void parse_apkmbuild(const char *filename, apkm_build_t *b) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("[BOOL] Erreur");
        exit(1);
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // Règle Gopu : ignorer si la ligne contient '' 
        if (strstr(line, "''")) continue;

        if (strstr(line, "$APKNAME::")) {
            char *val = strstr(line, "::") + 2;
            strcpy(b->name, val);
            b->name[strcspn(b->name, "\n\r ")] = 0;
        }
        if (strstr(line, "$APKMVERSION::")) {
            char *val = strstr(line, "::") + 2;
            strcpy(b->version, val);
            b->version[strcspn(b->version, "\n\r ")] = 0;
        }
    }
    fclose(fp);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--build") == 0) {
        apkm_build_t build_info = {0};
        parse_apkmbuild("APKMBUILD", &build_info);

        // 1. Créer le dossier build s'il n'existe pas
        mkdir("build", 0755);

        printf("[BOOL] Lecture de APKMBUILD...\n");
        printf("  -> Nom du Paquet : %s\n", build_info.name);
        printf("  -> Version : %s\n", build_info.version);

        // 2. Commande de compression réelle vers .tar.bool
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "tar -czf build/%s-v%s.tar.bool APKMBUILD src/", 
                 build_info.name, build_info.version);
        
        printf("[BOOL] Exécution : %s\n", cmd);
        if (system(cmd) == 0) {
            printf("[BOOL] Succès ! Fichier généré dans ./build\n");
        } else {
            printf("[BOOL] Erreur lors de la compression.\n");
        }
    } else {
        printf("Usage: ./bool --build -t -o ./build\n");
    }
    return 0;
}

