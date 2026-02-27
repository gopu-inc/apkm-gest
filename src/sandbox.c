#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "sandbox.h"

int apkm_sandbox_init(const char *target_path) {
    // Création d'un nouveau Mount Namespace
    if (unshare(CLONE_NEWNS) != 0) {
        perror("unshare");
        return -1;
    }

    // Rendre les montages privés pour ne pas polluer l'hôte
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

    // Montage d'une couche de staging temporaire (RAM disk)
    if (mount("tmpfs", target_path, "tmpfs", 0, NULL) != 0) {
        perror("mount tmpfs");
        return -1;
    }

    return 0;
}

