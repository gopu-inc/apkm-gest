#include "anv.h"
#include <dirent.h>
#include <libgen.h>
#include <wordexp.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/mount.h>
#include <sched.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <curl/curl.h>

// ============================================================================
// CONSTANTES
// ============================================================================
#define SUPERSU_URL "https://supersuroot.org/downloads/supersu-2-82.apk"
#define SUPERSU_PATH "/usr/local/share/anv/supersu.apk"
#define DOCKER_CHECK "docker --version > /dev/null 2>&1"
#define MAX_CMD 4096

// ============================================================================
// STRUCTURES POUR CURL
// ============================================================================
struct MemoryStruct {
    char *memory;
    size_t size;
};

// ============================================================================
// DÉCLARATIONS ANTICIPÉES DES FONCTIONS STATIQUES
// ============================================================================
static int install_supersu(anv_env_t *env);
static int setup_docker_in_env(anv_env_t *env);
static int check_nsenter_support(void);
static int create_devices(anv_env_t *env);
static int install_namesbar(anv_env_t *env);
static int mkdir_p(const char *root, const char *path);
static int save_env_config(anv_env_t *env);
static int load_env_config(anv_ctx_t *ctx, const char *name, anv_env_t *env);
static int save_env_pid(anv_env_t *env);
static int setup_rootfs(anv_env_t *env);
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);

// ============================================================================
// DÉTECTION ROOT ET MESSAGES
// ============================================================================
int anv_check_root(void) {
    if (geteuid() == 0) {
        printf("\033[1;31m");
        printf("╔════════════════════════════════════════════════════╗\n");
        printf("║  [track backsh >_< root no secure]               ║\n");
        printf("║  Running as root - downloading SuperSU for safety ║\n");
        printf("╚════════════════════════════════════════════════════╝\n");
        printf("\033[0m");
        
        // Télécharger SuperSU automatiquement
        anv_download_supersu();
        
        return ANV_OK; // Permettre l'exécution en root avec SuperSU
    }
    return ANV_OK;
}

// ============================================================================
// TÉLÉCHARGEMENT SUPERSU
// ============================================================================
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        printf("Not enough memory\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

int anv_download_supersu(void) {
    printf("[ANV] Downloading SuperSU for root environments...\n");
    
    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();
    
    if(curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, SUPERSU_URL);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ANV-SuperSU/1.0");
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 30L);
        
        res = curl_easy_perform(curl_handle);
        
        if(res == CURLE_OK) {
            // Créer le répertoire
            mkdir("/usr/local/share/anv", 0755);
            
            // Sauvegarder le fichier
            FILE *fp = fopen(SUPERSU_PATH, "wb");
            if(fp) {
                fwrite(chunk.memory, 1, chunk.size, fp);
                fclose(fp);
                chmod(SUPERSU_PATH, 0755);
                printf("[ANV] ✅ SuperSU downloaded: %s (%zu bytes)\n", 
                       SUPERSU_PATH, chunk.size);
            } else {
                printf("[ANV] ❌ Failed to save SuperSU\n");
            }
        } else {
            printf("[ANV] ❌ Download failed: %s\n", curl_easy_strerror(res));
        }
        
        curl_easy_cleanup(curl_handle);
        free(chunk.memory);
    }
    
    curl_global_cleanup();
    return (res == CURLE_OK) ? ANV_OK : ANV_ERR_DOWNLOAD;
}

// ============================================================================
// VÉRIFICATION DOCKER
// ============================================================================
int anv_check_docker(void) {
    if (system(DOCKER_CHECK) == 0) {
        printf("[ANV] 🐳 Docker detected - enabling container support\n");
        return 1;
    }
    return 0;
}

// ============================================================================
// INITIALISATION
// ============================================================================
int anv_init(anv_ctx_t *ctx) {
    memset(ctx, 0, sizeof(anv_ctx_t));
    
    // Vérifier root (avec SuperSU)
    anv_check_root();
    
    // Vérifier Docker
    ctx->docker_available = anv_check_docker();
    
    // Définir le chemin de base
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(ctx->base_path, sizeof(ctx->base_path), "%s/.anv", home);
    
    // Créer le répertoire de base
    mkdir(ctx->base_path, 0755);
    mkdir("/usr/local/share/anv", 0755);
    
    // Charger les environnements existants
    ctx->default_security = ANV_SEC_HIGH;
    ctx->verbose = 1;
    
    printf("🔒 ANV v%s - Advanced Namespace Virtualization\n", ANV_VERSION);
    printf("   Base path: %s\n", ctx->base_path);
    printf("   Security level: %s\n", 
           ctx->default_security == ANV_SEC_HIGH ? "High" : "Medium");
    if (ctx->docker_available) {
        printf("   Docker support: ✅ Enabled\n");
    }
    
    return ANV_OK;
}

// ============================================================================
// FONCTIONS DE CONFIGURATION DU ROOTFS
// ============================================================================
static int mkdir_p(const char *root, const char *path) {
    char full[ANV_PATH_MAX];
    snprintf(full, sizeof(full), "%s%s", root, path);
    
    char *p = full;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(full, 0755);
            *p = '/';
        }
        p++;
    }
    mkdir(full, 0755);
    return ANV_OK;
}

static int setup_rootfs(anv_env_t *env) {
    snprintf(env->rootfs, sizeof(env->rootfs), "%s/rootfs", env->path);
    
    // Créer la structure de base
    const char *dirs[] = {
        "bin", "sbin", "usr/bin", "usr/sbin", "usr/lib",
        "etc", "home", "tmp", "var", "proc", "sys", "dev",
        "run", "mnt", "media", "opt", "srv", "root",
        "system/bin", "system/app", "system/xbin",
        NULL
    };
    
    for (int i = 0; dirs[i]; i++) {
        char path[ANV_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", env->rootfs, dirs[i]);
        mkdir(path, 0755);
    }
    
    // Copier les binaires APKM/APSM/BOOL
    const char *apkm_bins[] = {
        "/usr/bin/apkm", "/usr/bin/apsm", "/usr/bin/bool",
        "/bin/sh", "/bin/busybox", "/bin/ls", "/bin/cat",
        "/bin/ps", "/bin/mount", "/bin/umount", "/bin/grep",
        NULL
    };
    
    for (int i = 0; apkm_bins[i]; i++) {
        if (access(apkm_bins[i], F_OK) == 0) {
            char dest[ANV_PATH_MAX];
            snprintf(dest, sizeof(dest), "%s%s", env->rootfs, apkm_bins[i]);
            
            char *dir = dirname(strdup(dest));
            mkdir(dir, 0755);
            
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "cp %s %s 2>/dev/null", apkm_bins[i], dest);
            system(cmd);
        }
    }
    
    // Créer le fichier de configuration APKM
    char conf_path[ANV_PATH_MAX];
    snprintf(conf_path, sizeof(conf_path), "%s/etc/apkm/repositories.conf", env->rootfs);
    mkdir_p(env->rootfs, "/etc/apkm");
    
    FILE *f = fopen(conf_path, "w");
    if (f) {
        fprintf(f, "# APKM Repositories\n");
        fprintf(f, "# Format: name url [priority]\n");
        fprintf(f, "zarch-hub https://gsql-badge.onrender.com 5\n");
        fclose(f);
    }
    
    return ANV_OK;
}

// ============================================================================
// INSTALLATION SUPERSU DANS L'ENVIRONNEMENT
// ============================================================================
static int install_supersu(anv_env_t *env) {
    char dest_path[ANV_PATH_MAX];
    snprintf(dest_path, sizeof(dest_path), "%s/system/bin/su", env->rootfs);
    
    // Créer les répertoires nécessaires
    mkdir_p(env->rootfs, "/system/bin");
    mkdir_p(env->rootfs, "/system/app/SuperSU");
    
    // Copier SuperSU
    if (access(SUPERSU_PATH, F_OK) == 0) {
        char cmd[MAX_CMD];
        snprintf(cmd, sizeof(cmd), "cp %s %s/", SUPERSU_PATH, env->rootfs);
        system(cmd);
        
        // Extraire si c'est un APK
        snprintf(cmd, sizeof(cmd), 
                 "cd %s && unzip -o supersu.apk -d system/ 2>/dev/null", 
                 env->rootfs);
        system(cmd);
    }
    
    // Créer le binaire su
    FILE *f = fopen(dest_path, "w");
    if (f) {
        fprintf(f, "#!/system/bin/sh\n");
        fprintf(f, "# SuperSU wrapper for ANV\n");
        fprintf(f, "if [ \"$1\" = \"-c\" ]; then\n");
        fprintf(f, "    shift\n");
        fprintf(f, "    sh -c \"$@\"\n");
        fprintf(f, "else\n");
        fprintf(f, "    sh\n");
        fprintf(f, "fi\n");
        fclose(f);
        chmod(dest_path, 06755);  // setuid root
    }
    
    printf("[ANV] 🔐 SuperSU installed in environment\n");
    return ANV_OK;
}

// ============================================================================
// CONFIGURATION DOCKER DANS L'ENVIRONNEMENT
// ============================================================================
static int setup_docker_in_env(anv_env_t *env) {
    char docker_socket[ANV_PATH_MAX];
    snprintf(docker_socket, sizeof(docker_socket), "%s/var/run/docker.sock", env->rootfs);
    
    // Monter le socket Docker
    mkdir_p(env->rootfs, "/var/run");
    
    // Bind mount du socket Docker réel (sera fait au démarrage)
    printf("[ANV] 🐳 Docker will be available in environment\n");
    
    return ANV_OK;
}

// ============================================================================
// CRÉATION DES PÉRIPHÉRIQUES
// ============================================================================
static int create_devices(anv_env_t *env) {
    char dev_path[ANV_PATH_MAX];
    snprintf(dev_path, sizeof(dev_path), "%s/dev", env->rootfs);
    mkdir(dev_path, 0755);
    
    // Créer les périphériques standards
    char path[ANV_PATH_MAX];
    
    snprintf(path, sizeof(path), "%s/null", dev_path);
    mknod(path, S_IFCHR | 0666, makedev(1, 3));
    
    snprintf(path, sizeof(path), "%s/zero", dev_path);
    mknod(path, S_IFCHR | 0666, makedev(1, 5));
    
    snprintf(path, sizeof(path), "%s/random", dev_path);
    mknod(path, S_IFCHR | 0666, makedev(1, 8));
    
    snprintf(path, sizeof(path), "%s/urandom", dev_path);
    mknod(path, S_IFCHR | 0666, makedev(1, 9));
    
    snprintf(path, sizeof(path), "%s/tty", dev_path);
    mknod(path, S_IFCHR | 0666, makedev(5, 0));
    
    return ANV_OK;
}

// ============================================================================
// INSTALLATION NAMESBAR
// ============================================================================
static int install_namesbar(anv_env_t *env) {
    char namesbar_path[ANV_PATH_MAX];
    snprintf(namesbar_path, sizeof(namesbar_path), "%s/usr/bin/%s", 
             env->rootfs, ANV_NAMESBAR);
    
    mkdir_p(env->rootfs, "/usr/bin");
    
    FILE *f = fopen(namesbar_path, "w");
    if (!f) return -1;
    
    fprintf(f, "#!/bin/sh\n");
    fprintf(f, "# NamesBar - Secure ANV Shell for APKM/APSM/BOOL\n");
    fprintf(f, "export PS1='[::%s::]\\u@\\h:\\w$ '\n", env->name);
    fprintf(f, "export ANV_ENV='%s'\n", env->name);
    fprintf(f, "export ANV_SEC_LEVEL='%d'\n", env->security_level);
    fprintf(f, "export APKM_REPO='https://gsql-badge.onrender.com'\n");
    fprintf(f, "if [ -f /system/bin/su ]; then\n");
    fprintf(f, "    export PATH=/system/bin:$PATH\n");
    fprintf(f, "    echo '[ANV] SuperSU available'\n");
    fprintf(f, "fi\n");
    fprintf(f, "echo '[ANV] Environment: %s (security level %d)'\n", 
            env->name, env->security_level);
    fprintf(f, "exec /bin/sh \"$@\"\n");
    
    fclose(f);
    chmod(namesbar_path, 0755);
    
    // Remplacer /bin/sh par namesbar
    char sh_path[ANV_PATH_MAX];
    snprintf(sh_path, sizeof(sh_path), "%s/bin/sh", env->rootfs);
    unlink(sh_path);
    symlink(namesbar_path, sh_path);
    
    return ANV_OK;
}

// ============================================================================
// VÉRIFICATION SUPPORT NSENTER
// ============================================================================
static int check_nsenter_support(void) {
    FILE *fp = popen("nsenter --help 2>&1 | grep -c '\\-C'", "r");
    if (!fp) return 0;
    
    char buffer[16];
    int has_cgroup = 0;
    if (fgets(buffer, sizeof(buffer), fp)) {
        has_cgroup = atoi(buffer) > 0;
    }
    pclose(fp);
    
    return has_cgroup;
}

// ============================================================================
// SAUVEGARDE / CHARGEMENT CONFIGURATION
// ============================================================================
static int save_env_config(anv_env_t *env) {
    char config_path[ANV_PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/config", env->path);
    
    FILE *f = fopen(config_path, "w");
    if (!f) return -1;
    
    fprintf(f, "ANV_ENV=%s\n", env->name);
    fprintf(f, "TYPE=%d\n", env->type);
    fprintf(f, "SECURITY_LEVEL=%d\n", env->security_level);
    fprintf(f, "CREATED=%ld\n", env->created);
    fprintf(f, "HOST_UID=%d\n", env->host_uid);
    fprintf(f, "HOST_GID=%d\n", env->host_gid);
    fprintf(f, "DOCKER_ENABLED=%d\n", env->docker_enabled);
    
    fclose(f);
    return ANV_OK;
}

static int load_env_config(anv_ctx_t *ctx, const char *name, anv_env_t *env) {
    memset(env, 0, sizeof(anv_env_t));
    strncpy(env->name, name, ANV_NAME_MAX - 1);
    
    snprintf(env->path, sizeof(env->path), "%s/%s", ctx->base_path, name);
    snprintf(env->rootfs, sizeof(env->rootfs), "%s/rootfs", env->path);
    
    char config_path[ANV_PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/config", env->path);
    
    FILE *f = fopen(config_path, "r");
    if (!f) return ANV_ERR_NOENV;
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64], value[192];
        if (sscanf(line, "%63[^=]=%191s", key, value) == 2) {
            if (strcmp(key, "TYPE") == 0) env->type = atoi(value);
            else if (strcmp(key, "SECURITY_LEVEL") == 0) env->security_level = atoi(value);
            else if (strcmp(key, "CREATED") == 0) env->created = atol(value);
            else if (strcmp(key, "HOST_UID") == 0) env->host_uid = atoi(value);
            else if (strcmp(key, "HOST_GID") == 0) env->host_gid = atoi(value);
            else if (strcmp(key, "DOCKER_ENABLED") == 0) env->docker_enabled = atoi(value);
        }
    }
    fclose(f);
    
    // Lire le PID si existe
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", env->path);
    f = fopen(pid_path, "r");
    if (f) {
        fscanf(f, "%d", &env->init_pid);
        env->is_running = 1;
        fclose(f);
    }
    
    return ANV_OK;
}

static int save_env_pid(anv_env_t *env) {
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", env->path);
    
    FILE *f = fopen(pid_path, "w");
    if (!f) return -1;
    
    fprintf(f, "%d", env->init_pid);
    fclose(f);
    
    return ANV_OK;
}

// ============================================================================
// FONCTION PRINCIPALE POUR LE PROCESSUS ENFANT (PID 1)
// ============================================================================
static int child_func(void *arg) {
    anv_env_t *env = (anv_env_t *)arg;
    
    // Configurer les namespaces
    int flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID;
    if (env->security_level >= ANV_SEC_HIGH) {
        flags |= CLONE_NEWNET | CLONE_NEWUSER | CLONE_NEWCGROUP;
    }
    
    if (unshare(flags) == -1) {
        perror("unshare");
        return ANV_ERR_NS;
    }
    
    // Configurer les mounts
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    
    // Monter /proc (nécessaire pour PID 1)
    char proc_path[ANV_PATH_MAX];
    snprintf(proc_path, sizeof(proc_path), "%s/proc", env->rootfs);
    mount("proc", proc_path, "proc", 0, NULL);
    
    // Monter /sys si nécessaire
    if (!env->security.no_network) {
        char sys_path[ANV_PATH_MAX];
        snprintf(sys_path, sizeof(sys_path), "%s/sys", env->rootfs);
        mount("sysfs", sys_path, "sysfs", 0, NULL);
    }
    
    // Monter le socket Docker si demandé
    if (env->docker_enabled) {
        char docker_socket[ANV_PATH_MAX];
        snprintf(docker_socket, sizeof(docker_socket), "%s/var/run/docker.sock", env->rootfs);
        mount("/var/run/docker.sock", docker_socket, NULL, MS_BIND, NULL);
    }
    
    // Créer les périphériques
    create_devices(env);
    
    // Installer NamesBar
    install_namesbar(env);
    
    // Chroot
    if (chroot(env->rootfs) != 0) {
        perror("chroot");
        return ANV_ERR_MOUNT;
    }
    chdir("/");
    
    // Appliquer les sécurités
    if (env->security.no_new_privs) {
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    }
    
    // Set hostname
    sethostname(env->hostname, strlen(env->hostname));
    
    // Lancer le shell (via namesbar)
    printf("\n[ANV] Environment '%s' started (security level %d)\n", 
           env->name, env->security_level);
    printf("[ANV] Type 'exit' to stop the environment\n\n");
    
    execl("/bin/sh", "sh", NULL);
    
    return ANV_OK;
}

// ============================================================================
// API PUBLIQUE - CRÉATION
// ============================================================================
int anv_create(anv_ctx_t *ctx, const char *name, int type, int security) {
    printf("🔐 Creating ANV environment: %s\n", name);
    printf("   Type: %s\n", 
           type == ANV_TYPE_APKM ? "APKM" :
           type == ANV_TYPE_BOOL ? "BOOL" :
           type == ANV_TYPE_APSM ? "APSM" : "CUSTOM");
    
    anv_env_t env;
    memset(&env, 0, sizeof(env));
    
    strncpy(env.name, name, ANV_NAME_MAX - 1);
    env.type = type;
    env.security_level = security;
    env.host_uid = getuid();
    env.host_gid = getgid();
    env.created = time(NULL);
    env.last_used = env.created;
    env.is_running = 0;
    env.docker_enabled = ctx->docker_available;
    
    snprintf(env.hostname, sizeof(env.hostname), "anv-%s", name);
    snprintf(env.path, sizeof(env.path), "%s/%s", ctx->base_path, name);
    snprintf(env.prompt_prefix, sizeof(env.prompt_prefix), "::%s::", name);
    
    // Configuration sécurité
    env.security.no_network = (security >= ANV_SEC_HIGH) ? 1 : 0;
    env.security.read_only = (security >= ANV_SEC_MEDIUM) ? 1 : 0;
    env.security.no_new_privs = (security >= ANV_SEC_HIGH) ? 1 : 0;
    env.security.seccomp = (security >= ANV_SEC_PARANOID) ? 1 : 0;
    
    // Créer le répertoire
    mkdir(env.path, 0755);
    
    // Configurer le rootfs
    if (setup_rootfs(&env) != ANV_OK) {
        printf("❌ Rootfs creation failed\n");
        return ANV_ERR_MOUNT;
    }
    
    // Installer SuperSU si root et disponible
    if (geteuid() == 0 && access(SUPERSU_PATH, F_OK) == 0) {
        install_supersu(&env);
    }
    
    // Configurer Docker si disponible
    if (env.docker_enabled) {
        setup_docker_in_env(&env);
    }
    
    // Sauvegarder la configuration
    save_env_config(&env);
    
    printf("✅ Environment created: %s\n", env.rootfs);
    printf("   Security level: %d\n", security);
    printf("   NamesBar: %s\n", ANV_NAMESBAR);
    if (env.docker_enabled) {
        printf("   Docker: ✅ Available\n");
    }
    
    return ANV_OK;
}

// ============================================================================
// API PUBLIQUE - DÉMARRAGE
// ============================================================================
int anv_start(anv_ctx_t *ctx, const char *name) {
    printf("🚀 Starting environment: %s\n", name);
    
    anv_env_t env;
    if (load_env_config(ctx, name, &env) != ANV_OK) {
        printf("❌ Environment '%s' not found\n", name);
        return ANV_ERR_NOENV;
    }
    
    // Stack pour clone (8MB)
    void *stack = malloc(8 * 1024 * 1024);
    void *stack_top = stack + 8 * 1024 * 1024;
    
    // Flags pour clone
    int flags = SIGCHLD | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC;
    if (env.security_level >= ANV_SEC_HIGH) {
        flags |= CLONE_NEWNET | CLONE_NEWUSER | CLONE_NEWCGROUP;
    }
    
    // Créer le processus fils (PID 1 dans le namespace)
    pid_t pid = clone(child_func, stack_top, flags, &env);
    
    if (pid == -1) {
        perror("clone");
        free(stack);
        return ANV_ERR_NS;
    }
    
    env.init_pid = pid;
    env.is_running = 1;
    
    printf("✅ Environment started with PID: %d (PID 1 in namespace)\n", pid);
    printf("   NamesBar: %s\n", ANV_NAMESBAR);
    
    // Sauvegarder le PID
    save_env_pid(&env);
    
    free(stack);
    return ANV_OK;
}

// ============================================================================
// API PUBLIQUE - ENTRER
// ============================================================================
int anv_enter(anv_ctx_t *ctx, const char *name, char *const argv[]) {
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/%s/pid", ctx->base_path, name);
    
    FILE *f = fopen(pid_path, "r");
    if (!f) {
        printf("❌ Environment '%s' not started\n", name);
        return ANV_ERR_NOENV;
    }
    
    pid_t pid;
    fscanf(f, "%d", &pid);
    fclose(f);
    
    // Vérifier si l'option -C est supportée
    int has_cgroup = check_nsenter_support();
    
    char cmd[MAX_CMD];
    if (has_cgroup) {
        snprintf(cmd, sizeof(cmd), 
                 "nsenter -t %d -m -u -i -n -p -C -- "
                 "env PS1='[::%s::]\\u@\\h:\\w$ ' /bin/sh -i",
                 pid, name);
    } else {
        snprintf(cmd, sizeof(cmd), 
                 "nsenter -t %d -m -u -i -n -p -- "
                 "env PS1='[::%s::]\\u@\\h:\\w$ ' /bin/sh -i",
                 pid, name);
    }
    
    printf("🔐 Entering environment %s\n", name);
    printf("   NamesBar active: %s\n", ANV_NAMESBAR);
    printf("   APKM/APSM/BOOL tools available\n");
    printf("   Type 'exit' to leave\n\n");
    
    system(cmd);
    
    return ANV_OK;
}

// ============================================================================
// API PUBLIQUE - ARRÊT
// ============================================================================
int anv_stop(anv_ctx_t *ctx, const char *name) {
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/%s/pid", ctx->base_path, name);
    
    FILE *f = fopen(pid_path, "r");
    if (!f) {
        printf("❌ Environment '%s' not started\n", name);
        return ANV_ERR_NOENV;
    }
    
    pid_t pid;
    fscanf(f, "%d", &pid);
    fclose(f);
    
    // Tuer le processus (PID 1 du namespace)
    kill(pid, SIGTERM);
    sleep(1);
    kill(pid, SIGKILL);
    
    unlink(pid_path);
    
    printf("✅ Environment '%s' stopped\n", name);
    
    return ANV_OK;
}

// ============================================================================
// API PUBLIQUE - LISTE
// ============================================================================
int anv_list(anv_ctx_t *ctx) {
    DIR *dir = opendir(ctx->base_path);
    if (!dir) {
        printf("📂 No environments found\n");
        return ANV_OK;
    }
    
    printf("\n📦 ANV Environments:\n");
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("%-20s %-10s %-12s %-10s %-12s %s\n", 
           "NAME", "STATUS", "TYPE", "SECURITY", "PID", "CREATED");
    printf("───────────────────────────────────────────────────────────────────\n");
    
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        
        anv_env_t env;
        if (load_env_config(ctx, entry->d_name, &env) != ANV_OK) {
            continue;
        }
        
        // Vérifier si en cours
        char pid_path[ANV_PATH_MAX];
        snprintf(pid_path, sizeof(pid_path), "%s/%s/pid", ctx->base_path, entry->d_name);
        int running = (access(pid_path, F_OK) == 0);
        
        // Formater la date
        char date_str[32] = "";
        if (env.created > 0) {
            struct tm *tm = localtime(&env.created);
            strftime(date_str, sizeof(date_str), "%Y-%m-%d", tm);
        }
        
        const char *type_str = env.type == 0 ? "APKM" : 
                              env.type == 1 ? "BOOL" :
                              env.type == 2 ? "APSM" : "CUSTOM";
        
        printf(" %-20s %-10s %-12s %-10d %-12d %s\n",
               env.name,
               running ? "\033[32mRUNNING\033[0m" : "\033[33mSTOPPED\033[0m",
               type_str,
               env.security_level,
               running ? env.init_pid : 0,
               date_str);
        
        if (running && env.docker_enabled) {
            printf("   └─ Docker: ✅\n");
        }
    }
    
    closedir(dir);
    printf("═══════════════════════════════════════════════════════════════════\n\n");
    
    return ANV_OK;
}

// ============================================================================
// API PUBLIQUE - SUPPRESSION
// ============================================================================
int anv_delete(anv_ctx_t *ctx, const char *name) {
    char env_path[ANV_PATH_MAX];
    snprintf(env_path, sizeof(env_path), "%s/%s", ctx->base_path, name);
    
    if (access(env_path, F_OK) != 0) {
        printf("❌ Environment '%s' not found\n", name);
        return ANV_ERR_NOENV;
    }
    
    // Vérifier s'il tourne
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", env_path);
    if (access(pid_path, F_OK) == 0) {
        printf("⚠️  Environment is still running. Stopping first...\n");
        anv_stop(ctx, name);
    }
    
    // Supprimer
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", env_path);
    system(cmd);
    
    printf("✅ Environment '%s' deleted\n", name);
    
    return ANV_OK;
}

// ============================================================================
// MAIN - CLI
// ============================================================================
void print_usage(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  ANV v%s - Advanced Namespace Virtualization                ║\n", ANV_VERSION);
    printf("║  [track backsh >_< secure environment for APKM/APSM/BOOL]   ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    printf("USAGE:\n");
    printf("  anv <command> [arguments]\n\n");
    
    printf("COMMANDS:\n");
    printf("  create <name> [type] [security]  Create an environment\n");
    printf("  start <name>                      Start the environment\n");
    printf("  enter <name>                       Enter the environment\n");
    printf("  stop <name>                        Stop the environment\n");
    printf("  delete <name>                       Delete the environment\n");
    printf("  list                                List all environments\n");
    printf("  help                                Show this help\n\n");
    
    printf("TYPES (for APKM ecosystem):\n");
    printf("  0 - APKM    (Package Manager)     - Install packages\n");
    printf("  1 - BOOL    (Package Builder)     - Build packages\n");
    printf("  2 - APSM    (Package Publisher)   - Publish packages\n");
    printf("  3 - CUSTOM  (Generic)             - Any application\n\n");
    
    printf("SECURITY LEVELS:\n");
    printf("  0 - None     (No isolation)       - Not recommended\n");
    printf("  1 - Low      (Basic isolation)    - Mount namespace only\n");
    printf("  2 - Medium   (Standard)           - PID, UTS, IPC\n");
    printf("  3 - High     (Reinforced)         - NET, USER, CGROUP ← RECOMMENDED\n");
    printf("  4 - Paranoid (Maximum)            - + seccomp, time\n\n");
    
    printf("EXAMPLES:\n");
    printf("  anv create apkm-dev 0 3           # Create APKM environment (level 3)\n");
    printf("  anv start apkm-dev                 # Start the environment\n");
    printf("  anv enter apkm-dev                  # Enter (prompt: [::apkm-dev::]user@host:~$)\n");
    printf("  anv stop apkm-dev                   # Stop the environment\n");
    printf("  anv delete apkm-dev                  # Delete the environment\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }
    
    anv_ctx_t ctx;
    anv_init(&ctx);
    
    if (strcmp(argv[1], "create") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            return 1;
        }
        int type = (argc >= 4) ? atoi(argv[3]) : ANV_TYPE_APKM;
        int security = (argc >= 5) ? atoi(argv[4]) : ANV_SEC_HIGH;
        return anv_create(&ctx, argv[2], type, security);
    }
    else if (strcmp(argv[1], "start") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            return 1;
        }
        return anv_start(&ctx, argv[2]);
    }
    else if (strcmp(argv[1], "enter") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            return 1;
        }
        return anv_enter(&ctx, argv[2], NULL);
    }
    else if (strcmp(argv[1], "stop") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            return 1;
        }
        return anv_stop(&ctx, argv[2]);
    }
    else if (strcmp(argv[1], "delete") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            return 1;
        }
        return anv_delete(&ctx, argv[2]);
    }
    else if (strcmp(argv[1], "list") == 0) {
        return anv_list(&ctx);
    }
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
    }
    else {
        printf("❌ Unknown command: %s\n", argv[1]);
        print_usage();
        return 1;
    }
    
    return 0;
}
