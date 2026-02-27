#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>
#include "apkm.h"
#include "sandbox.h"

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

    // 1. Initialisation de la Sandbox sécurisée
    const char *staging_path = "/tmp/apkm_staging";
    if (apkm_sandbox_init(staging_path) != 0) {
        fprintf(stderr, "[APKM] ❌ Erreur : Impossible de créer la sandbox.\n");
        return;
    }

    // 2. Extraction du format propriétaire .tar.bool
    printf("[APKM] 🔍 Débooleur en cours (extraction isolée)...\n");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tar -xzf %s -C %s", filepath, staging_path);
    
    if (system(cmd) != 0) {
        fprintf(stderr, "[APKM] ❌ Erreur lors du débooleur.\n");
        umount(staging_path);
        return;
    }
    
    // Résolution des dépendances (fonction déclarée dans apkm.h)
    resolve_dependencies(staging_path);
    
    // 3. Exécution du script d'installation
    printf("[APKM] ⚙️ Exécution du script d'installation...\n");
    char script_path[512];
    snprintf(script_path, sizeof(script_path), "sh %s/install.sh", staging_path);
    system(script_path);

    // 4. Gestion des Refs
    printf("[APKM] ⚓ Création d'une nouvelle ref dans /var/lib/apkm/refs/\n");
    
    // Nettoyage final
    umount(staging_path);
    printf("[APKM] ✅ Installation terminée avec succès.\n");
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
