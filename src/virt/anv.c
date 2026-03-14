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
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

// ============================================================================
// CONSTANTES
// ============================================================================
#define SUPERSU_URL "https://supersuroot.org/downloads/supersu-2-82.apk"
#define SUPERSU_PATH "/usr/local/share/anv/supersu.apk"
#define DOCKER_CHECK "docker --version > /dev/null 2>&1"
#define MAX_CMD 4096
#define STACK_SIZE (8 * 1024 * 1024)

// ============================================================================
// STRUCTURES POUR CURL
// ============================================================================
struct MemoryStruct {
    char *memory;
    size_t size;
};

// ============================================================================
// DÉCLARATIONS ANTICIPÉES
// ============================================================================
static int install_supersu(anv_env_t *env);
static int verify_supersu_in_env(anv_env_t *env);
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
static int supersu_already_downloaded(void);
static int ensure_supersu_downloaded(void);
static int child_process(anv_env_t *env);
static double time_execution(struct timeval start, struct timeval end);

// ============================================================================
// FICHIER DE LOG
// ============================================================================
static FILE *log_file = NULL;

static void debug_log(const char *format, ...) {
    if (!log_file) {
        log_file = fopen("/tmp/anv-debug.log", "a");
    }
    if (log_file) {
        va_list args;
        va_start(args, format);
        vfprintf(log_file, format, args);
        fprintf(log_file, "\n");
        fflush(log_file);
        va_end(args);
    }
}

// ============================================================================
// VÉRIFICATION SUPERSU DANS L'ENVIRONNEMENT
// ============================================================================
static int verify_supersu_in_env(anv_env_t *env) {
    debug_log("verify_supersu_in_env: checking locations");
    int found = 0;
    char path[ANV_PATH_MAX];
    
    const char *locations[] = {
        "/system/bin/su",
        "/bin/su",
        "/sbin/su",
        "/usr/bin/su",
        "/system/xbin/su",
        NULL
    };
    
    for (int i = 0; locations[i]; i++) {
        snprintf(path, sizeof(path), "%s%s", env->rootfs, locations[i]);
        debug_log("verify_supersu_in_env: checking %s", path);
        if (access(path, F_OK) == 0) {
            struct stat st;
            stat(path, &st);
            debug_log("verify_supersu_in_env: found at %s, mode=%o", locations[i], st.st_mode);
            
            if (!(st.st_mode & S_ISUID)) {
                debug_log("verify_supersu_in_env: fixing setuid bit");
                chmod(path, 04755);
            }
            found = 1;
        }
    }
    
    if (!found) {
        debug_log("verify_supersu_in_env: not found, installing");
        return install_supersu(env);
    }
    
    debug_log("verify_supersu_in_env: success");
    return ANV_OK;
}

// ============================================================================
// TÉLÉCHARGEMENT SUPERSU
// ============================================================================
static int supersu_already_downloaded(void) {
    int exists = (access(SUPERSU_PATH, F_OK) == 0);
    debug_log("supersu_already_downloaded: %d", exists);
    return exists;
}

static int ensure_supersu_downloaded(void) {
    debug_log("ensure_supersu_downloaded: checking");
    if (supersu_already_downloaded()) {
        return ANV_OK;
    }
    debug_log("ensure_supersu_downloaded: downloading");
    return anv_download_supersu();
}

int anv_download_supersu(void) {
    debug_log("anv_download_supersu: starting download");
    CURL *curl_handle;
    CURLcode res = CURLE_OK;
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
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
        
        res = curl_easy_perform(curl_handle);
        
        if(res == CURLE_OK) {
            mkdir("/usr/local/share/anv", 0755);
            
            FILE *fp = fopen(SUPERSU_PATH, "wb");
            if(fp) {
                fwrite(chunk.memory, 1, chunk.size, fp);
                fclose(fp);
                chmod(SUPERSU_PATH, 0644);
                debug_log("anv_download_supersu: download successful, %zu bytes", chunk.size);
            } else {
                debug_log("anv_download_supersu: failed to save file");
            }
        } else {
            debug_log("anv_download_supersu: curl error: %s", curl_easy_strerror(res));
        }
        
        curl_easy_cleanup(curl_handle);
        free(chunk.memory);
    }
    
    curl_global_cleanup();
    return (res == CURLE_OK) ? ANV_OK : ANV_ERR_DOWNLOAD;
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// ============================================================================
// INSTALLATION SUPERSU
// ============================================================================
static int install_supersu(anv_env_t *env) {
    debug_log("install_supersu: starting installation");
    if (ensure_supersu_downloaded() != ANV_OK) {
        debug_log("install_supersu: download failed");
        return ANV_ERR_DOWNLOAD;
    }
    
    mkdir_p(env->rootfs, "/system/bin");
    mkdir_p(env->rootfs, "/system/app/SuperSU");
    mkdir_p(env->rootfs, "/system/xbin");
    
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "cp %s %s/ 2>/dev/null", SUPERSU_PATH, env->rootfs);
    debug_log("install_supersu: running %s", cmd);
    system(cmd);
    
    snprintf(cmd, sizeof(cmd), 
             "cd %s && unzip -o supersu.apk -d system/ > /dev/null 2>&1", 
             env->rootfs);
    debug_log("install_supersu: running %s", cmd);
    system(cmd);
    
    char su_path[ANV_PATH_MAX];
    snprintf(su_path, sizeof(su_path), "%s/system/bin/su", env->rootfs);
    debug_log("install_supersu: creating su at %s", su_path);
    
    FILE *f = fopen(su_path, "w");
    if (f) {
        fprintf(f, "#!/system/bin/sh\n");
        fprintf(f, "# SuperSU for ANV environments\n");
        fprintf(f, "export PATH=/system/bin:/system/xbin:/bin:/sbin:/usr/bin:$PATH\n");
        fprintf(f, "if [ \"$1\" = \"-c\" ]; then\n");
        fprintf(f, "    shift\n");
        fprintf(f, "    sh -c \"$@\"\n");
        fprintf(f, "elif [ \"$1\" = \"--help\" ] || [ \"$1\" = \"-h\" ]; then\n");
        fprintf(f, "    echo 'SuperSU for ANV environments'\n");
        fprintf(f, "    echo 'Usage: su [options] [command]'\n");
        fprintf(f, "else\n");
        fprintf(f, "    sh\n");
        fprintf(f, "fi\n");
        fclose(f);
        chmod(su_path, 04755);
        debug_log("install_supersu: su created and setuid set");
    }
    
    char link_path[ANV_PATH_MAX];
    snprintf(link_path, sizeof(link_path), "%s/bin/su", env->rootfs);
    unlink(link_path);
    symlink(su_path, link_path);
    
    snprintf(link_path, sizeof(link_path), "%s/sbin/su", env->rootfs);
    unlink(link_path);
    symlink(su_path, link_path);
    
    snprintf(link_path, sizeof(link_path), "%s/system/xbin/su", env->rootfs);
    unlink(link_path);
    symlink(su_path, link_path);
    
    debug_log("install_supersu: done");
    return ANV_OK;
}

// ============================================================================
// DÉTECTION ROOT
// ============================================================================
int anv_check_root(void) {
    static int root_checked = 0;
    
    if (geteuid() == 0) {
        if (!root_checked) {
            printf("\033[1;31m");
            printf("╔════════════════════════════════════════════════════╗\n");
            printf("║  [track backsh >_< root no secure]               ║\n");
            printf("║  Running as root - preparing SuperSU...          ║\n");
            printf("╚════════════════════════════════════════════════════╝\n");
            printf("\033[0m");
            fflush(stdout);
            
            debug_log("anv_check_root: root detected");
            ensure_supersu_downloaded();
            root_checked = 1;
        }
        return ANV_OK;
    }
    return ANV_OK;
}

// ============================================================================
// VÉRIFICATION DOCKER
// ============================================================================
int anv_check_docker(void) {
    static int docker_checked = 0;
    static int docker_available = 0;
    
    if (!docker_checked) {
        docker_available = (system(DOCKER_CHECK) == 0);
        debug_log("anv_check_docker: available=%d", docker_available);
        docker_checked = 1;
    }
    return docker_available;
}

// ============================================================================
// INITIALISATION
// ============================================================================
int anv_init(anv_ctx_t *ctx) {
    static int initialized = 0;
    
    if (initialized) return ANV_OK;
    
    memset(ctx, 0, sizeof(anv_ctx_t));
    debug_log("anv_init: starting");
    
    anv_check_root();
    ctx->docker_available = anv_check_docker();
    
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(ctx->base_path, sizeof(ctx->base_path), "%s/.anv", home);
    
    mkdir(ctx->base_path, 0755);
    mkdir("/usr/local/share/anv", 0755);
    
    ctx->default_security = ANV_SEC_HIGH;
    ctx->verbose = 0;
    initialized = 1;
    
    debug_log("anv_init: done, base_path=%s", ctx->base_path);
    return ANV_OK;
}

// ============================================================================
// FONCTIONS DE CONFIGURATION
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
    debug_log("setup_rootfs: creating at %s", env->rootfs);
    
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
        debug_log("setup_rootfs: created %s", path);
    }
    
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
            debug_log("setup_rootfs: copying %s to %s", apkm_bins[i], dest);
            system(cmd);
        }
    }
    
    char conf_path[ANV_PATH_MAX];
    snprintf(conf_path, sizeof(conf_path), "%s/etc/apkm/repositories.conf", env->rootfs);
    mkdir_p(env->rootfs, "/etc/apkm");
    
    FILE *f = fopen(conf_path, "w");
    if (f) {
        fprintf(f, "# APKM Repositories\n");
        fprintf(f, "zarch-hub https://gsql-badge.onrender.com 5\n");
        fclose(f);
        debug_log("setup_rootfs: created APKM config at %s", conf_path);
    }
    
    return ANV_OK;
}

// ============================================================================
// FONCTION DU PROCESSUS ENFANT
// ============================================================================
static int child_process(anv_env_t *env) {
    debug_log("child_process: started with PID %d", getpid());
    
    // Créer les namespaces
    debug_log("child_process: creating namespaces");
    if (unshare(CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID) == -1) {
        debug_log("child_process: unshare failed: %s", strerror(errno));
        return 1;
    }
    debug_log("child_process: namespaces created");
    
    // Configurer les mounts
    debug_log("child_process: setting mount propagation private");
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    
    // Monter /proc
    char proc_path[ANV_PATH_MAX];
    snprintf(proc_path, sizeof(proc_path), "%s/proc", env->rootfs);
    mkdir_p(env->rootfs, "/proc");
    debug_log("child_process: mounting proc at %s", proc_path);
    if (mount("proc", proc_path, "proc", 0, NULL) == -1) {
        debug_log("child_process: mount proc failed: %s", strerror(errno));
    }
    
    // Monter /dev
    char dev_path[ANV_PATH_MAX];
    snprintf(dev_path, sizeof(dev_path), "%s/dev", env->rootfs);
    mkdir_p(env->rootfs, "/dev");
    debug_log("child_process: mounting dev at %s", dev_path);
    if (mount("tmpfs", dev_path, "tmpfs", 0, "size=10M") == -1) {
        debug_log("child_process: mount dev failed: %s", strerror(errno));
    }
    
    // Créer les périphériques
    debug_log("child_process: creating devices");
    create_devices(env);
    
    // Chroot
    debug_log("child_process: chroot to %s", env->rootfs);
    if (chroot(env->rootfs) != 0) {
        debug_log("child_process: chroot failed: %s", strerror(errno));
        return 1;
    }
    chdir("/");
    debug_log("child_process: chroot successful");
    
    // Set hostname
    sethostname(env->hostname, strlen(env->hostname));
    debug_log("child_process: hostname set to %s", env->hostname);
    
    // Installer NamesBar
    debug_log("child_process: installing NamesBar");
    install_namesbar(env);
    
    // Installer SuperSU
    debug_log("child_process: installing SuperSU");
    install_supersu(env);
    
    // Créer un fichier pour signaler que le processus est prêt
    char ready_path[ANV_PATH_MAX];
    snprintf(ready_path, sizeof(ready_path), "%s/ready", env->path);
    debug_log("child_process: creating ready file at %s", ready_path);
    FILE *f = fopen(ready_path, "w");
    if (f) {
        fprintf(f, "%d", getpid());
        fclose(f);
        debug_log("child_process: ready file created");
    } else {
        debug_log("child_process: failed to create ready file: %s", strerror(errno));
    }
    
    // Lancer le shell
    debug_log("child_process: launching shell");
    execl("/bin/sh", "sh", NULL);
    debug_log("child_process: execl failed: %s", strerror(errno));
    return 1;
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
    fflush(stdout);
    
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
    
    env.security.no_network = (security >= ANV_SEC_HIGH) ? 1 : 0;
    env.security.read_only = (security >= ANV_SEC_MEDIUM) ? 1 : 0;
    env.security.no_new_privs = (security >= ANV_SEC_HIGH) ? 1 : 0;
    
    mkdir(env.path, 0755);
    
    if (setup_rootfs(&env) != ANV_OK) {
        printf("❌ Rootfs creation failed\n");
        return ANV_ERR_MOUNT;
    }
    
    save_env_config(&env);
    
    printf("✅ Environment created: %s\n", env.rootfs);
    printf("   Security level: %d\n", security);
    printf("   NamesBar: %s\n", ANV_NAMESBAR);
    fflush(stdout);
    
    return ANV_OK;
}

// ============================================================================
// API PUBLIQUE - DÉMARRAGE
// ============================================================================
int anv_start(anv_ctx_t *ctx, const char *name) {
    printf("🚀 Starting environment: %s\n", name);
    fflush(stdout);
    debug_log("anv_start: starting %s", name);
    
    anv_env_t env;
    if (load_env_config(ctx, name, &env) != ANV_OK) {
        printf("❌ Environment '%s' not found\n", name);
        debug_log("anv_start: environment not found");
        return ANV_ERR_NOENV;
    }
    debug_log("anv_start: loaded config for %s, rootfs=%s", name, env.rootfs);
    
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/%s/pid", ctx->base_path, name);
    
    char ready_path[ANV_PATH_MAX];
    snprintf(ready_path, sizeof(ready_path), "%s/%s/ready", ctx->base_path, name);
    
    // Nettoyer les anciens fichiers
    unlink(pid_path);
    unlink(ready_path);
    debug_log("anv_start: cleaned old files");
    
    pid_t pid = fork();
    debug_log("anv_start: fork returned %d", pid);
    
    if (pid == -1) {
        perror("fork");
        debug_log("anv_start: fork failed: %s", strerror(errno));
        return ANV_ERR_NS;
    }
    
    if (pid == 0) {
        // Processus fils
        debug_log("anv_start: in child process");
        int ret = child_process(&env);
        debug_log("anv_start: child process exiting with %d", ret);
        exit(ret);
    }
    
    // Processus père
    debug_log("anv_start: waiting for child %d to create ready file", pid);
    
    // Attendre que le fichier ready soit créé (avec timeout de 10s pour sécurité)
    int waited = 0;
    while (waited < 100) { // 10 secondes max (100 * 100ms)
        if (access(ready_path, F_OK) == 0) {
            debug_log("anv_start: ready file found after %dms", waited * 100);
            break;
        }
        usleep(100000); // 100ms
        waited++;
    }
    
    if (waited >= 100) {
        debug_log("anv_start: TIMEOUT waiting for ready file");
        printf("⚠️  Timeout waiting for child process\n");
    }
    
    // Vérifier si le processus existe toujours
    if (kill(pid, 0) == 0) {
        debug_log("anv_start: child process %d is alive", pid);
        
        // Sauvegarder le PID
        env.init_pid = pid;
        env.is_running = 1;
        
        FILE *f = fopen(pid_path, "w");
        if (f) {
            fprintf(f, "%d", pid);
            fclose(f);
            debug_log("anv_start: saved PID to %s", pid_path);
        }
        
        printf("✅ Environment started with PID: %d\n", pid);
        printf("   NamesBar: %s\n", ANV_NAMESBAR);
        printf("   To enter: anv enter %s\n", name);
        fflush(stdout);
    } else {
        debug_log("anv_start: child process %d is dead", pid);
        printf("❌ Child process died during initialization\n");
        return ANV_ERR_NS;
    }
    
    return ANV_OK;
}

// ============================================================================
// API PUBLIQUE - ENTRER
// ============================================================================
int anv_enter(anv_ctx_t *ctx, const char *name, char *const argv[]) {
    (void)argv;
    
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
    
    // Vérifier que le processus existe toujours
    if (kill(pid, 0) != 0) {
        printf("❌ Environment process is dead\n");
        unlink(pid_path);
        return ANV_ERR_NOENV;
    }
    
    // Vérifier que les namespaces existent
    char ns_path[ANV_PATH_MAX];
    snprintf(ns_path, sizeof(ns_path), "/proc/%d/ns", pid);
    if (access(ns_path, F_OK) != 0) {
        printf("❌ Namespaces not ready\n");
        return ANV_ERR_NS;
    }
    
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
    printf("   Type 'exit' to leave\n\n");
    fflush(stdout);
    
    int ret = system(cmd);
    
    if (ret != 0) {
        // Alternative avec sh direct
        snprintf(cmd, sizeof(cmd), 
                 "nsenter -t %d -m -u -i -p /bin/sh",
                 pid);
        system(cmd);
    }
    
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
    
    kill(pid, SIGTERM);
    usleep(100000);
    kill(pid, SIGKILL);
    
    unlink(pid_path);
    
    char ready_path[ANV_PATH_MAX];
    snprintf(ready_path, sizeof(ready_path), "%s/%s/ready", ctx->base_path, name);
    unlink(ready_path);
    
    printf("✅ Environment '%s' stopped\n", name);
    
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
    
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", env_path);
    if (access(pid_path, F_OK) == 0) {
        printf("⚠️  Environment is still running. Stopping first...\n");
        anv_stop(ctx, name);
        usleep(100000);
    }
    
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", env_path);
    int ret = system(cmd);
    
    if (ret == 0) {
        printf("✅ Environment '%s' deleted\n", name);
        return ANV_OK;
    } else {
        printf("❌ Failed to delete environment '%s'\n", name);
        return ANV_ERR_NOENV;
    }
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
        
        char pid_path[ANV_PATH_MAX];
        snprintf(pid_path, sizeof(pid_path), "%s/%s/pid", ctx->base_path, entry->d_name);
        int running = (access(pid_path, F_OK) == 0);
        
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
    }
    
    closedir(dir);
    printf("═══════════════════════════════════════════════════════════════════\n\n");
    
    return ANV_OK;
}

// ============================================================================
// FONCTIONS UTILITAIRES
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

static int create_devices(anv_env_t *env) {
    debug_log("create_devices: creating device nodes");
    char dev_path[ANV_PATH_MAX];
    snprintf(dev_path, sizeof(dev_path), "%s/dev", env->rootfs);
    
    char path[ANV_PATH_MAX];
    
    snprintf(path, sizeof(path), "%s/null", dev_path);
    mknod(path, S_IFCHR | 0666, makedev(1, 3));
    debug_log("create_devices: created %s", path);
    
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

static int install_namesbar(anv_env_t *env) {
    char namesbar_path[ANV_PATH_MAX];
    snprintf(namesbar_path, sizeof(namesbar_path), "%s/usr/bin/%s", 
             env->rootfs, ANV_NAMESBAR);
    
    mkdir_p(env->rootfs, "/usr/bin");
    debug_log("install_namesbar: creating at %s", namesbar_path);
    
    FILE *f = fopen(namesbar_path, "w");
    if (!f) return -1;
    
    fprintf(f, "#!/bin/sh\n");
    fprintf(f, "# NamesBar - Secure ANV Shell\n");
    fprintf(f, "export PS1='[::%s::]\\u@\\h:\\w$ '\n", env->name);
    fprintf(f, "export ANV_ENV='%s'\n", env->name);
    fprintf(f, "export ANV_SEC_LEVEL='%d'\n", env->security_level);
    fprintf(f, "export APKM_REPO='https://gsql-badge.onrender.com'\n");
    fprintf(f, "if [ -f /system/bin/su ]; then\n");
    fprintf(f, "    export PATH=/system/bin:$PATH\n");
    fprintf(f, "fi\n");
    fprintf(f, "exec /bin/sh \"$@\"\n");
    
    fclose(f);
    chmod(namesbar_path, 0755);
    
    char sh_path[ANV_PATH_MAX];
    snprintf(sh_path, sizeof(sh_path), "%s/bin/sh", env->rootfs);
    unlink(sh_path);
    symlink(namesbar_path, sh_path);
    debug_log("install_namesbar: done");
    
    return ANV_OK;
}

static int save_env_config(anv_env_t *env) {
    char config_path[ANV_PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/config", env->path);
    
    FILE *f = fopen(config_path, "w");
    if (!f) return -1;
    
    fprintf(f, "ANV_ENV=%s\n", env->name);
    fprintf(f, "TYPE=%d\n", env->type);
    fprintf(f, "SECURITY_LEVEL=%d\n", env->security_level);
    fprintf(f, "CREATED=%lld\n", (long long)env->created);
    fprintf(f, "HOST_UID=%d\n", env->host_uid);
    fprintf(f, "HOST_GID=%d\n", env->host_gid);
    fprintf(f, "DOCKER_ENABLED=%d\n", env->docker_enabled);
    
    fclose(f);
    debug_log("save_env_config: saved to %s", config_path);
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
    
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", env->path);
    f = fopen(pid_path, "r");
    if (f) {
        fscanf(f, "%d", &env->init_pid);
        env->is_running = 1;
        fclose(f);
    }
    
    debug_log("load_env_config: loaded %s, rootfs=%s", name, env->rootfs);
    return ANV_OK;
}

// ============================================================================
// MAIN
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
    // Ouvrir le fichier de log
    log_file = fopen("/tmp/anv-debug.log", "w");
    if (log_file) {
        fprintf(log_file, "ANV debug log started\n");
        fflush(log_file);
    }
    
    debug_log("main: starting with %d args", argc);
    
    if (argc < 2) {
        print_usage();
        if (log_file) fclose(log_file);
        return 0;
    }
    
    anv_ctx_t ctx;
    anv_init(&ctx);
    
    int result = 0;
    
    if (strcmp(argv[1], "create") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            result = 1;
        } else {
            int type = (argc >= 4) ? atoi(argv[3]) : ANV_TYPE_APKM;
            int security = (argc >= 5) ? atoi(argv[4]) : ANV_SEC_HIGH;
            result = anv_create(&ctx, argv[2], type, security);
        }
    }
    else if (strcmp(argv[1], "start") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            result = 1;
        } else {
            result = anv_start(&ctx, argv[2]);
        }
    }
    else if (strcmp(argv[1], "enter") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            result = 1;
        } else {
            result = anv_enter(&ctx, argv[2], NULL);
        }
    }
    else if (strcmp(argv[1], "stop") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            result = 1;
        } else {
            result = anv_stop(&ctx, argv[2]);
        }
    }
    else if (strcmp(argv[1], "delete") == 0) {
        if (argc < 3) {
            printf("❌ Missing environment name\n");
            result = 1;
        } else {
            result = anv_delete(&ctx, argv[2]);
        }
    }
    else if (strcmp(argv[1], "list") == 0) {
        result = anv_list(&ctx);
    }
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
    }
    else {
        printf("❌ Unknown command: %s\n", argv[1]);
        print_usage();
        result = 1;
    }
    
    debug_log("main: exiting with %d", result);
    if (log_file) fclose(log_file);
    return result;
}
