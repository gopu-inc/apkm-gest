#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <time.h>

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
    char script_path[512];
    char includes[512];      // Répertoire des includes
    char libs[512];          // Répertoire des libs
    char pkgconfig[512];     // Fichiers .pc
    int dep_count;
    char** deps_array;
} apkm_build_t;

// Nettoyer une chaîne
void clean_string(char *str) {
    int len = strlen(str);
    if (len >= 2 && str[0] == '"' && str[len-1] == '"') {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
    
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
    strcpy(b->script_path, "install.sh");
    strcpy(b->includes, "include");
    strcpy(b->libs, "lib");
    strcpy(b->pkgconfig, "lib/pkgconfig");
    
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
        else if ((val = strstr(line, "$APKMINCLUDES::"))) {
            strcpy(b->includes, val + 15);
            clean_string(b->includes);
        }
        else if ((val = strstr(line, "$APKMLIBS::"))) {
            strcpy(b->libs, val + 11);
            clean_string(b->libs);
        }
        else if ((val = strstr(line, "$APKMPKGCONFIG::"))) {
            strcpy(b->pkgconfig, val + 16);
            clean_string(b->pkgconfig);
        }
    }
    fclose(fp);
}

// Créer la structure complète du paquet avec includes et libs
int create_package_structure(apkm_build_t *b, const char *build_dir) {
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/pkg-%s", build_dir, b->name);
    
    // Créer les répertoires standards
    mkdir(pkg_dir, 0755);
    mkdir(pkg_dir, 0755);
    
    char path[1024];
    
    // Créer la structure complète FHS (Filesystem Hierarchy Standard)
    snprintf(path, sizeof(path), "%s/usr", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/bin", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/lib", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/include", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/share", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/share/doc", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/share/man", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/share/man/man1", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/lib/pkgconfig", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/etc", pkg_dir);
    mkdir(path, 0755);
    
    printf("[BOOL] 📦 Copie des fichiers du projet...\n");
    
    // Copier tous les fichiers (sauf build/ et pkg-*/)
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), 
             "find . -maxdepth 1 -not -name 'build' -not -name 'pkg-*' -not -name '.' -exec cp -r {} %s/ \\;",
             pkg_dir);
    system(cmd);
    
    // Si des includes spécifiques sont définis, les copier
    if (strlen(b->includes) > 0 && strcmp(b->includes, "include") != 0) {
        printf("[BOOL] 📚 Copie des includes depuis %s...\n", b->includes);
        snprintf(cmd, sizeof(cmd), "cp -r %s/* %s/usr/include/ 2>/dev/null || true", 
                 b->includes, pkg_dir);
        system(cmd);
    }
    
    // Si des libs spécifiques sont définies, les copier
    if (strlen(b->libs) > 0 && strcmp(b->libs, "lib") != 0) {
        printf("[BOOL] 📚 Copie des librairies depuis %s...\n", b->libs);
        snprintf(cmd, sizeof(cmd), "cp -r %s/*.a %s/usr/lib/ 2>/dev/null || true", 
                 b->libs, pkg_dir);
        snprintf(cmd + strlen(cmd), sizeof(cmd) - strlen(cmd), 
                 "cp -r %s/*.so* %s/usr/lib/ 2>/dev/null || true", b->libs, pkg_dir);
        system(cmd);
    }
    
    // Copier les fichiers .pc (pkgconfig)
    if (strlen(b->pkgconfig) > 0) {
        printf("[BOOL] 📚 Copie des fichiers pkgconfig...\n");
        snprintf(cmd, sizeof(cmd), "cp -r %s/*.pc %s/usr/lib/pkgconfig/ 2>/dev/null || true", 
                 b->pkgconfig, pkg_dir);
        system(cmd);
    }
    
    // Rendre le script d'installation exécutable
    char script_path[512];
    snprintf(script_path, sizeof(script_path), "%s/%s", pkg_dir, b->script_path);
    chmod(script_path, 0755);
    
    return 0;
}

// Générer un fichier .pc (pkg-config)
void generate_pc_file(apkm_build_t *b, const char *pkg_dir) {
    char pc_path[1024];
    snprintf(pc_path, sizeof(pc_path), "%s/usr/lib/pkgconfig/%s.pc", pkg_dir, b->name);
    
    FILE *f = fopen(pc_path, "w");
    if (!f) return;
    
    fprintf(f, "prefix=/usr\n");
    fprintf(f, "exec_prefix=${prefix}\n");
    fprintf(f, "libdir=${exec_prefix}/lib\n");
    fprintf(f, "includedir=${prefix}/include\n\n");
    
    fprintf(f, "Name: %s\n", b->name);
    fprintf(f, "Description: %s\n", b->description);
    fprintf(f, "Version: %s\n", b->version);
    fprintf(f, "Cflags: -I${includedir}\n");
    fprintf(f, "Libs: -L${libdir} -l%s\n", b->name);
    
    fclose(f);
    printf("[BOOL] 📄 Fichier pkgconfig généré: %s.pc\n", b->name);
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
    printf("  • Includes    : %s\n", b->includes);
    printf("  • Librairies  : %s\n", b->libs);
    
    // Étape 1: Build
    if (strlen(b->build_cmd) > 0) {
        printf("\n🔧 ÉTAPE BUILD:\n");
        printf("  • Exécution: %s\n", b->build_cmd);
        if (system(b->build_cmd) != 0) {
            printf("[BOOL] ⚠️  Build non bloquant\n");
        }
    }
    
    // Étape 2: Tests
    if (strlen(b->check_cmd) > 0) {
        printf("\n🧪 ÉTAPE TESTS:\n");
        printf("  • Exécution: %s\n", b->check_cmd);
        system(b->check_cmd);
    }
    
    // Étape 3: Création de la structure du paquet
    printf("\n📁 PRÉPARATION DU PAQUET:\n");
    create_package_structure(b, ".");
    
    // Générer le fichier pkgconfig
    generate_pc_file(b, "pkg-" b->name);
    
    // Étape 4: Installation dans DESTDIR (simulée si commande spécifiée)
    if (strlen(b->install_cmd) > 0) {
        printf("\n⚙️  ÉTAPE INSTALL (DESTDIR):\n");
        
        // Sauvegarder la variable d'environnement
        char old_pkgdir[1024] = "";
        char *old = getenv("DESTDIR");
        if (old) strcpy(old_pkgdir, old);
        
        // Définir DESTDIR pour l'installation
        char destdir[512];
        snprintf(destdir, sizeof(destdir), "pkg-%s", b->name);
        setenv("DESTDIR", destdir, 1);
        
        printf("  • DESTDIR=%s\n", destdir);
        printf("  • Commande: %s\n", b->install_cmd);
        
        if (system(b->install_cmd) != 0) {
            printf("[BOOL] ⚠️  Installation non bloquante\n");
        }
        
        // Restaurer l'ancienne valeur
        if (strlen(old_pkgdir) > 0) {
            setenv("DESTDIR", old_pkgdir, 1);
        } else {
            unsetenv("DESTDIR");
        }
    }
    
    // Étape 5: Création de l'archive finale
    printf("\n📦 CRÉATION DE L'ARCHIVE:\n");
    
    char archive_name[512];
    snprintf(archive_name, sizeof(archive_name), 
             "build/%s-v%s-%s.%s.tar.bool", 
             b->name, b->version, b->release, b->arch);
    
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), 
             "cd pkg-%s && tar -czf ../%s * && cd ..", 
             b->name, archive_name);
    
    if (system(cmd) == 0) {
        // Obtenir la taille du fichier
        struct stat st;
        stat(archive_name, &st);
        printf("  ✅ Archive créée: %s (%.2f KB)\n", 
               archive_name, st.st_size / 1024.0);
        
        // Générer le manifeste
        char manifest[512];
        snprintf(manifest, sizeof(manifest), "build/%s.manifest.json", b->name);
        FILE *mf = fopen(manifest, "w");
        if (mf) {
            fprintf(mf, "{\n");
            fprintf(mf, "  \"name\": \"%s\",\n", b->name);
            fprintf(mf, "  \"version\": \"%s\",\n", b->version);
            fprintf(mf, "  \"release\": \"%s\",\n", b->release);
            fprintf(mf, "  \"architecture\": \"%s\",\n", b->arch);
            fprintf(mf, "  \"maintainer\": \"%s\",\n", b->maintainer);
            fprintf(mf, "  \"description\": \"%s\",\n", b->description);
            fprintf(mf, "  \"license\": \"%s\",\n", b->license);
            fprintf(mf, "  \"size\": %ld,\n", st.st_size);
            fprintf(mf, "  \"includes\": [");
            
            // Lister les includes
            DIR *dir = opendir("include");
            if (dir) {
                struct dirent *entry;
                int first = 1;
                while ((entry = readdir(dir)) != NULL) {
                    if (entry->d_name[0] != '.') {
                        if (!first) fprintf(mf, ",");
                        fprintf(mf, "\n    \"%s\"", entry->d_name);
                        first = 0;
                    }
                }
                closedir(dir);
            }
            fprintf(mf, "\n  ]\n");
            fprintf(mf, "}\n");
            fclose(mf);
            printf("  📄 Manifeste généré: %s\n", manifest);
        }
        
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
        mkdir("build", 0755);
        
        apkm_build_t build_info = {0};
        parse_apkmbuild("APKMBUILD", &build_info);
        
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
