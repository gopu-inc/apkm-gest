#ifndef ANV_H
#define ANV_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/capability.h>
#include <sys/syscall.h>
#include <grp.h>
#include <pwd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h> 
#include <libgen.h> 

#define ANV_VERSION "1.0.0"
#define ANV_NAME_MAX 64
#define ANV_PATH_MAX 512
#define ANV_MAGIC "ANV_SECURE_ENV"
#define ANV_NAMESBAR "__namesbar"

// Niveaux de sécurité
#define ANV_SEC_NONE    0  // Aucune isolation
#define ANV_SEC_LOW     1  // Isolation basique
#define ANV_SEC_MEDIUM  2  // Isolation standard
#define ANV_SEC_HIGH    3  // Isolation renforcée
#define ANV_SEC_PARANOID 4 // Isolation maximale

// Types d'environnements
#define ANV_TYPE_APKM   0  // Pour APKM
#define ANV_TYPE_BOOL   1  // Pour BOOL
#define ANV_TYPE_APSM   2  // Pour APSM
#define ANV_TYPE_CUSTOM 3  // Personnalisé

// Codes d'erreur
#define ANV_OK          0
#define ANV_ERR_ROOT    -1
#define ANV_ERR_NOENV   -2
#define ANV_ERR_EXISTS  -3
#define ANV_ERR_MOUNT   -4
#define ANV_ERR_CAP     -5
#define ANV_ERR_NS      -6

// Structure d'environnement
typedef struct {
    char name[ANV_NAME_MAX];
    char path[ANV_PATH_MAX];
    int type;
    int security_level;
    pid_t init_pid;
    uid_t host_uid;
    gid_t host_gid;
    uid_t ns_uid;
    gid_t ns_gid;
    char hostname[64];
    time_t created;
    time_t last_used;
    int namespaces[8];  // PID, NET, IPC, UTS, MOUNT, USER, CGROUP, TIME
    int is_running;
    char prompt_prefix[32];
    char shell_path[256];
    char rootfs[ANV_PATH_MAX];
    struct {
        int no_network;
        int read_only;
        int no_new_privs;
        int seccomp;
        int apparmor;
        int landlock;
    } security;
} anv_env_t;

// Structure de contexte
typedef struct {
    anv_env_t *envs;
    int count;
    char base_path[ANV_PATH_MAX];
    int default_security;
    int verbose;
} anv_ctx_t;

// Prototypes
int anv_init(anv_ctx_t *ctx);
int anv_create(anv_ctx_t *ctx, const char *name, int type, int security);
int anv_start(anv_ctx_t *ctx, const char *name);
int anv_enter(anv_ctx_t *ctx, const char *name, char *const argv[]);
int anv_stop(anv_ctx_t *ctx, const char *name);
int anv_delete(anv_ctx_t *ctx, const char *name);
int anv_list(anv_ctx_t *ctx);
int anv_check_root(void);
void anv_set_prompt(anv_env_t *env, char *prompt, size_t size);

#endif
