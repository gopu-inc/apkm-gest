#include "apkm.h"
#include <pthread.h>
#include <semaphore.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/prctl.h>
#include <cap-ng.h>
#include <sys/xattr.h>
#include <sys/fanotify.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <lz4.h>
#include <zstd.h>
#include <blake3.h>
#include <toml.h>
#include <yaml.h>
#include <jansson.h>
#include <sqlite3.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>

// Structure de contexte global
typedef struct {
    sqlite3* db;
    pthread_mutex_t db_mutex;
    pthread_rwlock_t cache_lock;
    sem_t* worker_sem;
    int epoll_fd;
    int signal_fd;
    int timer_fd;
    int inotify_fd;
    int fanotify_fd;
    security_level_t security;
    progress_callback_t progress_cb;
    error_callback_t error_cb;
    log_callback_t log_cb;
    int thread_count;
    bool initialized;
    bool tracing_enabled;
    char* config_path;
    EVP_PKEY* signing_key;
    X509* cert;
} apkm_context_t;

static apkm_context_t ctx = {0};

// Initialisation avancée
int apkm_init(security_level_t security, progress_callback_t progress_cb, error_callback_t error_cb) {
    if (ctx.initialized) return -1;
    
    memset(&ctx, 0, sizeof(ctx));
    ctx.security = security;
    ctx.progress_cb = progress_cb;
    ctx.error_cb = error_cb;
    ctx.thread_count = sysconf(_SC_NPROCESSORS_ONLN);
    
    // Initialisation des mutex
    pthread_mutex_init(&ctx.db_mutex, NULL);
    pthread_rwlock_init(&ctx.cache_lock, NULL);
    sem_init(&ctx.worker_sem, 0, ctx.thread_count * 2);
    
    // Initialisation epoll pour I/O multiplexing
    ctx.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (ctx.epoll_fd < 0) return -1;
    
    // Initialisation signal fd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    ctx.signal_fd = signalfd(-1, &mask, SFD_CLOEXEC);
    
    // Timer pour les opérations asynchrones
    ctx.timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    
    // Inotify pour surveiller les changements
    ctx.inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    inotify_add_watch(ctx.inotify_fd, APKM_DB_PATH, IN_CREATE | IN_DELETE | IN_MODIFY);
    
    // Fanotify pour la surveillance des accès (high security)
    if (security >= SECURITY_HIGH) {
        ctx.fanotify_fd = fanotify_init(FAN_CLOEXEC | FAN_CLASS_CONTENT, O_RDONLY);
        fanotify_mark(ctx.fanotify_fd, FAN_MARK_ADD | FAN_MARK_MOUNT,
                      FAN_ACCESS | FAN_MODIFY | FAN_OPEN, AT_FDCWD, "/");
    }
    
    // Initialisation de la base de données
    sqlite3_open_v2(APKM_DB_PATH "/packages.db", &ctx.db,
                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                    SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_SHAREDCACHE, NULL);
    
    // Optimisation SQLite
    sqlite3_exec(ctx.db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(ctx.db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(ctx.db, "PRAGMA cache_size=-64000;", NULL, NULL, NULL);
    sqlite3_exec(ctx.db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
    
    // Création des tables optimisées
    const char* schema = 
        "CREATE TABLE IF NOT EXISTS packages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT UNIQUE NOT NULL,"
        "version TEXT NOT NULL,"
        "architecture TEXT,"
        "maintainer TEXT,"
        "description TEXT,"
        "license TEXT,"
        "checksum TEXT,"
        "signature TEXT,"
        "size INTEGER,"
        "installed_size INTEGER,"
        "build_date INTEGER,"
        "install_date INTEGER DEFAULT (strftime('%s','now')),"
        "state INTEGER DEFAULT 1,"
        "metadata BLOB,"
        "FOREIGN KEY (state) REFERENCES states(id),"
        "INDEX idx_name (name),"
        "INDEX idx_state (state)"
        ");"
        
        "CREATE TABLE IF NOT EXISTS dependencies ("
        "package_id INTEGER,"
        "dep_name TEXT,"
        "dep_type INTEGER,"
        "version_constraint TEXT,"
        "FOREIGN KEY (package_id) REFERENCES packages(id) ON DELETE CASCADE,"
        "INDEX idx_deps (package_id)"
        ");"
        
        "CREATE TABLE IF NOT EXISTS refs ("
        "id TEXT PRIMARY KEY,"
        "timestamp INTEGER,"
        "description TEXT,"
        "checksum TEXT,"
        "snapshot BLOB"
        ");"
        
        "CREATE TABLE IF NOT EXISTS metrics ("
        "key TEXT PRIMARY KEY,"
        "value BLOB,"
        "updated INTEGER"
        ");"
        
        "CREATE VIRTUAL TABLE IF NOT EXISTS packages_fts USING fts5("
        "name, description, content=packages"
        ");";
    
    sqlite3_exec(ctx.db, schema, NULL, NULL, NULL);
    
    // Initialisation des threads de travail
    for (int i = 0; i < ctx.thread_count; i++) {
        pthread_t thread;
        pthread_create(&thread, NULL, worker_thread, NULL);
        pthread_detach(thread);
    }
    
    ctx.initialized = true;
    
    // Chargement de la clé de signature si disponible
    load_signing_key();
    
    return 0;
}

// Thread worker ultra-optimisé
void* worker_thread(void* arg) {
    struct epoll_event events[32];
    char thread_name[16];
    
    prctl(PR_SET_NAME, (unsigned long)"apkm-worker", 0, 0, 0);
    
    while (ctx.initialized) {
        int nfds = epoll_wait(ctx.epoll_fd, events, 32, 1000);
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].events & EPOLLIN) {
                work_item_t* work = (work_item_t*)events[i].data.ptr;
                
                // Exécution de la tâche
                sem_wait(&ctx.worker_sem);
                work->function(work->data);
                sem_post(&ctx.worker_sem);
                
                // Notification de complétion
                if (work->completion_fd > 0) {
                    uint64_t val = 1;
                    write(work->completion_fd, &val, sizeof(val));
                }
            }
        }
    }
    return NULL;
}

// Installation avec résolution de dépendances parallèle
int apkm_install(const char* package, bool force, bool no_deps) {
    if (!ctx.initialized) return -1;
    
    if (ctx.progress_cb) ctx.progress_cb("install_start", 0);
    
    // Chargement du paquet
    package_t* pkg = fetch_package_info(package);
    if (!pkg) {
        if (ctx.error_cb) ctx.error_cb("Package not found", 404);
        return -1;
    }
    
    // Vérification des signatures (sécurité haute)
    if (ctx.security >= SECURITY_HIGH) {
        if (!verify_package_signature(pkg)) {
            if (ctx.error_cb) ctx.error_cb("Invalid signature", 403);
            free_package(pkg);
            return -1;
        }
    }
    
    // Résolution des dépendances en parallèle
    if (!no_deps && pkg->dep_count > 0) {
        resolve_dependencies_parallel(pkg);
    }
    
    // Vérification des conflits
    if (!force && check_conflicts(pkg) > 0) {
        if (ctx.error_cb) ctx.error_cb("Package conflicts detected", 409);
        free_package(pkg);
        return -1;
    }
    
    // Sandbox pour l'installation
    char sandbox_path[256];
    snprintf(sandbox_path, sizeof(sandbox_path), "%s/%s", APKM_SANDBOX_PATH, package);
    
    if (apkm_sandbox_create(sandbox_path, false, true) != 0) {
        if (ctx.error_cb) ctx.error_cb("Failed to create sandbox", 500);
        free_package(pkg);
        return -1;
    }
    
    // Téléchargement et extraction avec décompression accélérée
    if (download_package_parallel(pkg, sandbox_path) != 0) {
        apkm_sandbox_destroy(sandbox_path);
        free_package(pkg);
        return -1;
    }
    
    // Extraction avec décompression optimisée
    extract_package_fast(pkg, sandbox_path);
    
    // Installation des fichiers avec xattr pour la traçabilité
    install_files_with_metadata(pkg, sandbox_path);
    
    // Mise à jour de la base de données
    pthread_mutex_lock(&ctx.db_mutex);
    update_database(pkg);
    pthread_mutex_unlock(&ctx.db_mutex);
    
    // Nettoyage
    apkm_sandbox_destroy(sandbox_path);
    
    // Création automatique d'une ref (snapshot)
    if (ctx.security >= SECURITY_MEDIUM) {
        apkm_ref_create("Post-install snapshot");
    }
    
    if (ctx.progress_cb) ctx.progress_cb("install_complete", 100);
    
    free_package(pkg);
    return 0;
}

// Résolution de dépendances parallélisée
void resolve_dependencies_parallel(package_t* pkg) {
    if (pkg->dep_count == 0) return;
    
    int* results = calloc(pkg->dep_count, sizeof(int));
    pthread_t* threads = malloc(pkg->dep_count * sizeof(pthread_t));
    dep_resolve_arg_t* args = malloc(pkg->dep_count * sizeof(dep_resolve_arg_t));
    
    for (int i = 0; i < pkg->dep_count; i++) {
        args[i].dep_name = pkg->dependencies[i];
        args[i].result = &results[i];
        pthread_create(&threads[i], NULL, resolve_single_dependency, &args[i]);
    }
    
    for (int i = 0; i < pkg->dep_count; i++) {
        pthread_join(threads[i], NULL);
        if (results[i] != 0) {
            // Dépendance manquante, installation automatique
            if (ctx.security <= SECURITY_MEDIUM) {
                apkm_install(pkg->dependencies[i], false, false);
            }
        }
    }
    
    free(results);
    free(threads);
    free(args);
}

// Audit de sécurité avancé
int apkm_audit(security_level_t level) {
    if (!ctx.initialized) return -1;
    
    if (ctx.progress_cb) ctx.progress_cb("audit_start", 0);
    
    int vulnerabilities = 0;
    char** packages = apkm_list_installed(false);
    int pkg_count = 0;
    while (packages[pkg_count]) pkg_count++;
    
    // Scan parallélisé des vulnérabilités
    #pragma omp parallel for reduction(+:vulnerabilities)
    for (int i = 0; i < pkg_count; i++) {
        package_t* pkg = apkm_info(packages[i], false);
        if (pkg) {
            if (check_vulnerabilities(pkg, level) > 0) {
                vulnerabilities++;
                log_vulnerability(pkg);
            }
            free_package(pkg);
        }
        
        if (ctx.progress_cb) {
            ctx.progress_cb("audit_scan", (i * 100) / pkg_count);
        }
    }
    
    // Vérification d'intégrité avec BLAKE3
    if (level >= SECURITY_HIGH) {
        verify_all_checksums();
    }
    
    // Scan des rootkits si niveau paranoïaque
    if (level == SECURITY_PARANOID) {
        scan_rootkits();
        check_file_integrity();
        analyze_processes();
    }
    
    // Mise à jour des métriques
    update_security_metrics(vulnerabilities);
    
    if (ctx.progress_cb) ctx.progress_cb("audit_complete", 100);
    
    return vulnerabilities;
}

// Compression ZSTD optimisée pour les paquets
int compress_package(const char* source, const char* dest, int level) {
    FILE* in = fopen(source, "rb");
    FILE* out = fopen(dest, "wb");
    if (!in || !out) return -1;
    
    // Lecture du fichier
    fseek(in, 0, SEEK_END);
    size_t in_size = ftell(in);
    fseek(in, 0, SEEK_SET);
    
    char* in_buf = malloc(in_size);
    fread(in_buf, 1, in_size, in);
    
    // Compression ZSTD
    size_t out_size = ZSTD_compressBound(in_size);
    char* out_buf = malloc(out_size);
    
    out_size = ZSTD_compress(out_buf, out_size, in_buf, in_size, level);
    
    // Écriture du résultat
    fwrite(out_buf, 1, out_size, out);
    
    free(in_buf);
    free(out_buf);
    fclose(in);
    fclose(out);
    
    return 0;
}
