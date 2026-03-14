#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "anv.h"

int initialize_environment(anv_config_t *config) {
    // Vérification de sécurité : empêcher l'exécution en root réel
    if (getuid() == 0) {
        fprintf(stderr, "[track backsh >_< root no secure]\n");
        return -1;
    }

    // Création des namespaces pour l'isolation totale
    if (unshare(CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNET) == -1) {
        perror("unshare");
        return -1;
    }

    printf("Environment '%s' created successfully.\n", config->name);
    return 0;
}

