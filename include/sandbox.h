#ifndef APKM_SANDBOX_H
#define APKM_SANDBOX_H

// Prépare un environnement isolé pour l'installation
int apkm_sandbox_init(const char *target_path);
// Applique les filtres Seccomp pour restreindre les syscalls
int apkm_sandbox_lockdown();

#endif

