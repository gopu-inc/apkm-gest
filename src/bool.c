#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>

typedef struct {
    char name[128];
    char version[64];
    char release[16];
    char arch[32];
    char maintainer[256];
    char description[512];
    char license[64];
    char url[256];
    char deps[1024];
    char build_cmd[1024];
    char install_cmd[1024];
    char check_cmd[1024];
    char script_path[512];  // $APKMPATH
    int dep_count;
    char** deps_array;
} apkm_build_t;

// Nettoyer une chaîne
void clean_string(char *str) {
    // Enlever guillemets
    int len = strlen(str);
    if (len >= 2 && str[0] == '"' && str[len-1] == '"') {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
    
    // Enlever espaces début/fin
    char *start = str;
    while (*start == ' ' || *start == '\t') start++;
    if (start != str) memmove(str, start, strlen(start) + 1);
    
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
}

// Parser le fichier APKMBUILD
void parse_apkmbuild(const char *filename, apkm_build_t *b) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("[BOOL] Erreur");
        exit(1);
    }

    char line[1024];
    int in_block = 0;
    char current_block[1024] = "";
    
    // Initialisation
    memset(b, 0, sizeof(apkm_build_t));
    strcpy(b->arch, "x86_64");
    strcpy(b->release, "r0");
    strcpy(b->script_path, "install.sh"); // Par défaut
    
    while (fgets(line, sizeof(line), fp)) {
        // Gestion des blocs multilignes
        if (strstr(line, "$APKMMAKE::")) {
            in_block = 1;
            strcpy(current_block, "make");
            char *val = strstr(line, "::") + 2;
            strcpy(b->build_cmd, val);
            continue;
        }
        if (strstr(line, "$APKMINSTALL::")) {
            in_block = 1;
            strcpy(current_block, "install");
            char *val = strstr(line, "::") + 2;
            strcpy(b->install_cmd, val);
            continue;
        }
        if (strstr(line, "$APKMCHECK::")) {
            in_block = 1;
            strcpy(current_block, "check");
            char *val = strstr(line, "::") + 2;
            strcpy(b->check_cmd, val);
            continue;
        }
        
        // Capture des lignes de blocs
        if (in_block) {
            if (strstr(line, "}")) {
                in_block = 0;
            } else {
                if (strcmp(current_block, "make") == 0)
                    strcat(b->build_cmd, line);
                else if (strcmp(current_block, "install") == 0)
                    strcat(b->install_cmd, line);
                else if (strcmp(current_block, "check") == 0)
                    strcat(b->check_cmd, line);
            }
            continue;
        }
        
        // Champs simples
        char *val;
        if ((val = strstr(line, "$APKNAME::"))) {
            strcpy(b->name, val + 10);
            clean_string(b->name);
        }
        else if ((val = strstr(line, "$APKMVERSION::"))) {
            strcpy(b->version, val + 14);
            clean_string(b->version);
        }
        else if ((val = strstr(line, "$APKMRELEASE::"))) {
            strcpy(b->release, val + 14);
            clean_string(b->release);
        }
        else if ((val = strstr(line, "$APKMARCH::"))) {
            strcpy(b->arch, val + 11);
            clean_string(b->arch);
        }
        else if ((val = strstr(line, "$APKMMAINT::"))) {
            strcpy(b->maintainer, val + 12);
            clean_string(b->maintainer);
        }
        else if ((val = strstr(line, "$APKMDESC::"))) {
            strcpy(b->description, val + 11);
            clean_string(b->description);
        }
        else if ((val = strstr(line, "$APKMLICENSE::"))) {
            strcpy(b->license, val + 14);
            clean_string(b->license);
        }
        else if ((val = strstr(line, "$APKMURL::"))) {
            strcpy(b->url, val + 10);
            clean_string(b->url);
        }
        else if ((val = strstr(line, "$APKMDEP::"))) {
            strcpy(b->deps, val + 10);
            clean_string(b->deps);
        }
        else if ((val = strstr(line, "$APKMPATH::"))) {
            strcpy(b->script_path, val + 11);
            clean_string(b->script_path);
        }
    }
    fclose(fp);
}

// Copier un répertoire récursivement
void copy_directory(const char *src, const char *dst) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp -r %s/* %s/ 2>/dev/null || true", src, dst);
    system(cmd);
}

// Créer la structure du paquet
int create_package_structure(apkm_build_t *b, const char *build_dir) {
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/pkg-%s", build_dir, b->name);
    
    // Créer le répertoire du paquet
    mkdir(pkg_dir, 0755);
    
    // Copier tout le projet (sauf build/)
    printf("[BOOL] 📦 Copie des fichiers du projet...\n");
    
    // Copier tous les fichiers sauf build/ et pkg-*/
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "find . -maxdepth 1 -not -name 'build' -not -name 'pkg-*' -not -name '.' -exec cp -r {} %s/ \\;",
             pkg_dir);
    system(cmd);
    
    // S'assurer que le script d'installation est exécutable
    char script_path[512];
    snprintf(script_path, sizeof(script_path), "%s/%s", pkg_dir, b->script_path);
    chmod(script_path, 0755);
    
    return 0;
}

// Builder le paquet
int build_package(apkm_build_t *b) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  BOOL - APKM Package Builder v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    printf("📦 INFORMATIONS DU PAQUET:\n");
    printf("  • Nom        : %s\n", b->name);
    printf("  • Version    : %s-%s\n", b->version, b->release);
    printf("  • Architecture: %s\n", b->arch);
    printf("  • Script path : %s\n", b->script_path);
    
    // Étape 1: Build si commande spécifiée
    if (strlen(b->build_cmd) > 0) {
        printf("\n🔧 ÉTAPE BUILD:\n");
        printf("  • Exécution: %s\n", b->build_cmd);
        if (system(b->build_cmd) != 0) {
            printf("[BOOL] ⚠️  Build non bloquant\n");
        }
    }
    
    // Étape 2: Tests si commande spécifiée
    if (strlen(b->check_cmd) > 0) {
        printf("\n🧪 ÉTAPE TESTS:\n");
        printf("  • Exécution: %s\n", b->check_cmd);
        system(b->check_cmd);
    }
    
    // Étape 3: Création de la structure du paquet
    printf("\n📁 PRÉPARATION DU PAQUET:\n");
    create_package_structure(b, ".");
    
    // Étape 4: Installation si commande spécifiée
    if (strlen(b->install_cmd) > 0) {
        printf("\n⚙️  ÉTAPE INSTALL (simulée):\n");
        printf("  • %s\n", b->install_cmd);
    }
    
    // Étape 5: Création de l'archive finale
    printf("\n📦 CRÉATION DE L'ARCHIVE:\n");
    
    char archive_name[512];
    snprintf(archive_name, sizeof(archive_name), 
             "build/%s-v%s-%s.%s.tar.bool", 
             b->name, b->version, b->release, b->arch);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "cd pkg-%s && tar -czf ../%s * && cd ..", 
             b->name, archive_name);
    
    if (system(cmd) == 0) {
        printf("  ✅ Archive créée: %s\n", archive_name);
        
        // Nettoyage
        snprintf(cmd, sizeof(cmd), "rm -rf pkg-%s", b->name);
        system(cmd);
        
        return 0;
    } else {
        printf("  ❌ Erreur lors de la création de l'archive\n");
        return -1;
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--build") == 0) {
        // Créer le répertoire build s'il n'existe pas
        mkdir("build", 0755);
        
        // Parser APKMBUILD
        apkm_build_t build_info = {0};
        parse_apkmbuild("APKMBUILD", &build_info);
        
        // Builder le paquet
        if (build_package(&build_info) == 0) {
            printf("\n[BOOL] ✅ Build terminé avec succès!\n");
        } else {
            printf("\n[BOOL] ❌ Échec du build\n");
            return 1;
        }
        
    } else {
        printf("Usage: ./bool --build\n");
        printf("Options:\n");
        printf("  --build     Construire le paquet depuis APKMBUILD\n");
        printf("  --help      Afficher cette aide\n");
    }
    return 0;
}
