#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <libgen.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define BOOL_VERSION "2.1.0"
#define MANIFEST_NAME "Manifest.toml"
#define APKMBUILD_NAME "APKMBUILD"

// ============================================================================
// STRUCTURES
// ============================================================================

typedef struct {
    char name[256];
    char version[64];
    char release[16];
    char arch[32];
    char maintainer[256];
    char description[1024];
    char license[64];
    char url[256];
    char deps[2048];
    char build_deps[1024];
    char build_cmd[1024];
    char install_cmd[1024];
    char check_cmd[1024];
    char script_path[512];
    char readme_path[512];
    char sha256[128];
    char signature[256];
    char build_date[32];
    char build_host[128];
    long long file_size;
    int dep_count;
} build_info_t;

// Structure pour les fichiers à inclure
typedef struct {
    char source[512];
    char dest[512];
    mode_t mode;
} file_entry_t;

#define MAX_FILES 1024
static file_entry_t files[MAX_FILES];
static int file_count = 0;

static int debug_mode = 0;
static int quiet_mode = 0;

// ============================================================================
// FONCTIONS UTILITAIRES
// ============================================================================

void debug_print(const char *format, ...) {
    if (!debug_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[90m[DEBUG] ");
    vprintf(format, args);
    printf("\033[0m\n");
    va_end(args);
    fflush(stdout);
}

void print_info(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[36mℹ️  \033[0m");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_success(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[32m✅ \033[0m");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "\033[31m❌ \033[0m");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void print_warning(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[33m⚠️  \033[0m");
    vprintf(format, args);
    printf("\n");
    va_end(args);
}

void print_step(const char *format, ...) {
    if (quiet_mode) return;
    
    va_list args;
    va_start(args, format);
    printf("\033[35m▶ \033[0m");
    vprintf(format, args);
    printf("...\n");
    va_end(args);
}

// Nettoyer une chaîne (enlever guillemets et espaces)
void clean_string(char *str) {
    if (!str) return;
    
    int len = strlen(str);
    if (len >= 2 && str[0] == '"' && str[len-1] == '"') {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
    
    char *start = str;
    while (*start == ' ' || *start == '\t') start++;
    if (start != str) memmove(str, start, strlen(start) + 1);
    
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
}

// ============================================================================
// CALCUL SHA256
// ============================================================================

int calculate_file_sha256(const char *filepath, char *output) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        debug_print("Cannot open file for SHA256: %s", filepath);
        return -1;
    }
    
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    
    unsigned char buffer[8192];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        SHA256_Update(&ctx, buffer, bytes);
    }
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[SHA256_DIGEST_LENGTH * 2] = '\0';
    
    fclose(f);
    return 0;
}

// ============================================================================
// GÉNÉRATION DE SIGNATURE
// ============================================================================

void generate_signature(build_info_t *info, unsigned char *signature) {
    // Signature simple basée sur les métadonnées
    char buffer[4096];
    snprintf(buffer, sizeof(buffer),
             "%s:%s:%s:%s:%s:%s:%ld",
             info->name, info->version, info->release, info->arch,
             info->maintainer, info->build_date, time(NULL));
    
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, buffer, strlen(buffer));
    SHA256_Final(signature, &ctx);
    
    // Ajouter un sel fixe pour rendre unique
    const unsigned char salt[] = {0x00, 0x03, 0x78, 0x32, 0x30, 0x32, 0x32};
    SHA256_Update(&ctx, salt, sizeof(salt));
    SHA256_Final(signature, &ctx);
}

// ============================================================================
// PARSEUR APKMBUILD
// ============================================================================

int parse_apkmbuild(const char *filename, build_info_t *info) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        print_error("Cannot open %s", filename);
        return -1;
    }

    char line[1024];
    int in_block = 0;
    char current_block[64] = "";
    
    // Initialisation
    memset(info, 0, sizeof(build_info_t));
    strcpy(info->arch, "x86_64");
    strcpy(info->release, "r0");
    strcpy(info->license, "MIT");
    
    // Date de build
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(info->build_date, sizeof(info->build_date), "%Y-%m-%d %H:%M:%S", tm);
    
    // Hostname
    gethostname(info->build_host, sizeof(info->build_host));
    
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        
        // Gestion des blocs multi-lignes
        if (strstr(line, "$APKMMAKE::")) {
            in_block = 1;
            strcpy(current_block, "make");
            char *val = strstr(line, "::") + 2;
            strcpy(info->build_cmd, val);
            clean_string(info->build_cmd);
            continue;
        }
        if (strstr(line, "$APKMINSTALL::")) {
            in_block = 1;
            strcpy(current_block, "install");
            char *val = strstr(line, "::") + 2;
            strcpy(info->install_cmd, val);
            clean_string(info->install_cmd);
            continue;
        }
        if (strstr(line, "$APKMCHECK::")) {
            in_block = 1;
            strcpy(current_block, "check");
            char *val = strstr(line, "::") + 2;
            strcpy(info->check_cmd, val);
            clean_string(info->check_cmd);
            continue;
        }
        
        if (in_block) {
            if (strstr(line, "}")) {
                in_block = 0;
            } else {
                if (strcmp(current_block, "make") == 0) {
                    strcat(info->build_cmd, " ");
                    strcat(info->build_cmd, line);
                } else if (strcmp(current_block, "install") == 0) {
                    strcat(info->install_cmd, " ");
                    strcat(info->install_cmd, line);
                } else if (strcmp(current_block, "check") == 0) {
                    strcat(info->check_cmd, " ");
                    strcat(info->check_cmd, line);
                }
            }
            continue;
        }
        
        // Variables simples
        char *val;
        if ((val = strstr(line, "$APKNAME::"))) {
            strcpy(info->name, val + 10);
            clean_string(info->name);
        }
        else if ((val = strstr(line, "$APKMVERSION::"))) {
            strcpy(info->version, val + 14);
            clean_string(info->version);
        }
        else if ((val = strstr(line, "$APKMRELEASE::"))) {
            strcpy(info->release, val + 14);
            clean_string(info->release);
        }
        else if ((val = strstr(line, "$APKMARCH::"))) {
            strcpy(info->arch, val + 11);
            clean_string(info->arch);
        }
        else if ((val = strstr(line, "$APKMMAINT::"))) {
            strcpy(info->maintainer, val + 12);
            clean_string(info->maintainer);
        }
        else if ((val = strstr(line, "$APKMDESC::"))) {
            strcpy(info->description, val + 11);
            clean_string(info->description);
        }
        else if ((val = strstr(line, "$APKMLICENSE::"))) {
            strcpy(info->license, val + 14);
            clean_string(info->license);
        }
        else if ((val = strstr(line, "$APKMURL::"))) {
            strcpy(info->url, val + 10);
            clean_string(info->url);
        }
        else if ((val = strstr(line, "$APKMDEP::"))) {
            strcpy(info->deps, val + 10);
            clean_string(info->deps);
        }
        else if ((val = strstr(line, "$APKMBUILDDEP::"))) {
            strcpy(info->build_deps, val + 15);
            clean_string(info->build_deps);
        }
        else if ((val = strstr(line, "$APKMPATH::"))) {
            strcpy(info->script_path, val + 11);
            clean_string(info->script_path);
        }
        else if ((val = strstr(line, "$APKMREADME::"))) {
            strcpy(info->readme_path, val + 13);
            clean_string(info->readme_path);
        }
        else if ((val = strstr(line, "$APKMINSTALL::"))) {
            // Pour les fichiers à installer
            char *file = val + 14;
            clean_string(file);
            if (strlen(file) > 0 && file_count < MAX_FILES) {
                strcpy(files[file_count].source, file);
                strcpy(files[file_count].dest, file);
                files[file_count].mode = 0644;
                file_count++;
            }
        }
    }
    
    fclose(fp);
    
    if (strlen(info->name) == 0) {
        print_error("Missing $APKNAME in APKMBUILD");
        return -1;
    }
    
    return 0;
}

// ============================================================================
// GÉNÉRATION DE MANIFEST.TOML
// ============================================================================

int generate_manifest(build_info_t *info, const char *output_dir) {
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s", output_dir, MANIFEST_NAME);
    
    FILE *f = fopen(manifest_path, "w");
    if (!f) {
        print_error("Cannot create manifest");
        return -1;
    }
    
    fprintf(f, "# Generated by BOOL v%s\n", BOOL_VERSION);
    fprintf(f, "# Build date: %s\n", info->build_date);
    fprintf(f, "# Build host: %s\n", info->build_host);
    fprintf(f, "\n");
    
    fprintf(f, "[metadata]\n");
    fprintf(f, "name = \"%s\"\n", info->name);
    fprintf(f, "version = \"%s\"\n", info->version);
    fprintf(f, "release = \"%s\"\n", info->release);
    fprintf(f, "arch = \"%s\"\n", info->arch);
    fprintf(f, "description = \"%s\"\n", info->description);
    fprintf(f, "maintainer = \"%s\"\n", info->maintainer);
    fprintf(f, "license = \"%s\"\n", info->license);
    
    if (strlen(info->url) > 0) {
        fprintf(f, "homepage = \"%s\"\n", info->url);
    }
    
    fprintf(f, "\n");
    
    // Badges
    fprintf(f, "[metadata.badges]\n");
    fprintf(f, "version = { label = \"version\", color = \"blue\" }\n");
    fprintf(f, "license = { label = \"license\", color = \"yellow\" }\n");
    fprintf(f, "\n");
    
    // Dépendances
    if (strlen(info->deps) > 0) {
        fprintf(f, "[dependencies]\n");
        char deps_copy[2048];
        strcpy(deps_copy, info->deps);
        
        char *dep = strtok(deps_copy, ";");
        while (dep) {
            char *eq = strchr(dep, '=');
            if (eq) {
                *eq = ' ';
                fprintf(f, "%s = \"%s\"\n", dep, eq + 1);
            } else {
                fprintf(f, "%s = \"*\"\n", dep);
            }
            dep = strtok(NULL, ";");
        }
        fprintf(f, "\n");
    }
    
    // Dépendances de build
    if (strlen(info->build_deps) > 0) {
        fprintf(f, "[build-dependencies]\n");
        char deps_copy[1024];
        strcpy(deps_copy, info->build_deps);
        
        char *dep = strtok(deps_copy, ";");
        while (dep) {
            char *eq = strchr(dep, '=');
            if (eq) {
                *eq = ' ';
                fprintf(f, "%s = \"%s\"\n", dep, eq + 1);
            } else {
                fprintf(f, "%s = \"*\"\n", dep);
            }
            dep = strtok(NULL, ";");
        }
        fprintf(f, "\n");
    }
    
    // Fichiers
    if (file_count > 0) {
        fprintf(f, "[files]\n");
        for (int i = 0; i < file_count; i++) {
            fprintf(f, "[[file]]\n");
            fprintf(f, "source = \"%s\"\n", files[i].source);
            fprintf(f, "dest = \"%s\"\n", files[i].dest);
            fprintf(f, "mode = \"%o\"\n", files[i].mode);
        }
        fprintf(f, "\n");
    }
    
    // Scripts
    if (strlen(info->install_cmd) > 0 || access("install.sh", F_OK) == 0) {
        fprintf(f, "[scripts]\n");
        if (strlen(info->install_cmd) > 0) {
            fprintf(f, "install = \"%s\"\n", info->install_cmd);
        }
        if (access("install.sh", F_OK) == 0) {
            fprintf(f, "postinst = \"install.sh\"\n");
        }
        fprintf(f, "\n");
    }
    
    // Signature
    fprintf(f, "[signature]\n");
    fprintf(f, "sha256 = \"%s\"\n", info->sha256);
    fprintf(f, "timestamp = \"%s\"\n", info->build_date);
    
    fclose(f);
    print_success("Generated %s", MANIFEST_NAME);
    return 0;
}

// ============================================================================
// CRÉATION DE LA STRUCTURE DU PAQUET
// ============================================================================

int create_package_structure(build_info_t *info, const char *build_dir) {
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/pkg-%s", build_dir, info->name);
    
    // Créer le répertoire principal
    mkdir(pkg_dir, 0755);
    
    // Créer la structure standard
    char path[1024];
    snprintf(path, sizeof(path), "%s/usr", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/bin", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/lib", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/include", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/share", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/share/doc", pkg_dir);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/usr/share/doc/%s", pkg_dir, info->name);
    mkdir(path, 0755);
    
    print_info("Created package structure in %s", pkg_dir);
    return 0;
}

// ============================================================================
// COPIE DES FICHIERS
// ============================================================================

int copy_files(build_info_t *info, const char *build_dir) {
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/pkg-%s", build_dir, info->name);
    
    // Copier les fichiers spécifiés
    for (int i = 0; i < file_count; i++) {
        char dest_path[1024];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", pkg_dir, files[i].dest);
        
        // Créer les répertoires parents si nécessaire
        char *last_slash = strrchr(dest_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            mkdir(dest_path, 0755);
            *last_slash = '/';
        }
        
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", files[i].source, dest_path);
        system(cmd);
        
        if (files[i].mode != 0) {
            chmod(dest_path, files[i].mode);
        }
        
        debug_print("Copied %s -> %s", files[i].source, dest_path);
    }
    
    // Copier automatiquement les binaires courants
    if (access(info->name, F_OK) == 0) {
        char dest[1024];
        snprintf(dest, sizeof(dest), "%s/usr/bin/%s", pkg_dir, info->name);
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "cp '%s' '%s' && chmod 755 '%s'", info->name, dest, dest);
        system(cmd);
        debug_print("Copied binary %s", info->name);
    }
    
    // Copier la documentation
    if (strlen(info->readme_path) > 0 && access(info->readme_path, F_OK) == 0) {
        char dest[1024];
        snprintf(dest, sizeof(dest), "%s/usr/share/doc/%s/README.md", pkg_dir, info->name);
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", info->readme_path, dest);
        system(cmd);
    }
    
    // Copier install.sh s'il existe
    if (access("install.sh", F_OK) == 0) {
        char dest[1024];
        snprintf(dest, sizeof(dest), "%s/install.sh", pkg_dir);
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "cp install.sh '%s' && chmod 755 '%s'", dest, dest);
        system(cmd);
    }
    
    return 0;
}

// ============================================================================
// EXÉCUTION DES COMMANDES DE BUILD
// ============================================================================

int run_build_commands(build_info_t *info) {
    if (strlen(info->build_cmd) == 0) return 0;
    
    print_step("Running build commands");
    debug_print("Executing: %s", info->build_cmd);
    
    int ret = system(info->build_cmd);
    if (ret != 0) {
        print_warning("Build command exited with code %d", ret);
    }
    
    return 0;
}

int run_check_commands(build_info_t *info) {
    if (strlen(info->check_cmd) == 0) return 0;
    
    print_step("Running tests");
    debug_print("Executing: %s", info->check_cmd);
    
    int ret = system(info->check_cmd);
    if (ret != 0) {
        print_warning("Tests failed with code %d", ret);
    }
    
    return 0;
}

// ============================================================================
// CRÉATION DE L'ARCHIVE FINALE
// ============================================================================

int create_archive(build_info_t *info, const char *build_dir) {
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/pkg-%s", build_dir, info->name);
    
    // S'assurer que le répertoire build existe
    mkdir("build", 0755);
    
    char archive_name[512];
    snprintf(archive_name, sizeof(archive_name), 
             "build/%s-v%s-%s.%s.tar.bool", 
             info->name, info->version, info->release, info->arch);
    
    print_step("Creating archive");
    
    // Créer l'archive avec tar
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), 
             "cd '%s' && tar -cf '%s' * 2>/dev/null", 
             pkg_dir, archive_name);
    
    debug_print("Running: %s", cmd);
    
    int tar_result = system(cmd);
    
    if (tar_result == 0 && access(archive_name, F_OK) == 0) {
        struct stat st;
        stat(archive_name, &st);
        info->file_size = st.st_size;
        
        // Calculer SHA256
        if (calculate_file_sha256(archive_name, info->sha256) == 0) {
            print_success("Archive created: %s (%.2f KB)", 
                         archive_name, st.st_size / 1024.0);
            print_info("SHA256: %s", info->sha256);
        }
        
        return 0;
    }
    
    print_error("Failed to create archive");
    return -1;
}

// ============================================================================
// NETTOYAGE
// ============================================================================

int cleanup(build_info_t *info, const char *build_dir) {
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/pkg-%s", build_dir, info->name);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", pkg_dir);
    system(cmd);
    
    debug_print("Cleaned up %s", pkg_dir);
    return 0;
}

// ============================================================================
// BUILD PRINCIPAL
// ============================================================================

int build_package(build_info_t *info) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║     BOOL v%s - Package Builder                  ║\n", BOOL_VERSION);
    printf("║     [tar.bool] with Manifest.toml               ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
    
    // Afficher les infos
    printf("📦 Package Information:\n");
    printf("  Name:         %s\n", info->name);
    printf("  Version:      %s-%s\n", info->version, info->release);
    printf("  Architecture: %s\n", info->arch);
    printf("  Build date:   %s\n", info->build_date);
    
    if (strlen(info->description) > 0) {
        printf("  Description:  %s\n", info->description);
    }
    printf("\n");
    
    // Build
    if (strlen(info->build_cmd) > 0) {
        run_build_commands(info);
    }
    
    // Tests
    if (strlen(info->check_cmd) > 0) {
        run_check_commands(info);
    }
    
    // Créer la structure
    create_package_structure(info, ".");
    
    // Copier les fichiers
    copy_files(info, ".");
    
    // Générer le manifest
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "pkg-%s", info->name);
    generate_manifest(info, pkg_dir);
    
    // Créer l'archive
    if (create_archive(info, ".") != 0) {
        cleanup(info, ".");
        return -1;
    }
    
    // Nettoyer
    cleanup(info, ".");
    
    printf("\n");
    print_success("Build completed successfully!");
    
    char archive_name[512];
    snprintf(archive_name, sizeof(archive_name), 
             "build/%s-v%s-%s.%s.tar.bool", 
             info->name, info->version, info->release, info->arch);
    
    printf("📦 Output: %s\n", archive_name);
    printf("📄 Manifest: included in archive as %s\n", MANIFEST_NAME);
    printf("🔏 SHA256: %s\n", info->sha256);
    
    return 0;
}

// ============================================================================
// INFO SUR LE PAQUET
// ============================================================================

int show_package_info(const char *package_path) {
    struct stat st;
    if (stat(package_path, &st) != 0) {
        print_error("File not found: %s", package_path);
        return -1;
    }
    
    printf("\n");
    printf("📦 Package Information\n");
    printf("──────────────────────\n");
    printf("File:     %s\n", package_path);
    printf("Size:     %.2f KB\n", st.st_size / 1024.0);
    
    // Calculer SHA256
    char sha256[128];
    if (calculate_file_sha256(package_path, sha256) == 0) {
        printf("SHA256:   %s\n", sha256);
    }
    
    // Vérifier le type
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "file '%s' | grep -q 'tar archive'", package_path);
    if (system(cmd) == 0) {
        printf("Type:     tar archive\n");
        
        // Lister le contenu (juste les noms)
        printf("\nContents:\n");
        snprintf(cmd, sizeof(cmd), "tar -tf '%s' 2>/dev/null | head -10 | sed 's/^/  • /'", package_path);
        fflush(stdout);
        system(cmd);
        
        // Chercher le manifest
        printf("\nLooking for %s...\n", MANIFEST_NAME);
        snprintf(cmd, sizeof(cmd), 
                 "tar -tf '%s' 2>/dev/null | grep -i 'manifest.toml' || echo 'Not found'",
                 package_path);
        fflush(stdout);
        system(cmd);
        
    } else {
        printf("Type:     unknown\n");
    }
    
    return 0;
}

// ============================================================================
// VÉRIFICATION DU PAQUET
// ============================================================================

int verify_package(const char *package_path) {
    printf("\n");
    printf("🔐 Verifying %s\n", package_path);
    printf("──────────────────\n");
    
    // Vérifier l'existence
    if (access(package_path, F_OK) != 0) {
        print_error("File not found");
        return -1;
    }
    
    // Vérifier le SHA256 si le fichier .sha256 existe
    char sha_file[512];
    snprintf(sha_file, sizeof(sha_file), "%s.sha256", package_path);
    
    if (access(sha_file, F_OK) == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cd %s && sha256sum -c '%s'", 
                 dirname(strdup(sha_file)), basename(strdup(sha_file)));
        
        if (system(cmd) == 0) {
            print_success("SHA256 verification passed");
        } else {
            print_error("SHA256 verification failed");
        }
    } else {
        char sha256[128];
        if (calculate_file_sha256(package_path, sha256) == 0) {
            printf("SHA256: %s\n", sha256);
            print_warning("No signature file found");
        }
    }
    
    return 0;
}

// ============================================================================
// AIDE
// ============================================================================

void print_help(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║     BOOL v%s - Package Builder                  ║\n", BOOL_VERSION);
    printf("║     [tar.bool] with Manifest.toml               ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
    
    printf("USAGE:\n");
    printf("  bool <command> [arguments]\n\n");
    
    printf("COMMANDS:\n");
    printf("  --build                 Build package from APKMBUILD\n");
    printf("  --info <package>        Show package information\n");
    printf("  --verify <package>      Verify package integrity\n");
    printf("  --init                  Create template APKMBUILD and Manifest.toml\n");
    printf("  --help                  Show this help\n\n");
    
    printf("OPTIONS:\n");
    printf("  --debug                 Enable debug output\n");
    printf("  --quiet                 Suppress output\n\n");
    
    printf("EXAMPLES:\n");
    printf("  bool --build\n");
    printf("  bool --info build/package.tar.bool\n");
    printf("  bool --verify build/package.tar.bool\n");
    printf("  bool --init\n\n");
}

// ============================================================================
// INITIALISATION D'UN NOUVEAU PROJET
// ============================================================================

int init_project(void) {
    if (access("APKMBUILD", F_OK) == 0 || access(MANIFEST_NAME, F_OK) == 0) {
        print_warning("Project files already exist");
        printf("Overwrite? [y/N] ");
        char response = getchar();
        if (response != 'y' && response != 'Y') {
            print_info("Initialisation cancelled");
            return 0;
        }
    }
    
    // Créer APKMBUILD template
    FILE *f = fopen("APKMBUILD", "w");
    if (!f) {
        print_error("Cannot create APKMBUILD");
        return -1;
    }
    
    fprintf(f, "# APKMBUILD template\n");
    fprintf(f, "# Generated by BOOL v%s\n\n", BOOL_VERSION);
    fprintf(f, "$APKNAME::myapp\n");
    fprintf(f, "$APKMVERSION::1.0.0\n");
    fprintf(f, "$APKMRELEASE::r0\n");
    fprintf(f, "$APKMARCH::x86_64\n");
    fprintf(f, "$APKMMAINT::Your Name <email@example.com>\n");
    fprintf(f, "$APKMDESC::My awesome application\n");
    fprintf(f, "$APKMLICENSE::MIT\n");
    fprintf(f, "$APKMDEP:: libc >=2.30; gcc\n");
    fprintf(f, "$APKMBUILDDEP:: cmake\n");
    fprintf(f, "$APKMPATH::install.sh\n");
    fprintf(f, "$APKMREADME::README.md\n");
    fprintf(f, "\n");
    fprintf(f, "$APKMMAKE:: {\n");
    fprintf(f, "    mkdir -p build\n");
    fprintf(f, "    cd build\n");
    fprintf(f, "    cmake ..\n");
    fprintf(f, "    make\n");
    fprintf(f, "}\n");
    fprintf(f, "\n");
    fprintf(f, "$APKMINSTALL:: {\n");
    fprintf(f, "    make install DESTDIR=\"$DESTDIR\"\n");
    fprintf(f, "}\n");
    
    fclose(f);
    print_success("Created APKMBUILD");
    
    // Créer Manifest.toml template
    f = fopen(MANIFEST_NAME, "w");
    if (!f) {
        print_error("Cannot create Manifest.toml");
        return -1;
    }
    
    fprintf(f, "# Manifest.toml template\n");
    fprintf(f, "# Generated by BOOL v%s\n\n", BOOL_VERSION);
    fprintf(f, "[metadata]\n");
    fprintf(f, "name = \"myapp\"\n");
    fprintf(f, "version = \"1.0.0\"\n");
    fprintf(f, "release = \"r0\"\n");
    fprintf(f, "arch = \"x86_64\"\n");
    fprintf(f, "description = \"My awesome application\"\n");
    fprintf(f, "maintainer = \"Your Name <email@example.com>\"\n");
    fprintf(f, "license = \"MIT\"\n");
    fprintf(f, "\n");
    fprintf(f, "[metadata.badges]\n");
    fprintf(f, "version = { label = \"version\", color = \"blue\" }\n");
    fprintf(f, "license = { label = \"license\", color = \"yellow\" }\n");
    fprintf(f, "\n");
    fprintf(f, "[dependencies]\n");
    fprintf(f, "libc = \">=2.30\"\n");
    fprintf(f, "gcc = \"*\"\n");
    fprintf(f, "\n");
    fprintf(f, "[build-dependencies]\n");
    fprintf(f, "cmake = \">=3.20\"\n");
    
    fclose(f);
    print_success("Created Manifest.toml");
    
    // Créer README.md template
    f = fopen("README.md", "w");
    if (f) {
        fprintf(f, "# myapp\n\n");
        fprintf(f, "My awesome application built with BOOL\n");
        fclose(f);
        print_success("Created README.md");
    }
    
    // Créer install.sh template
    f = fopen("install.sh", "w");
    if (f) {
        fprintf(f, "#!/bin/sh\n");
        fprintf(f, "# Installation script\n");
        fprintf(f, "echo \"Installing myapp...\"\n");
        fprintf(f, "mkdir -p /usr/local/bin\n");
        fprintf(f, "cp myapp /usr/local/bin/\n");
        fprintf(f, "chmod 755 /usr/local/bin/myapp\n");
        fclose(f);
        chmod("install.sh", 0755);
        print_success("Created install.sh");
    }
    
    printf("\n");
    print_success("Project initialised!");
    printf("Run 'bool --build' to build the package\n");
    
    return 0;
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }
    
    // Parser les options globales
    int args_processed = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
            args_processed++;
        }
        else if (strcmp(argv[i], "--quiet") == 0) {
            quiet_mode = 1;
            args_processed++;
        }
        else {
            break;
        }
    }
    
    if (args_processed > 1) {
        argv += args_processed - 1;
        argc -= args_processed - 1;
    }
    
    if (strcmp(argv[1], "--build") == 0) {
        // Vérifier que APKMBUILD existe
        if (access(APKMBUILD_NAME, F_OK) != 0) {
            print_error("APKMBUILD not found");
            print_info("Run 'bool --init' to create a template");
            return 1;
        }
        
        build_info_t info;
        if (parse_apkmbuild(APKMBUILD_NAME, &info) != 0) {
            return 1;
        }
        
        return build_package(&info);
    }
    else if (strcmp(argv[1], "--info") == 0) {
        if (argc < 3) {
            print_error("Missing package file");
            return 1;
        }
        return show_package_info(argv[2]);
    }
    else if (strcmp(argv[1], "--verify") == 0) {
        if (argc < 3) {
            print_error("Missing package file");
            return 1;
        }
        return verify_package(argv[2]);
    }
    else if (strcmp(argv[1], "--init") == 0) {
        return init_project();
    }
    else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_help();
        return 0;
    }
    else {
        print_error("Unknown command: %s", argv[1]);
        print_help();
        return 1;
    }
    
    return 0;
}
