#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/prctl.h>
#include <grp.h>
#include <stdbool.h>  // AJOUTÉ pour bool
#include "sandbox.h"

// Constantes de namespace
#ifndef CLONE_NEWNS
#define CLONE_NEWNS 0x00020000
#endif
#ifndef CLONE_NEWUTS
#define CLONE_NEWUTS 0x04000000
#endif
#ifndef CLONE_NEWIPC
#define CLONE_NEWIPC 0x08000000
#endif
#ifndef CLONE_NEWPID
#define CLONE_NEWPID 0x20000000
#endif
#ifndef CLONE_NEWNET
#define CLONE_NEWNET 0x40000000
#endif

// Constantes prctl pour les capacités
#ifndef PR_SET_SECUREBITS
#define PR_SET_SECUREBITS 28
#endif
#ifndef SECBIT_KEEP_CAPS
#define SECBIT_KEEP_CAPS 1
#endif
#ifndef SECBIT_NOROOT
#define SECBIT_NOROOT 2
#endif
#ifndef SECBIT_NO_SETUID_FIXUP
#define SECBIT_NO_SETUID_FIXUP 4
#endif

// Version simplifiée sans libcap
static void drop_privileges(void) {
    // Changer l'utilisateur et le groupe
    gid_t gid = 65534; // nobody
    uid_t uid = 65534; // nobody
    
    setgroups(0, NULL);
    setgid(gid);
    setuid(uid);
}

int apkm_sandbox_init(const char *target_path) {
    // Création d'un nouveau Mount Namespace
    if (unshare(CLONE_NEWNS) != 0) {
        perror("unshare");
        return -1;
    }

    // Rendre les montages privés
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

    // Montage d'une couche de staging temporaire
    if (mount("tmpfs", target_path, "tmpfs", 0, NULL) != 0) {
        perror("mount tmpfs");
        return -1;
    }

    return 0;
}

int apkm_sandbox_create(const char* path, int enable_network, int enable_mount) {
    // Utiliser int au lieu de bool pour éviter les problèmes
    int flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID;
    if (!enable_network) flags |= CLONE_NEWNET;
    
    // Création des namespaces
    if (unshare(flags) != 0) {
        perror("unshare");
        return -1;
    }
    
    // Mount proc si demandé
    if (enable_mount) {
        if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
            perror("mount proc");
            return -1;
        }
    }
    
    // Drop privileges
    drop_privileges();
    
    // Chroot si nécessaire
    if (enable_mount && path) {
        chroot(path);
        chdir("/");
    }
    
    return 0;
}

int apkm_sandbox_lockdown() {
    // Désactiver les capacités dangereuses (version simplifiée)
    if (prctl(PR_SET_SECUREBITS, 
              SECBIT_KEEP_CAPS | SECBIT_NOROOT | SECBIT_NO_SETUID_FIXUP, 
              0, 0, 0) != 0) {
        perror("prctl");
        return -1;
    }
    
    return 0;
}
