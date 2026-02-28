#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <time.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#define BOOL_VERSION "2.1.0"
#define SIGNATURE_SIZE 128

typedef struct {
    char name[128];
    char version[64];
    char release[16];
    char arch[32];
    char maintainer[256];
    char description[512];
    char license[64];
    char url[256];
    char deps[1024];
    char build_cmd[1024];
    char install_cmd[1024];
    char check_cmd[1024];
    char script_path[512];
    char includes[512];
    char libs[512];
    char pkgconfig[512];
    char sha256[128];
    char signature[SIGNATURE_SIZE];
    char build_date[32];
    char build_host[128];
// Dans la structure apkm_build_t, ajouter :
    char docs[2048];        // Documentation (peut contenir des chemins de fichiers)
    char readme_path[512];  // Chemin vers le README
    char doc_content[8192]; // Contenu de la documentation
    long long file_size;
    int dep_count;
    char** deps_array;
} apkm_build_t;

// Nettoyer une chaîne
void clean_string(char *str) {
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

// Calculer SHA256 d'un fichier
int calculate_file_sha256(const char *filepath, char *output) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        printf("[BOOL] Cannot open file for SHA256: %s\n", filepath);
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

// Parser le fichier APKMBUILD
void parse_apkmbuild(const char *filename, apkm_build_t *b) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("[BOOL] Error opening APKMBUILD");
        exit(1);
    }

    char line[1024];
    int in_block = 0;
    char current_block[1024] = "";
    
    // Initialisation
    memset(b, 0, sizeof(apkm_build_t));
    strcpy(b->arch, "x86_64");
    strcpy(b->release, "r0");
    strcpy(b->script_path, "install.sh");
    strcpy(b->includes, "include");
    strcpy(b->libs, "lib");
    strcpy(b->pkgconfig, "lib/pkgconfig");
    
    // Date de build
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(b->build_date, sizeof(b->build_date), "%Y-%m-%d %H:%M:%S", tm);
    
    // Hostname
    gethostname(b->build_host, sizeof(b->build_host));
    
    while (fgets(line, sizeof(line), fp)) {
        // Enlever les retours à la ligne
        line[strcspn(line, "\n")] = 0;
        
        if (strstr(line, "$APKMMAKE::")) {
            in_block = 1;
            strcpy(current_block, "make");
            char *val = strstr(line, "::") + 2;
            strcpy(b->build_cmd, val);
            clean_string(b->build_cmd);
            continue;
        }
        if (strstr(line, "$APKMINSTALL::")) {
            in_block = 1;
            strcpy(current_block, "install");
            char *val = strstr(line, "::") + 2;
            strcpy(b->install_cmd, val);
            clean_string(b->install_cmd);
            continue;
        }
        if (strstr(line, "$APKMCHECK::")) {
            in_block = 1;
            strcpy(current_block, "check");
            char *val = strstr(line, "::") + 2;
            strcpy(b->check_cmd, val);
            clean_string(b->check_cmd);
            continue;
        }
        
        if (in_block) {
            if (strstr(line, "}")) {
                in_block = 0;
            } else {
                if (strcmp(current_block, "make") == 0) {
                    strcat(b->build_cmd, " ");
                    strcat(b->build_cmd, line);
                } else if (strcmp(current_block, "install") == 0) {
                    strcat(b->install_cmd, " ");
                    strcat(b->install_cmd, line);
                } else if (strcmp(current_block, "check") == 0) {
                    strcat(b->check_cmd, " ");
                    strcat(b->check_cmd, line);
                }
            }
            continue;
        }
        
        char *val;
        if ((val = strstr(line, "$APKNAME::"))) {
            strcpy(b->name, val + 10);
            clean_string(b->name);
        }
        else if ((val = strstr(line, "$APKMVERSION::"))) {
            strcpy(b->version, val + 14);
            clean_string(b->version);
        }
        else if ((val = strstr(line, "$APKMRELEASE::"))) {
            strcpy(b->release, val + 14);
            clean_string(b->release);
        }
        else if ((val = strstr(line, "$APKMARCH::"))) {
            strcpy(b->arch, val + 11);
            clean_string(b->arch);
        }
        else if ((val = strstr(line, "$APKMMAINT::"))) {
            strcpy(b->maintainer, val + 12);
            clean_string(b->maintainer);
        }
        else if ((val = strstr(line, "$APKMDESC::"))) {
            strcpy(b->description, val + 11);
            clean_string(b->description);
        }
        else if ((val = strstr(line, "$APKMLICENSE::"))) {
            strcpy(b->license, val + 14);
            clean_string(b->license);
        }
        else if ((val = strstr(line, "$APKMURL::"))) {
            strcpy(b->url, val + 10);
            clean_string(b->url);
        }
        else if ((val = strstr(line, "$APKMDEP::"))) {
            strcpy(b->deps, val + 10);
            clean_string(b->deps);
        }
        else if ((val = strstr(line, "$APKMPATH::"))) {
            strcpy(b->script_path, val + 11);
            clean_string(b->script_path);
        }
        else if ((val = strstr(line, "$APKMINCLUDES::"))) {
            strcpy(b->includes, val + 15);
            clean_string(b->includes);
        }
            // Dans parse_apkmbuild, ajouter :
else if ((val = strstr(line, "$APKMDOC::"))) {
    strcpy(b->docs, val + 10);
    clean_string(b->docs);
    
    // Parser le format spécial [%OPEN+==fichier]
    char *open_marker = strstr(b->docs, "[%OPEN+==");
    if (open_marker) {
        char *file_start = open_marker + 9;
        char *file_end = strchr(file_start, ']');
        if (file_end) {
            int file_len = file_end - file_start;
            strncpy(b->readme_path, file_start, file_len);
            b->readme_path[file_len] = '\0';
        }
    }
}
        else if ((val = strstr(line, "$APKMLIBS::"))) {
            strcpy(b->libs, val + 11);
            clean_string(b->libs);
        }
        else if ((val = strstr(line, "$APKMPKGCONFIG::"))) {
            strcpy(b->pkgconfig, val + 16);
            clean_string(b->pkgconfig);
        }
    }
    fclose(fp);
}

// Dans bool.c, ajouter cette fonction
char* load_readme_content(const char *readme_path, char *buffer, size_t buffer_size) {
    FILE *f = fopen(readme_path, "r");
    if (!f) {
        // Essayer différents noms
        const char *alt_paths[] = {
            "README.md",
            "README",
            "readme.md",
            "Readme.md",
            "doc/README.md",
            "docs/README.md",
            NULL
        };
        
        for (int i = 0; alt_paths[i] != NULL; i++) {
            f = fopen(alt_paths[i], "r");
            if (f) break;
        }
    }
    
    if (!f) {
        snprintf(buffer, buffer_size, "No documentation found for %s", readme_path);
        return buffer;
    }
    
    size_t total = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) && total < buffer_size - 1) {
        size_t len = strlen(line);
        if (total + len < buffer_size - 1) {
            strcpy(buffer + total, line);
            total += len;
        }
    }
    fclose(f);
    
    return buffer;
}

// Créer la structure complète du paquet
int create_package_structure(apkm_build_t *b, const char *build_dir) {
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/pkg-%s", build_dir, b->name);
    
    // Créer le répertoire principal
    mkdir(pkg_dir, 0755);
    
    char path[1024];
    
    // Créer la structure FHS
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
    
    snprintf(path, sizeof(path), "%s/usr/lib/pkgconfig", pkg_dir);
    mkdir(path, 0755);
    
    printf("[BOOL] Copying project files...\n");
    
    // Copier tous les fichiers (sauf build/ et pkg-*/)
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), 
             "find . -maxdepth 1 -not -name 'build' -not -name 'pkg-*' -not -name '.' -exec cp -r {} %s/ \\;",
             pkg_dir);
    system(cmd);
    
    return 0;
}

// Créer un fichier de signature séparé
void create_signature_file(apkm_build_t *b, const char *pkg_dir) {
    char sig_path[512];
    snprintf(sig_path, sizeof(sig_path), "%s/.BOOL.sig", pkg_dir);
    
    FILE *sig = fopen(sig_path, "w");
    if (!sig) return;
    
    fprintf(sig, "# BOOL Signature File\n");
    fprintf(sig, "# Generated: %s\n", b->build_date);
    fprintf(sig, "# Host: %s\n", b->build_host);
    fprintf(sig, "\n");
    fprintf(sig, "NAME=%s\n", b->name);
    fprintf(sig, "VERSION=%s\n", b->version);
    fprintf(sig, "RELEASE=%s\n", b->release);
    fprintf(sig, "ARCH=%s\n", b->arch);
    fprintf(sig, "MAINTAINER=%s\n", b->maintainer);
    fprintf(sig, "DESCRIPTION=%s\n", b->description);
    fprintf(sig, "LICENSE=%s\n", b->license);
    fprintf(sig, "SHA256=%s\n", b->sha256);
    fprintf(sig, "SIGNATURE=%s\n", b->signature);
    fprintf(sig, "DEPENDENCIES=%s\n", b->deps);
    
    fclose(sig);
    printf("[BOOL] Signature file created: .BOOL.sig\n");
}

// Builder le paquet
int build_package(apkm_build_t *b) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  BOOL - APKM Package Builder v%s\n", BOOL_VERSION);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    printf("📦 PACKAGE INFORMATION:\n");
    printf("  • Name         : %s\n", b->name);
    printf("  • Version      : %s-%s\n", b->version, b->release);
    printf("  • Architecture : %s\n", b->arch);
    printf("  • Script path  : %s\n", b->script_path);
    printf("  • Build date   : %s\n", b->build_date);
    
    // Step 1: Build
    if (strlen(b->build_cmd) > 0) {
        printf("\n🔧 BUILD STEP:\n");
        printf("  • Executing: %s\n", b->build_cmd);
        if (system(b->build_cmd) != 0) {
            printf("[BOOL] Build non-blocking (continuing anyway)\n");
        }
    }
    
    // Step 2: Tests
    if (strlen(b->check_cmd) > 0) {
        printf("\n🧪 TEST STEP:\n");
        printf("  • Executing: %s\n", b->check_cmd);
        system(b->check_cmd);
    }
    
    // Step 3: Create package structure
    printf("\n📁 PACKAGE PREPARATION:\n");
    create_package_structure(b, ".");
    
    // Step 4: Installation
    if (strlen(b->install_cmd) > 0) {
        printf("\n⚙️ INSTALL STEP:\n");
        char destdir[512];
        snprintf(destdir, sizeof(destdir), "pkg-%s", b->name);
        setenv("DESTDIR", destdir, 1);
        printf("  • DESTDIR=%s\n", destdir);
        printf("  • Command: %s\n", b->install_cmd);
        system(b->install_cmd);
    }
    
    // Step 5: Create signature file
    strcpy(b->sha256, "pending");
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "pkg-%s", b->name);
    create_signature_file(b, pkg_dir);
    
    // Step 6: Create final archive
    printf("\n📦 CREATING FINAL ARCHIVE:\n");
    
    char archive_name[512];
    snprintf(archive_name, sizeof(archive_name), 
             "build/%s-v%s-%s.%s.tar.bool", 
             b->name, b->version, b->release, b->arch);
    
    // S'assurer que le répertoire build existe
    mkdir("build", 0755);
    
    // Commande tar simplifiée et corrigée
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), 
             "cd pkg-%s && tar -czf ../%s *", 
             b->name, archive_name);
    
    printf("[BOOL] Running: %s\n", cmd);
    fflush(stdout);
    
    int tar_result = system(cmd);
    
    if (tar_result == 0) {
        // Vérifier que l'archive a été créée
        if (access(archive_name, F_OK) == 0) {
            // Calculer SHA256
            if (calculate_file_sha256(archive_name, b->sha256) == 0) {
                // Obtenir la taille
                struct stat st;
                stat(archive_name, &st);
                b->file_size = st.st_size;
                
                printf("  ✅ Archive created: %s (%.2f KB)\n", 
                       archive_name, st.st_size / 1024.0);
                printf("  🔏 SHA256: %.32s...\n", b->sha256);
                
                // Créer le fichier SHA256
                char sha_file[512];
                snprintf(sha_file, sizeof(sha_file), "%s.sha256", archive_name);
                FILE *sf = fopen(sha_file, "w");
                if (sf) {
                    fprintf(sf, "%s  %s\n", b->sha256, archive_name);
                    fclose(sf);
                    printf("  📄 SHA256 file created: %s\n", sha_file);
                }
                
                // Créer le manifeste
                char manifest[512];
                snprintf(manifest, sizeof(manifest), "build/%s.manifest", b->name);
                FILE *mf = fopen(manifest, "w");
                if (mf) {
                    fprintf(mf, "NAME=%s\n", b->name);
                    fprintf(mf, "VERSION=%s\n", b->version);
                    fprintf(mf, "RELEASE=%s\n", b->release);
                    fprintf(mf, "ARCH=%s\n", b->arch);
                    fprintf(mf, "SHA256=%s\n", b->sha256);
                    fprintf(mf, "SIZE=%lld\n", (long long)st.st_size);
                    fprintf(mf, "BUILD_DATE=%s\n", b->build_date);
                    fprintf(mf, "BUILD_HOST=%s\n", b->build_host);
                    fprintf(mf, "MAINTAINER=%s\n", b->maintainer);
                    fprintf(mf, "DESCRIPTION=%s\n", b->description);
                    fclose(mf);
                    printf("  📄 Manifest created: %s\n", manifest);
                }
            } else {
                printf("  ❌ Failed to calculate SHA256\n");
            }
        } else {
            printf("  ❌ Archive file not found after tar command\n");
            printf("  🔍 Check if pkg-%s directory exists and has files\n", b->name);
            
            // Lister le contenu du répertoire pour debug
            snprintf(cmd, sizeof(cmd), "ls -la pkg-%s/", b->name);
            system(cmd);
        }
    } else {
        printf("  ❌ Tar command failed with code: %d\n", tar_result);
        
        // Essayer une commande alternative
        printf("[BOOL] Trying alternative tar command...\n");
        snprintf(cmd, sizeof(cmd), 
                 "tar -czf %s -C pkg-%s .", 
                 archive_name, b->name);
        printf("[BOOL] Running: %s\n", cmd);
        
        if (system(cmd) == 0 && access(archive_name, F_OK) == 0) {
            printf("  ✅ Archive created with alternative command\n");
            
            struct stat st;
            stat(archive_name, &st);
            b->file_size = st.st_size;
            printf("  ✅ Archive created: %s (%.2f KB)\n", 
                   archive_name, st.st_size / 1024.0);
        } else {
            printf("  ❌ Alternative command also failed\n");
        }
    }
    
    // Cleanup
    printf("[BOOL] Cleaning up...\n");
    snprintf(cmd, sizeof(cmd), "rm -rf pkg-%s", b->name);
    system(cmd);
    
    if (access(archive_name, F_OK) == 0) {
        return 0;
    } else {
        return -1;
    }
}

// Afficher les informations du package
int show_package_info(const char *package_path) {
    struct stat st;
    if (stat(package_path, &st) != 0) {
        printf("[BOOL] File not found: %s\n", package_path);
        return -1;
    }
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  BOOL Package Information\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    printf("  📦 File: %s\n", package_path);
    printf("  📏 Size: %.2f KB\n", st.st_size / 1024.0);
    
    // Calculer SHA256
    char sha256[128];
    if (calculate_file_sha256(package_path, sha256) == 0) {
        printf("  🔏 SHA256: %s\n", sha256);
    }
    
    // Vérifier si c'est une archive valide
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "file %s | grep -q 'gzip compressed data'", package_path);
    if (system(cmd) == 0) {
        printf("  ✅ Valid gzip archive\n");
        
        // Afficher le contenu
        printf("\n  📋 Archive contents:\n");
        snprintf(cmd, sizeof(cmd), "tar -tzf %s 2>/dev/null | head -10 | sed 's/^/    /'", package_path);
        system(cmd);
    } else {
        printf("  ❌ Not a valid gzip archive\n");
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  BOOL - APKM Package Builder v%s\n", BOOL_VERSION);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
        printf("Usage:\n");
        printf("  bool --build                 Build package from APKMBUILD\n");
        printf("  bool --info <package>        Show package information\n");
        printf("  bool --verify <package>      Verify package integrity\n");
        printf("  bool --help                   Show this help\n\n");
        printf("Examples:\n");
        printf("  bool --build\n");
        printf("  bool --info build/package.tar.bool\n");
        printf("  sha256sum -c build/package.tar.bool.sha256\n");
        return 0;
    }
    
    if (strcmp(argv[1], "--build") == 0) {
        // Créer le répertoire build
        mkdir("build", 0755);
        
        // Vérifier que APKMBUILD existe
        if (access("APKMBUILD", F_OK) != 0) {
            printf("[BOOL] Error: APKMBUILD not found in current directory\n");
            return 1;
        }
        
        apkm_build_t build_info = {0};
        parse_apkmbuild("APKMBUILD", &build_info);
        
        if (build_package(&build_info) == 0) {
            printf("\n[BOOL] ✅ Build completed successfully!\n");
            printf("[BOOL] 📦 Package: build/%s-v%s-%s.%s.tar.bool\n",
                   build_info.name, build_info.version, 
                   build_info.release, build_info.arch);
            printf("[BOOL] 🔏 SHA256: cat build/%s-v%s-%s.%s.tar.bool.sha256\n",
                   build_info.name, build_info.version, 
                   build_info.release, build_info.arch);
        } else {
            printf("\n[BOOL] ❌ Build failed\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "--info") == 0) {
        if (argc < 3) {
            printf("[BOOL] Error: Specify a package file\n");
            return 1;
        }
        show_package_info(argv[2]);
    }
    else if (strcmp(argv[1], "--verify") == 0) {
        if (argc < 3) {
            printf("[BOOL] Error: Specify a package file\n");
            return 1;
        }
        printf("[BOOL] Verifying %s...\n", argv[2]);
        
        // Vérifier si le fichier SHA256 existe
        char sha_file[512];
        snprintf(sha_file, sizeof(sha_file), "%s.sha256", argv[2]);
        
        if (access(sha_file, F_OK) == 0) {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "cd %s && sha256sum -c %s", 
                     dirname(strdup(sha_file)), basename(strdup(sha_file)));
            if (system(cmd) == 0) {
                printf("[BOOL] ✅ Package verified successfully\n");
            } else {
                printf("[BOOL] ❌ Package verification failed\n");
            }
        } else {
            // Calculer SHA256 directement
            char sha256[128];
            if (calculate_file_sha256(argv[2], sha256) == 0) {
                printf("[BOOL] 🔏 SHA256: %s\n", sha256);
                printf("[BOOL] No signature file found\n");
            }
        }
    }
    else if (strcmp(argv[1], "--help") == 0) {
        printf("BOOL - APKM Package Builder v%s\n", BOOL_VERSION);
        printf("Usage: bool --build\n");
        printf("Options:\n");
        printf("  --build     Build package from APKMBUILD\n");
        printf("  --info      Show package information\n");
        printf("  --verify    Verify package integrity\n");
        printf("  --help      Show this help\n");
    }
    else {
        printf("[BOOL] Unknown option: %s\n", argv[1]);
        printf("Try 'bool --help'\n");
        return 1;
    }
    
    return 0;
}
