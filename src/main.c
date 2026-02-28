#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "apkm.h"
#include "sandbox.h"

/**
 * APKM v2.0 - The Gopu.inc Smart Package Manager
 */

void print_help(void) {
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  APKM - Advanced Package Manager (Gopu.inc Edition) v2.0\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("USAGE:\n");
    printf("  apkm [COMMANDE] [PAQUET/CHEMIN] [OPTIONS]\n\n");
    printf("COMMANDES:\n");
    printf("  sync        Synchronise la base de données Alpine locale\n");
    printf("  install     Installe un fichier .tar.bool de façon isolée\n");
    printf("  list        Liste les paquets installés\n");
    printf("  audit       Analyse les vulnérabilités et l'intégrité\n");
    printf("  rollback    Revient à la référence (ref) précédente\n");
    printf("  register    Enregistre manuellement un paquet\n\n");
    printf("OPTIONS:\n");
    printf("  -j, --json  Sortie structurée pour jq\n");
    printf("  -t, --toml  Sortie structurée pour config\n");
    printf("  --help      Affiche ce menu\n\n");
}

void register_installed_package(const char *pkg_name, const char *version, const char *arch) {
    // Créer le répertoire APKM s'il n'existe pas
    mkdir("/var/lib/apkm", 0755);
    
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "/var/lib/apkm/packages.db");
    
    FILE *db = fopen(db_path, "a");
    if (!db) {
        db = fopen(db_path, "w");
    }
    
    if (db) {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char date_str[20];
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        // Corriger le warning format: utiliser %lld pour time_t
        fprintf(db, "%s|%s|%s|%lld|%s|/usr/local/bin/%s\n", 
                pkg_name, version, arch, (long long)now, date_str, pkg_name);
        fclose(db);
        
        printf("[APKM] ✅ Paquet %s %s enregistré dans la base\n", pkg_name, version);
        
        // Créer aussi un fichier manifeste
        char manifest_path[512];
        snprintf(manifest_path, sizeof(manifest_path), "/var/lib/apkm/%s.manifest", pkg_name);
        
        FILE *mf = fopen(manifest_path, "w");
        if (mf) {
            fprintf(mf, "NAME=%s\n", pkg_name);
            fprintf(mf, "VERSION=%s\n", version);
            fprintf(mf, "ARCH=%s\n", arch);
            fprintf(mf, "INSTALL_DATE=%s\n", date_str);
            fprintf(mf, "BINARY_PATH=/usr/local/bin/%s\n", pkg_name);
            fclose(mf);
        }
    } else {
        printf("[APKM] ⚠️ Impossible d'enregistrer le paquet dans la base\n");
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
        char name[256] = "";
        char version[64] = "";
        char arch[32] = "";
        char date_str[20] = "";
        long long timestamp = 0;
        char binary[512] = "";
        
        // Format: nom|version|arch|timestamp|date|binary
        int parsed = sscanf(line, "%255[^|]|%63[^|]|%31[^|]|%lld|%19[^|]|%511[^\n]", 
                            name, version, arch, &timestamp, date_str, binary);
        
        if (parsed >= 5) {
            printf(" • %-20s %-12s %-10s %-20s\n", name, version, arch, date_str);
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
        int name_len = (int)(version_start - filename);
        if (name_len > 0 && (size_t)name_len < sizeof(pkg_name)) {
            strncpy(pkg_name, filename, (size_t)name_len);
            pkg_name[name_len] = '\0';
        }
        
        // Parser la version (entre -v et .)
        char *arch_start = strstr(version_start + 2, ".");
        if (arch_start) {
            int ver_len = (int)(arch_start - (version_start + 2));
            if (ver_len > 0 && (size_t)ver_len < sizeof(pkg_version)) {
                strncpy(pkg_version, version_start + 2, (size_t)ver_len);
                pkg_version[ver_len] = '\0';
            }
            
            // Parser l'architecture
            char *ext_start = strstr(arch_start + 1, ".tar.bool");
            if (ext_start) {
                int arch_len = (int)(ext_start - (arch_start + 1));
                if (arch_len > 0 && (size_t)arch_len < sizeof(pkg_arch)) {
                    strncpy(pkg_arch, arch_start + 1, (size_t)arch_len);
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
    
    // Résolution des dépendances (fonction externe)
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
        register_installed_package(pkg_name, pkg_version, pkg_arch);
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

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }

    char *command = argv[1];
    output_format_t fmt = OUTPUT_TEXT;

    // Détection des formats (JSON/TOML)
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 || strcmp(argv[i], "-j") == 0) {
            fmt = OUTPUT_JSON;
        }
        if (strcmp(argv[i], "--toml") == 0 || strcmp(argv[i], "-t") == 0) {
            fmt = OUTPUT_TOML;
        }
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
    }
    else if (strcmp(command, "register") == 0) {
        if (argc < 4) {
            printf("Usage: apkm register <nom> <version> [arch]\n");
            return 1;
        }
        char *name = argv[2];
        char *version = argv[3];
        char *arch = (argc > 4) ? argv[4] : "x86_64";
        register_installed_package(name, version, arch);
        printf("[APKM] ✅ Paquet %s %s enregistré manuellement\n", name, version);
    }
    else if (strcmp(command, "audit") == 0) {
        printf("[APKM] 🛡️ Analyse CVE et scan d'intégrité...\n");
        // Logique audit à implémenter
        printf("[APKM] ✅ Audit terminé (not implamentay)\n");
    } 
    else if (strcmp(command, "rollback") == 0) {
        printf("[APKM] ⏪ Restauration vers la version précédente...\n");
        // Logique rollback à implémenter
        printf("[APKM] ✅ Rollback terminé (not implamentay)\n");
    } 
    else {
        fprintf(stderr, "[APKM] Commande inconnue : %s\n", command);
        fprintf(stderr, "Utilisez 'apkm --help' pour voir les commandes disponibles\n");
        return 1;
    }

    return 0;
}
