#ifndef ANV_H
#define ANV_H

#define MAX_ENV_NAME 32
#define DEFAULT_ROOTFS "/var/lib/anv/base"

typedef struct {
    char name[MAX_ENV_NAME];
    int secure_mode; // Flag pour activer le mode restreint
    uid_t original_uid;
} anv_config_t;

int initialize_environment(anv_config_t *config);

#endif

