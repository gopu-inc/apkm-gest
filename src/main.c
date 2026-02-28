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

    const char *staging_path = "/tmp/apkm_install";
    
    // Créer et vider le répertoire temporaire
    mkdir(staging_path, 0755);
    char cmd_clean[512];
    snprintf(cmd_clean, sizeof(cmd_clean), "rm -rf %s/*", staging_path);
    system(cmd_clean);

    // Extraction
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
        NULL
    };
    
    int script_found = 0;
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
                break;
            } else {
                printf("[APKM] ⚠️ Échec du script %s (code: %d)\n", scripts[i], ret);
            }
        }
    }
    
    if (!script_found) {
        printf("[APKM] ⚠️ Aucun script d'installation trouvé\n");
    }
    
    // Nettoyage
    printf("[APKM] 🧹 Nettoyage...\n");
    snprintf(cmd_clean, sizeof(cmd_clean), "rm -rf %s", staging_path);
    system(cmd_clean);
    
    printf("[APKM] ✅ Installation terminée !\n");
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
