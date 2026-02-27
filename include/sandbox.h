#ifndef APKM_SANDBOX_H
#define APKM_SANDBOX_H

// Prépare un environnement isolé pour l'installation
int apkm_sandbox_init(const char *target_path);

// Crée une sandbox complète avec namespaces
// enable_network: 1 pour activer le réseau, 0 pour le désactiver
// enable_mount: 1 pour activer les montages, 0 pour les désactiver
int apkm_sandbox_create(const char* path, int enable_network, int enable_mount);

// Applique les restrictions de sécurité supplémentaires
int apkm_sandbox_lockdown(void);

#endif
