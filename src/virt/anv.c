#include "anv.h"
#include <libgen.h>
#include <wordexp.h>
#include <sys/statvfs.h>

// ============================================================================
// DÉTECTION ROOT ET MESSAGES DE SÉCURITÉ
// ============================================================================

int anv_check_root(void) {
    if (geteuid() == 0) {
        printf("\033[1;31m");
        printf("╔════════════════════════════════════════════════════╗\n");
        printf("║  [track backsh >_< root no secure]               ║\n");
        printf("║  ANV ne doit PAS être exécuté en root !          ║\n");
        printf("║  Utilisez un utilisateur standard pour la sécurité║\n");
        printf("╚════════════════════════════════════════════════════╝\n");
        printf("\033[0m");
        return ANV_ERR_ROOT;
    }
    return ANV_OK;
}

// ============================================================================
// INITIALISATION
// ============================================================================

int anv_init(anv_ctx_t *ctx) {
    memset(ctx, 0, sizeof(anv_ctx_t));
    
    // Vérifier root
    if (anv_check_root() != ANV_OK) {
        return ANV_ERR_ROOT;
    }
    
    // Définir le chemin de base
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(ctx->base_path, sizeof(ctx->base_path), "%s/.anv", home);
    
    // Créer le répertoire de base
    mkdir(ctx->base_path, 0755);
    
    // Charger les environnements existants
    ctx->default_security = ANV_SEC_HIGH;
    ctx->verbose = 1;
    
    printf("🔒 ANV v%s - Advanced Namespace Virtualization\n", ANV_VERSION);
    printf("   Base path: %s\n", ctx->base_path);
    printf("   Security level: %s\n", 
           ctx->default_security == ANV_SEC_HIGH ? "High" : "Medium");
    
    return ANV_OK;
}

// ============================================================================
// CRÉATION D'ENVIRONNEMENT NAMESPACE
// ============================================================================

static int setup_namespaces(anv_env_t *env) {
    int flags = 0;
    
    // Namespaces à activer selon niveau de sécurité
    if (env->security_level >= ANV_SEC_LOW) {
        flags |= CLONE_NEWNS;    // Mount namespace
    }
    if (env->security_level >= ANV_SEC_MEDIUM) {
        flags |= CLONE_NEWUTS;    // UTS namespace (hostname)
        flags |= CLONE_NEWIPC;    // IPC namespace
        flags |= CLONE_NEWPID;    // PID namespace
    }
    if (env->security_level >= ANV_SEC_HIGH) {
        flags |= CLONE_NEWNET;    // Network namespace (isolé)
        flags |= CLONE_NEWUSER;   // User namespace (map utilisateur)
        flags |= CLONE_NEWCGROUP; // Cgroup namespace
    }
    if (env->security_level >= ANV_SEC_PARANOID) {
        flags |= CLONE_NEWTIME;   // Time namespace
    }
    
    // Créer les namespaces avec unshare
    if (unshare(flags) == -1) {
        perror("unshare");
        return ANV_ERR_NS;
    }
    
    // Configurer le user namespace mapping
    if (flags & CLONE_NEWUSER) {
        char map_path[256];
        snprintf(map_path, sizeof(map_path), "/proc/%d/uid_map", getpid());
        FILE *f = fopen(map_path, "w");
        if (f) {
            fprintf(f, "0 %d 1", env->host_uid);
            fclose(f);
        }
        
        snprintf(map_path, sizeof(map_path), "/proc/%d/gid_map", getpid());
        f = fopen(map_path, "w");
        if (f) {
            fprintf(f, "0 %d 1", env->host_gid);
            fclose(f);
        }
    }
    
    return ANV_OK;
}

static int setup_rootfs(anv_env_t *env) {
    char path[ANV_PATH_MAX];
    
    // Créer la structure du rootfs
    snprintf(env->rootfs, sizeof(env->rootfs), "%s/%s/rootfs", env->path, env->name);
    
    // Dossiers système
    const char *dirs[] = {
        "bin", "sbin", "usr/bin", "usr/sbin", "usr/lib",
        "etc", "home", "tmp", "var", "proc", "sys", "dev",
        "run", "mnt", "media", "opt", "srv", "root",
        NULL
    };
    
    for (int i = 0; dirs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", env->rootfs, dirs[i]);
        mkdir(path, 0755);
    }
    
    // Copier les binaires essentiels
    const char *bins[] = {
        "/bin/sh", "/bin/bash", "/bin/ls", "/bin/cat",
        "/bin/ps", "/bin/mount", "/bin/umount", "/bin/grep",
        "/usr/bin/apkm", "/usr/bin/bool", "/usr/bin/apsm",
        NULL
    };
    
    for (int i = 0; bins[i]; i++) {
        if (access(bins[i], F_OK) == 0) {
            char dest[ANV_PATH_MAX];
            snprintf(dest, sizeof(dest), "%s%s", env->rootfs, bins[i]);
            
            char *dir = dirname(strdup(dest));
            mkdir(dir, 0755);
            
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "cp %s %s 2>/dev/null", bins[i], dest);
            system(cmd);
        }
    }
    
    return ANV_OK;
}

static int setup_mounts(anv_env_t *env) {
    // Rendre le mount namespace privé
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
    
    // Monter proc
    char proc_path[ANV_PATH_MAX];
    snprintf(proc_path, sizeof(proc_path), "%s/proc", env->rootfs);
    mount("proc", proc_path, "proc", 0, NULL);
    
    // Monter sysfs si demandé
    if (!env->security.no_network) {
        char sys_path[ANV_PATH_MAX];
        snprintf(sys_path, sizeof(sys_path), "%s/sys", env->rootfs);
        mount("sysfs", sys_path, "sysfs", 0, NULL);
    }
    
    // Monter tmpfs
    char tmp_path[ANV_PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s/tmp", env->rootfs);
    mount("tmpfs", tmp_path, "tmpfs", 0, "size=100M");
    
    // Monter dev
    char dev_path[ANV_PATH_MAX];
    snprintf(dev_path, sizeof(dev_path), "%s/dev", env->rootfs);
    mount("tmpfs", dev_path, "tmpfs", 0, "size=10M");
    
    // Créer les périphériques essentiels
    char null_path[ANV_PATH_MAX];
    snprintf(null_path, sizeof(null_path), "%s/null", dev_path);
    mknod(null_path, S_IFCHR | 0666, makedev(1, 3));
    
    char zero_path[ANV_PATH_MAX];
    snprintf(zero_path, sizeof(zero_path), "%s/zero", dev_path);
    mknod(zero_path, S_IFCHR | 0666, makedev(1, 5));
    
    char random_path[ANV_PATH_MAX];
    snprintf(random_path, sizeof(random_path), "%s/random", dev_path);
    mknod(random_path, S_IFCHR | 0666, makedev(1, 8));
    
    char urandom_path[ANV_PATH_MAX];
    snprintf(urandom_path, sizeof(urandom_path), "%s/urandom", dev_path);
    mknod(urandom_path, S_IFCHR | 0666, makedev(1, 9));
    
    char tty_path[ANV_PATH_MAX];
    snprintf(tty_path, sizeof(tty_path), "%s/tty", dev_path);
    mknod(tty_path, S_IFCHR | 0666, makedev(5, 0));
    
    return ANV_OK;
}

// ============================================================================
// NAMESBAR - SHELL SÉCURISÉ
// ============================================================================

static void setup_namesbar(anv_env_t *env) {
    // Créer le binaire namesbar (wrapper)
    char namesbar_path[ANV_PATH_MAX];
    snprintf(namesbar_path, sizeof(namesbar_path), "%s/usr/bin/%s", 
             env->rootfs, ANV_NAMESBAR);
    
    FILE *f = fopen(namesbar_path, "w");
    if (!f) return;
    
    fprintf(f, "#!/bin/sh\n");
    fprintf(f, "# NamesBar - Secure ANV Shell\n");
    fprintf(f, "export PS1='[::%s::]\\u@\\h:\\w$ '\n", env->name);
    fprintf(f, "export ANV_ENV='%s'\n", env->name);
    fprintf(f, "export ANV_SEC_LEVEL='%d'\n", env->security_level);
    fprintf(f, "exec /bin/sh \"$@\"\n");
    
    fclose(f);
    chmod(namesbar_path, 0755);
    
    // Créer un lien symbolique
    char sh_path[ANV_PATH_MAX];
    snprintf(sh_path, sizeof(sh_path), "%s/bin/sh", env->rootfs);
    unlink(sh_path);
    symlink("/usr/bin/__namesbar", sh_path);
}

// ============================================================================
// CHROOT ET SÉCURITÉ
// ============================================================================

static int apply_security(anv_env_t *env) {
    // Chroot dans le rootfs
    if (chroot(env->rootfs) != 0) {
        perror("chroot");
        return ANV_ERR_MOUNT;
    }
    chdir("/");
    
    // no_new_privs (empêche les escalades de privilèges)
    if (env->security.no_new_privs) {
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    }
    
    // Drop capabilities
    cap_t caps = cap_get_proc();
    cap_clear(caps);
    cap_set_proc(caps);
    cap_free(caps);
    
    // Changer l'utilisateur (si user namespace)
    if (env->security_level >= ANV_SEC_HIGH) {
        setgid(0);
        setuid(0);
    }
    
    return ANV_OK;
}

// ============================================================================
// PID 1 (INIT) DANS LE NAMESPACE
// ============================================================================

static int child_func(void *arg) {
    anv_env_t *env = (anv_env_t *)arg;
    
    // Configurer les namespaces
    if (setup_namespaces(env) != ANV_OK) {
        return ANV_ERR_NS;
    }
    
    // Configurer les mounts
    if (setup_mounts(env) != ANV_OK) {
        return ANV_ERR_MOUNT;
    }
    
    // Configurer namesbar
    setup_namesbar(env);
    
    // Appliquer la sécurité
    if (apply_security(env) != ANV_OK) {
        return ANV_ERR_CAP;
    }
    
    // Set hostname
    sethostname(env->hostname, strlen(env->hostname));
    
    // Lancer le shell
    execl("/bin/sh", "sh", NULL);
    
    return ANV_OK;
}

// ============================================================================
// API PUBLIQUE
// ============================================================================

int anv_create(anv_ctx_t *ctx, const char *name, int type, int security) {
    // Vérifier root
    if (anv_check_root() != ANV_OK) {
        return ANV_ERR_ROOT;
    }
    
    printf("🔐 Création de l'environnement ANV: %s\n", name);
    
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
    
    snprintf(env.hostname, sizeof(env.hostname), "anv-%s", name);
    snprintf(env.path, sizeof(env.path), "%s/%s", ctx->base_path, name);
    snprintf(env.prompt_prefix, sizeof(env.prompt_prefix), "::%s::", name);
    
    // Configuration sécurité par défaut
    env.security.no_network = (security >= ANV_SEC_HIGH) ? 1 : 0;
    env.security.read_only = (security >= ANV_SEC_MEDIUM) ? 1 : 0;
    env.security.no_new_privs = (security >= ANV_SEC_HIGH) ? 1 : 0;
    env.security.seccomp = (security >= ANV_SEC_PARANOID) ? 1 : 0;
    
    // Créer le répertoire
    mkdir(env.path, 0755);
    
    // Configurer le rootfs
    if (setup_rootfs(&env) != ANV_OK) {
        printf("❌ Erreur création rootfs\n");
        return ANV_ERR_MOUNT;
    }
    
    // Sauvegarder la configuration
    char config_path[ANV_PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/config", env.path);
    
    FILE *f = fopen(config_path, "w");
    if (f) {
        fprintf(f, "ANV_ENV=%s\n", env.name);
        fprintf(f, "SECURITY_LEVEL=%d\n", env.security_level);
        fprintf(f, "TYPE=%d\n", env.type);
        fprintf(f, "CREATED=%ld\n", env.created);
        fprintf(f, "HOST_UID=%d\n", env.host_uid);
        fprintf(f, "HOST_GID=%d\n", env.host_gid);
        fclose(f);
    }
    
    printf("✅ Environnement créé: %s\n", env.rootfs);
    printf("   Sécurité: niveau %d\n", security);
    printf("   NamesBar: %s\n", ANV_NAMESBAR);
    
    return ANV_OK;
}

int anv_start(anv_ctx_t *ctx, const char *name) {
    // Vérifier root
    if (anv_check_root() != ANV_OK) {
        return ANV_ERR_ROOT;
    }
    
    printf("🚀 Démarrage de l'environnement: %s\n", name);
    
    anv_env_t env;
    char config_path[ANV_PATH_MAX];
    snprintf(env.path, sizeof(env.path), "%s/%s", ctx->base_path, name);
    snprintf(config_path, sizeof(config_path), "%s/config", env.path);
    
    // Charger la configuration
    FILE *f = fopen(config_path, "r");
    if (!f) {
        printf("❌ Environnement non trouvé\n");
        return ANV_ERR_NOENV;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64], value[192];
        if (sscanf(line, "%63[^=]=%191s", key, value) == 2) {
            if (strcmp(key, "ANV_ENV") == 0) strcpy(env.name, value);
            else if (strcmp(key, "SECURITY_LEVEL") == 0) env.security_level = atoi(value);
            else if (strcmp(key, "TYPE") == 0) env.type = atoi(value);
            else if (strcmp(key, "HOST_UID") == 0) env.host_uid = atoi(value);
            else if (strcmp(key, "HOST_GID") == 0) env.host_gid = atoi(value);
        }
    }
    fclose(f);
    
    snprintf(env.rootfs, sizeof(env.rootfs), "%s/rootfs", env.path);
    snprintf(env.hostname, sizeof(env.hostname), "anv-%s", name);
    env.security.no_network = (env.security_level >= ANV_SEC_HIGH) ? 1 : 0;
    env.security.no_new_privs = (env.security_level >= ANV_SEC_HIGH) ? 1 : 0;
    
    // Stack pour clone (8MB)
    void *stack = malloc(8 * 1024 * 1024);
    void *stack_top = stack + 8 * 1024 * 1024;
    
    // Flags pour clone
    int flags = SIGCHLD;
    if (env.security_level >= ANV_SEC_MEDIUM) flags |= CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWIPC;
    if (env.security_level >= ANV_SEC_HIGH) flags |= CLONE_NEWNET | CLONE_NEWUSER;
    if (env.security_level >= ANV_SEC_PARANOID) flags |= CLONE_NEWCGROUP | CLONE_NEWTIME;
    
    // Créer le processus fils
    pid_t pid = clone(child_func, stack_top, flags, &env);
    
    if (pid == -1) {
        perror("clone");
        free(stack);
        return ANV_ERR_NS;
    }
    
    env.init_pid = pid;
    env.is_running = 1;
    
    printf("✅ Environnement démarré avec PID: %d\n", pid);
    printf("   NamesBar: %s\n", ANV_NAMESBAR);
    printf("   Prompt: [::%s::]user@host:~$ \n", name);
    
    // Sauvegarder le PID
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", env.path);
    f = fopen(pid_path, "w");
    if (f) {
        fprintf(f, "%d", pid);
        fclose(f);
    }
    
    free(stack);
    return ANV_OK;
}

int anv_enter(anv_ctx_t *ctx, const char *name, char *const argv[]) {
    // Vérifier root
    if (anv_check_root() != ANV_OK) {
        return ANV_ERR_ROOT;
    }
    
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/%s/pid", ctx->base_path, name);
    
    FILE *f = fopen(pid_path, "r");
    if (!f) {
        printf("❌ Environnement non démarré\n");
        return ANV_ERR_NOENV;
    }
    
    pid_t pid;
    fscanf(f, "%d", &pid);
    fclose(f);
    
    // Changer le prompt via NSEnter
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
             "nsenter -t %d -m -u -i -n -p -C -- "
             "env PS1='[::%s::]\\u@\\h:\\w$ ' /bin/sh -i",
             pid, name);
    
    printf("🔐 Entrée dans l'environnement %s\n", name);
    printf("   NamesBar actif: %s\n", ANV_NAMESBAR);
    printf("   Pour sortir: exit\n\n");
    
    system(cmd);
    
    return ANV_OK;
}

int anv_stop(anv_ctx_t *ctx, const char *name) {
    // Vérifier root
    if (anv_check_root() != ANV_OK) {
        return ANV_ERR_ROOT;
    }
    
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/%s/pid", ctx->base_path, name);
    
    FILE *f = fopen(pid_path, "r");
    if (!f) {
        printf("❌ Environnement non démarré\n");
        return ANV_ERR_NOENV;
    }
    
    pid_t pid;
    fscanf(f, "%d", &pid);
    fclose(f);
    
    kill(pid, SIGTERM);
    sleep(1);
    kill(pid, SIGKILL);
    
    unlink(pid_path);
    
    printf("✅ Environnement %s arrêté\n", name);
    
    return ANV_OK;
}

int anv_delete(anv_ctx_t *ctx, const char *name) {
    // Vérifier root
    if (anv_check_root() != ANV_OK) {
        return ANV_ERR_ROOT;
    }
    
    char env_path[ANV_PATH_MAX];
    snprintf(env_path, sizeof(env_path), "%s/%s", ctx->base_path, name);
    
    // Vérifier si l'environnement existe
    if (access(env_path, F_OK) != 0) {
        printf("❌ Environnement non trouvé\n");
        return ANV_ERR_NOENV;
    }
    
    // Vérifier s'il tourne
    char pid_path[ANV_PATH_MAX];
    snprintf(pid_path, sizeof(pid_path), "%s/pid", env_path);
    if (access(pid_path, F_OK) == 0) {
        printf("⚠️  L'environnement tourne encore. Arrêt d'abord...\n");
        anv_stop(ctx, name);
    }
    
    // Supprimer
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", env_path);
    system(cmd);
    
    printf("✅ Environnement %s supprimé\n", name);
    
    return ANV_OK;
}

int anv_list(anv_ctx_t *ctx) {
    // Vérifier root
    if (anv_check_root() != ANV_OK) {
        return ANV_ERR_ROOT;
    }
    
    DIR *dir = opendir(ctx->base_path);
    if (!dir) {
        printf("📂 Aucun environnement trouvé\n");
        return ANV_OK;
    }
    
    printf("\n📦 Environnements ANV:\n");
    printf("════════════════════════════════════════════════════\n");
    printf("%-20s %-10s %-12s %-10s %s\n", "NAME", "STATUS", "TYPE", "SECURITY", "CREATED");
    printf("────────────────────────────────────────────────────\n");
    
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        
        char config_path[ANV_PATH_MAX];
        snprintf(config_path, sizeof(config_path), "%s/%s/config", 
                 ctx->base_path, entry->d_name);
        
        FILE *f = fopen(config_path, "r");
        if (!f) continue;
        
        char name[64] = "";
        int type = 0, security = 0;
        long created = 0;
        
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char key[64], value[192];
            if (sscanf(line, "%63[^=]=%191s", key, value) == 2) {
                if (strcmp(key, "ANV_ENV") == 0) strcpy(name, value);
                else if (strcmp(key, "TYPE") == 0) type = atoi(value);
                else if (strcmp(key, "SECURITY_LEVEL") == 0) security = atoi(value);
                else if (strcmp(key, "CREATED") == 0) created = atol(value);
            }
        }
        fclose(f);
        
        // Vérifier si en cours
        char pid_path[ANV_PATH_MAX];
        snprintf(pid_path, sizeof(pid_path), "%s/%s/pid", ctx->base_path, entry->d_name);
        int running = (access(pid_path, F_OK) == 0);
        
        // Formater la date
        char date_str[32] = "";
        if (created > 0) {
            struct tm *tm = localtime(&created);
            strftime(date_str, sizeof(date_str), "%Y-%m-%d", tm);
        }
        
        const char *type_str = type == 0 ? "APKM" : 
                              type == 1 ? "BOOL" :
                              type == 2 ? "APSM" : "CUSTOM";
        
        printf(" %-20s %-10s %-12s %-10d %s\n",
               name,
               running ? "\033[32mRUNNING\033[0m" : "\033[33mSTOPPED\033[0m",
               type_str,
               security,
               date_str);
    }
    
    closedir(dir);
    printf("════════════════════════════════════════════════════\n\n");
    
    return ANV_OK;
}

// ============================================================================
// MAIN - CLI
// ============================================================================

void print_usage(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════╗\n");
    printf("║  ANV v%s - Advanced Namespace Virtualization      ║\n", ANV_VERSION);
    printf("║  [track backsh >_< secure environment]            ║\n");
    printf("╚════════════════════════════════════════════════════╝\n\n");
    
    printf("USAGE:\n");
    printf("  anv <commande> [arguments]\n\n");
    
    printf("COMMANDES:\n");
    printf("  create <name> [type] [security]  Créer un environnement\n");
    printf("  start <name>                      Démarrer l'environnement\n");
    printf("  enter <name>                       Entrer dans l'environnement\n");
    printf("  stop <name>                        Arrêter l'environnement\n");
    printf("  delete <name>                       Supprimer l'environnement\n");
    printf("  list                                Lister les environnements\n");
    printf("  help                                Afficher cette aide\n\n");
    
    printf("TYPES:\n");
    printf("  0 - APKM (Package Manager)\n");
    printf("  1 - BOOL (Package Builder)\n");
    printf("  2 - APSM (Package Publisher)\n");
    printf("  3 - CUSTOM\n\n");
    
    printf("NIVEAUX DE SÉCURITÉ:\n");
    printf("  0 - None     (Aucune isolation)\n");
    printf("  1 - Low      (Isolation basique)\n");
    printf("  2 - Medium   (Isolation standard)\n");
    printf("  3 - High     (Isolation renforcée) ← Recommandé\n");
    printf("  4 - Paranoid (Isolation maximale)\n\n");
    
    printf("EXEMPLES:\n");
    printf("  anv create apkm-dev 0 3\n");
    printf("  anv start apkm-dev\n");
    printf("  anv enter apkm-dev   # Prompt devient [::apkm-dev::]user@host:~$\n");
    printf("  anv stop apkm-dev\n\n");
}

int main(int argc, char *argv[]) {
    // Vérification initiale de root
    if (geteuid() == 0) {
        printf("\033[1;31m[track backsh >_< root no secure]\033[0m\n");
        printf("ANV ne doit pas être exécuté en root pour des raisons de sécurité.\n");
        return 1;
    }
    
    if (argc < 2) {
        print_usage();
        return 0;
    }
    
    anv_ctx_t ctx;
    anv_init(&ctx);
    
    if (strcmp(argv[1], "create") == 0 && argc >= 3) {
        int type = (argc >= 4) ? atoi(argv[3]) : ANV_TYPE_APKM;
        int security = (argc >= 5) ? atoi(argv[4]) : ANV_SEC_HIGH;
        return anv_create(&ctx, argv[2], type, security);
    }
    else if (strcmp(argv[1], "start") == 0 && argc >= 3) {
        return anv_start(&ctx, argv[2]);
    }
    else if (strcmp(argv[1], "enter") == 0 && argc >= 3) {
        return anv_enter(&ctx, argv[2], NULL);
    }
    else if (strcmp(argv[1], "stop") == 0 && argc >= 3) {
        return anv_stop(&ctx, argv[2]);
    }
    else if (strcmp(argv[1], "delete") == 0 && argc >= 3) {
        return anv_delete(&ctx, argv[2]);
    }
    else if (strcmp(argv[1], "list") == 0) {
        return anv_list(&ctx);
    }
    else {
        print_usage();
    }
    
    return 0;
}
