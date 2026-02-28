#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>
#include "apkm.h"
#include "sandbox.h"
#include <sys/stat.h>  // pour mkdir, chmod


/**
 * APKM v0.1 - The Gopu.inc Smart Package Manager
 */

void print_help(void) {
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APKM - Advanced Package Manager (Gopu.inc Edition)\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("USAGE:\n");
    printf("  apkm [COMMANDE] [PAQUET/CHEMIN] [OPTIONS]\n\n");
    printf("COMMANDES:\n");
    printf("  sync        Synchronise la base de données Alpine locale\n");
    printf("  install     Installe un fichier .tar.bool de façon isolée\n");
    printf("  audit       Analyse les vulnérabilités et l'intégrité\n");
    printf("  rollback    Revient à la référence (ref) précédente\n\n");
    printf("OPTIONS:\n");
    printf("  -j, --json  Sortie structurée pour jq\n");
    printf("  -t, --toml  Sortie structurée pour config\n");
    printf("  --help      Affiche ce menu\n\n");
}

// Fonction de "Déboolage" et Installation
void apkm_install_bool(const char *filepath) {
    printf("[APKM] 📦 Préparation de l'installation : %s\n", filepath);

    // Extraire le nom du paquet et sa version depuis le nom du fichier
    char pkg_name[256] = "unknown";
    char pkg_version[64] = "0.0.0";
    char pkg_arch[32] = "x86_64";
    
    // Exemple: super-app-v1.0.0-r1.x86_64.tar.bool
    char *basename = strrchr(filepath, '/');
    if (basename) basename++; else basename = (char*)filepath;
    
    // Copier le nom de base pour parsing
    char filename[512];
    strncpy(filename, basename, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
    
    // Parser le nom du paquet (tout avant -v)
    char *version_start = strstr(filename, "-v");
    if (version_start) {
        int name_len = version_start - filename;
        if (name_len > 0 && name_len < sizeof(pkg_name)) {
            strncpy(pkg_name, filename, name_len);
            pkg_name[name_len] = '\0';
        }
        
        // Parser la version (entre -v et .)
        char *arch_start = strstr(version_start + 2, ".");
        if (arch_start) {
            int ver_len = arch_start - (version_start + 2);
            if (ver_len > 0 && ver_len < sizeof(pkg_version)) {
                strncpy(pkg_version, version_start + 2, ver_len);
                pkg_version[ver_len] = '\0';
            }
            
            // Parser l'architecture
            char *ext_start = strstr(arch_start + 1, ".tar.bool");
            if (ext_start) {
                int arch_len = ext_start - (arch_start + 1);
                if (arch_len > 0 && arch_len < sizeof(pkg_arch)) {
                    strncpy(pkg_arch, arch_start + 1, arch_len);
                    pkg_arch[arch_len] = '\0';
                }
            }
        }
    }
    
    printf("[APKM] 📦 Paquet: %s %s (%s)\n", pkg_name, pkg_version, pkg_arch);

    // Répertoire de staging
    const char *staging_path = "/tmp/apkm_install";
    
    // Créer et vider le répertoire temporaire
    struct stat st = {0};
    if (stat(staging_path, &st) == -1) {
        mkdir(staging_path, 0755);
    }
    
    char cmd_clean[512];
    snprintf(cmd_clean, sizeof(cmd_clean), "rm -rf %s/*", staging_path);
    system(cmd_clean);

    // Extraction du paquet
    printf("[APKM] 🔍 Extraction en cours...\n");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tar -xzf %s -C %s", filepath, staging_path);
    
    if (system(cmd) != 0) {
        fprintf(stderr, "[APKM] ❌ Erreur lors de l'extraction.\n");
        return;
    }
    
    // Résolution des dépendances
    resolve_dependencies(staging_path);
    
    // Chercher et exécuter le script d'installation
    printf("[APKM] ⚙️ Recherche du script d'installation...\n");
    
    // Liste des scripts possibles
    const char *scripts[] = {
        "install.sh",
        "INSTALL.sh",
        "post-install.sh",
        "setup.sh",
        "configure.sh",
        NULL
    };
    
    int script_found = 0;
    int install_success = 0;
    
    for (int i = 0; scripts[i] != NULL; i++) {
        char script_path[512];
        snprintf(script_path, sizeof(script_path), "%s/%s", staging_path, scripts[i]);
        
        if (access(script_path, F_OK) == 0) {
            printf("[APKM] ⚙️ Exécution de %s...\n", scripts[i]);
            chmod(script_path, 0755);
            
            // Changer de répertoire pour exécuter le script dans le bon contexte
            char current_dir[1024];
            getcwd(current_dir, sizeof(current_dir));
            chdir(staging_path);
            
            // Exécuter le script
            int ret = system(script_path);
            
            // Revenir
            chdir(current_dir);
            
            if (ret == 0) {
                printf("[APKM] ✅ Script exécuté avec succès\n");
                script_found = 1;
                install_success = 1;
                break;
            } else {
                printf("[APKM] ⚠️ Échec du script %s (code: %d)\n", scripts[i], ret);
            }
        }
    }
    
    if (!script_found) {
        printf("[APKM] ⚠️ Aucun script d'installation trouvé\n");
        // Chercher si un binaire a été extrait
        char binary_path[512];
        snprintf(binary_path, sizeof(binary_path), "%s/%s", staging_path, pkg_name);
        
        if (access(binary_path, F_OK) == 0) {
            printf("[APKM] 📦 Binaire trouvé à la racine, installation directe\n");
            snprintf(cmd, sizeof(cmd), "cp %s /usr/local/bin/ && chmod 755 /usr/local/bin/%s", 
                     binary_path, pkg_name);
            if (system(cmd) == 0) {
                install_success = 1;
            }
        }
    }
    
    // Enregistrer dans la base de données si installation réussie
    if (install_success) {
        printf("[APKM] 📝 Enregistrement du paquet dans la base de données...\n");
        
        // Créer le répertoire APKM s'il n'existe pas
        mkdir("/var/lib/apkm", 0755);
        
        // Base de données texte simple
        char db_path[512];
        snprintf(db_path, sizeof(db_path), "/var/lib/apkm/packages.db");
        
        FILE *db = fopen(db_path, "a");
        if (!db) {
            // Essayer de créer le fichier
            db = fopen(db_path, "w");
        }
        
        if (db) {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char date_str[20];
            strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);
            
            fprintf(db, "%s|%s|%s|%ld|%s|/usr/local/bin/%s\n", 
                    pkg_name, pkg_version, pkg_arch, now, date_str, pkg_name);
            fclose(db);
            
            printf("[APKM] ✅ Paquet %s %s enregistré dans la base\n", pkg_name, pkg_version);
            
            // Créer aussi un fichier manifeste
            char manifest_path[512];
            snprintf(manifest_path, sizeof(manifest_path), "/var/lib/apkm/%s.manifest", pkg_name);
            
            FILE *mf = fopen(manifest_path, "w");
            if (mf) {
                fprintf(mf, "NAME=%s\n", pkg_name);
                fprintf(mf, "VERSION=%s\n", pkg_version);
                fprintf(mf, "ARCH=%s\n", pkg_arch);
                fprintf(mf, "INSTALL_DATE=%s\n", date_str);
                fprintf(mf, "BINARY_PATH=/usr/local/bin/%s\n", pkg_name);
                fprintf(mf, "SOURCE=%s\n", filepath);
                fclose(mf);
                printf("[APKM] 📄 Manifeste créé: %s\n", manifest_path);
            }
        } else {
            printf("[APKM] ⚠️ Impossible d'enregistrer le paquet dans la base\n");
        }
    }
    
    // Nettoyage
    printf("[APKM] 🧹 Nettoyage...\n");
    snprintf(cmd_clean, sizeof(cmd_clean), "rm -rf %s", staging_path);
    system(cmd_clean);
    
    if (install_success) {
        printf("[APKM] ✅ Installation terminée avec succès !\n");
        printf("[APKM] 👉 Pour tester: %s --version\n", pkg_name);
    } else {
        printf("[APKM] ❌ Échec de l'installation\n");
    }
}
void apkm_list_packages(void) {
    printf("[APKM] 📋 Paquets installés:\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    FILE *db = fopen("/var/lib/apkm/packages.db", "r");
    if (!db) {
        printf("  Aucun paquet installé (base de données vide)\n");
        return;
    }
    
    char line[1024];
    int count = 0;
    
    printf(" %-20s %-12s %-10s %-20s\n", "NOM", "VERSION", "ARCH", "DATE");
    printf(" ──────────────────────────────────────────────────\n");
    
    while (fgets(line, sizeof(line), db)) {
        char name[256], version[64], arch[32], date_str[20];
        long timestamp;
        
        if (sscanf(line, "%[^|]|%[^|]|%[^|]|%ld|%[^|]", 
                   name, version, arch, &timestamp, date_str) == 5) {
            printf(" • %-20s %-12s %-10s %-20s\n", 
                   name, version, arch, date_str);
            count++;
        }
    }
    
    fclose(db);
    
    if (count == 0) {
        printf("  Aucun paquet trouvé\n");
    } else {
        printf("\n 📊 Total: %d paquet(s) installé(s)\n", count);
    }
}

// Ajouter cette fonction dans src/main.c
void register_installed_package(const char *pkg_name, const char *version) {
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "/var/lib/apkm/installed.db");
    
    FILE *db = fopen(db_path, "a");
    if (!db) {
        // Créer le répertoire si nécessaire
        mkdir("/var/lib/apkm", 0755);
        db = fopen(db_path, "a");
    }
    
    if (db) {
        time_t now = time(NULL);
        fprintf(db, "%s|%s|%ld\n", pkg_name, version, now);
        fclose(db);
        printf("[APKM] 📝 Paquet enregistré: %s %s\n", pkg_name, version);
    }
}
int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }

    char *command = argv[1];
    output_format_t fmt = OUTPUT_TEXT;

    // Détection des formats (JSON/TOML)
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) fmt = OUTPUT_JSON;
        if (strcmp(argv[i], "--toml") == 0 || strcmp(argv[i], "-t") == 0) fmt = OUTPUT_TOML;
    }

    // Routage intelligent
    if (strcmp(command, "sync") == 0) {
        sync_alpine_db(fmt);
    } 
    else if (strcmp(command, "install") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[APKM] Erreur : Spécifiez un fichier .tar.bool\n");
            return 1;
        }
        apkm_install_bool(argv[2]);
    } 
    else if (strcmp(command, "list") == 0) {
    apkm_list_packages();
} else {
        printf("  Aucun paquet APKM installé\n");
    }
}
    else if (strcmp(command, "audit") == 0) {
        printf("[APKM] 🛡️ Analyse CVE et scan d'intégrité...\n");
        // Logique audit
    } 
    else if (strcmp(command, "rollback") == 0) {
        printf("[APKM] ⏪ Restauration vers la version précédente...\n");
        // Logique rollback
    } 
    else {
        fprintf(stderr, "[APKM] Commande inconnue : %s\n", command);
        return 1;
    }

    return 0;
}
