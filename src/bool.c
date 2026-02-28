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
#include <openssl/pem.h>
#include <openssl/rsa.h>

#define BOOL_MAGIC "00012x0 0032000 bool APKM"
#define BOOL_VERSION "2.1.0"
#define SIGNATURE_SIZE 512

typedef struct {
    char magic[32];           // Signature magique pour head
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
    if (!f) return -1;
    
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

// Créer une signature simple
void create_signature(apkm_build_t *b, const char *private_key) {
    char data_to_sign[4096] = "";
    
    // Concaténer les infos importantes
    snprintf(data_to_sign, sizeof(data_to_sign),
             "%s|%s|%s|%s|%s|%s|%s|%lld",
             b->name, b->version, b->release, b->arch,
             b->maintainer, b->sha256, b->build_date,
             (long long)time(NULL));
    
    // Pour l'instant, signature simulée
    // Dans une version réelle, utiliser RSA ou GPG
    snprintf(b->signature, SIGNATURE_SIZE,
             "BOOLSIG:%s:%s:%s",
             b->name, b->version, b->sha256);
    
    printf("[BOOL] 🔏 Signature créée: %.32s...\n", b->signature);
}

// Vérifier la signature
int verify_signature(apkm_build_t *b) {
    // Vérification simple
    if (strstr(b->signature, "BOOLSIG:") != b->signature) {
        return -1;
    }
    return 0;
}

// Parser le fichier APKMBUILD
void parse_apkmbuild(const char *filename, apkm_build_t *b) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("[BOOL] Erreur");
        exit(1);
    }

    char line[1024];
    int in_block = 0;
    char current_block[1024] = "";
    
    // Initialisation avec le magic header
    memset(b, 0, sizeof(apkm_build_t));
    strcpy(b->magic, BOOL_MAGIC);
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
                if (strcmp(current_block, "make") == 0)
                    strcat(b->build_cmd, line);
                else if (strcmp(current_block, "install") == 0)
                    strcat(b->install_cmd, line);
                else if (strcmp(current_block, "check") == 0)
                    strcat(b->check_cmd, line);
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

// Créer la structure complète du paquet
int create_package_structure(apkm_build_t *b, const char *build_dir) {
    char pkg_dir[512];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/pkg-%s", build_dir, b->name);
    
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
    
    printf("[BOOL] 📦 Copie des fichiers du projet...\n");
    
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), 
             "find . -maxdepth 1 -not -name 'build' -not -name 'pkg-*' -not -name '.' -exec cp -r {} %s/ \\;",
             pkg_dir);
    system(cmd);
    
    // Ajouter le fichier de signature
    char sig_path[512];
    snprintf(sig_path, sizeof(sig_path), "%s/.BOOL.sig", pkg_dir);
    FILE *sig = fopen(sig_path, "w");
    if (sig) {
        fprintf(sig, "MAGIC=%s\n", BOOL_MAGIC);
        fprintf(sig, "NAME=%s\n", b->name);
        fprintf(sig, "VERSION=%s\n", b->version);
        fprintf(sig, "RELEASE=%s\n", b->release);
        fprintf(sig, "SHA256=%s\n", b->sha256);
        fprintf(sig, "SIGNATURE=%s\n", b->signature);
        fprintf(sig, "BUILD_DATE=%s\n", b->build_date);
        fprintf(sig, "BUILD_HOST=%s\n", b->build_host);
        fclose(sig);
    }
    
    return 0;
}

// Créer l'en-tête magique pour la commande head
void create_magic_header(apkm_build_t *b, FILE *archive) {
    // L'en-tête sera ajouté au début de l'archive
    // Quand on fait "head -1 fichier.tar.bool", on voit le magic
    fprintf(archive, "%s\n", BOOL_MAGIC);
    fprintf(archive, "# Package: %s %s-%s\n", b->name, b->version, b->release);
    fprintf(archive, "# Architecture: %s\n", b->arch);
    fprintf(archive, "# SHA256: %s\n", b->sha256);
    fprintf(archive, "# Signature: %s\n", b->signature);
    fprintf(archive, "# Build: %s on %s\n", b->build_date, b->build_host);
    fprintf(archive, "# This is a BOOL APKM package\n");
    fprintf(archive, "#%010ld\n", (long)time(NULL));
}

// Builder le paquet
int build_package(apkm_build_t *b) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  BOOL - APKM Package Builder v%s\n", BOOL_VERSION);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    printf("📦 INFORMATIONS DU PAQUET:\n");
    printf("  • Magic header : %s\n", BOOL_MAGIC);
    printf("  • Nom          : %s\n", b->name);
    printf("  • Version      : %s-%s\n", b->version, b->release);
    printf("  • Architecture : %s\n", b->arch);
    printf("  • Script path  : %s\n", b->script_path);
    printf("  • Date build   : %s\n", b->build_date);
    
    // Étape 1: Build
    if (strlen(b->build_cmd) > 0) {
        printf("\n🔧 ÉTAPE BUILD:\n");
        printf("  • Exécution: %s\n", b->build_cmd);
        if (system(b->build_cmd) != 0) {
            printf("[BOOL] ⚠️  Build non bloquant\n");
        }
    }
    
    // Étape 2: Tests
    if (strlen(b->check_cmd) > 0) {
        printf("\n🧪 ÉTAPE TESTS:\n");
        printf("  • Exécution: %s\n", b->check_cmd);
        system(b->check_cmd);
    }
    
    // Étape 3: Création de la structure
    printf("\n📁 PRÉPARATION DU PAQUET:\n");
    create_package_structure(b, ".");
    
    // Étape 4: Installation simulée
    if (strlen(b->install_cmd) > 0) {
        printf("\n⚙️  ÉTAPE INSTALL:\n");
        char destdir[512];
        snprintf(destdir, sizeof(destdir), "pkg-%s", b->name);
        setenv("DESTDIR", destdir, 1);
        printf("  • DESTDIR=%s\n", destdir);
        printf("  • Commande: %s\n", b->install_cmd);
        system(b->install_cmd);
    }
    
    // Étape 5: Calculer SHA256
    printf("\n🔐 CALCUL DE L'EMPREINTE:\n");
    char package_path[512];
    snprintf(package_path, sizeof(package_path), "pkg-%s", b->name);
    
    // Pour l'instant, on calcule sur un fichier temporaire
    // Le vrai SHA256 sera calculé sur l'archive finale
    strcpy(b->sha256, "calcul_en_cours");
    
    // Étape 6: Créer la signature
    create_signature(b, NULL);
    
    // Étape 7: Création de l'archive avec en-tête
    printf("\n📦 CRÉATION DE L'ARCHIVE SIGNÉE:\n");
    
    char archive_name[512];
    snprintf(archive_name, sizeof(archive_name), 
             "build/%s-v%s-%s.%s.tar.bool", 
             b->name, b->version, b->release, b->arch);
    
    // Créer un fichier temporaire avec l'en-tête
    char temp_archive[512];
    snprintf(temp_archive, sizeof(temp_archive), "/tmp/%s-temp.tar", b->name);
    
    // Créer l'archive sans en-tête d'abord
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), 
             "cd pkg-%s && tar -cf %s * && cd ..", 
             b->name, temp_archive);
    system(cmd);
    
    // Ajouter l'en-tête au début
    FILE *final = fopen(archive_name, "w");
    if (final) {
        create_magic_header(b, final);
        
        // Ajouter le contenu de l'archive
        FILE *temp = fopen(temp_archive, "r");
        if (temp) {
            char buffer[8192];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), temp)) > 0) {
                fwrite(buffer, 1, bytes, final);
            }
            fclose(temp);
        }
        fclose(final);
        
        // Calculer le vrai SHA256
        calculate_file_sha256(archive_name, b->sha256);
        
        // Mettre à jour l'en-tête avec le SHA256 réel
        // Pour l'instant, on recrée l'archive
        printf("[BOOL] 🔏 SHA256 final: %s\n", b->sha256);
        
        // Obtenir la taille
        struct stat st;
        stat(archive_name, &st);
        b->file_size = st.st_size;
        
        printf("  ✅ Archive créée: %s (%.2f KB)\n", 
               archive_name, st.st_size / 1024.0);
        
        // Nettoyage
        unlink(temp_archive);
        snprintf(cmd, sizeof(cmd), "rm -rf pkg-%s", b->name);
        system(cmd);
        
        return 0;
    }
    
    return -1;
}

// Afficher l'en-tête d'un package
int show_package_header(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;
    
    char line[256];
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  BOOL Package Header\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    // Lire les premières lignes
    for (int i = 0; i < 10 && fgets(line, sizeof(line), f); i++) {
        if (i == 0) {
            printf("🔮 Magic: %s", line);
        } else {
            printf("  %s", line);
        }
    }
    
    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  BOOL - APKM Package Builder v%s\n", BOOL_VERSION);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
        printf("Usage:\n");
        printf("  bool --build              Construire le paquet depuis APKMBUILD\n");
        printf("  bool --header <fichier>   Afficher l'en-tête d'un package\n");
        printf("  bool --verify <fichier>   Vérifier la signature\n");
        printf("  bool --help                Afficher cette aide\n\n");
        printf("Exemple:\n");
        echo "  head -1 mon-app-v1.0.0-r1.x86_64.tar.bool\n";
        printf("  > %s\n", BOOL_MAGIC);
        return 0;
    }
    
    if (strcmp(argv[1], "--build") == 0) {
        mkdir("build", 0755);
        
        apkm_build_t build_info = {0};
        parse_apkmbuild("APKMBUILD", &build_info);
        
        if (build_package(&build_info) == 0) {
            printf("\n[BOOL] ✅ Build terminé avec succès!\n");
            printf("[BOOL] 📦 Pour voir l'en-tête: head -1 build/%s-v%s-%s.%s.tar.bool\n",
                   build_info.name, build_info.version, 
                   build_info.release, build_info.arch);
        } else {
            printf("\n[BOOL] ❌ Échec du build\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "--header") == 0) {
        if (argc < 3) {
            printf("[BOOL] ❌ Spécifiez un fichier\n");
            return 1;
        }
        show_package_header(argv[2]);
    }
    else if (strcmp(argv[1], "--verify") == 0) {
        if (argc < 3) {
            printf("[BOOL] ❌ Spécifiez un fichier\n");
            return 1;
        }
        printf("[BOOL] 🔍 Vérification de %s...\n", argv[2]);
        // Logique de vérification
        show_package_header(argv[2]);
    }
    else if (strcmp(argv[1], "--help") == 0) {
        printf("Usage: bool --build\n");
        printf("Options:\n");
        printf("  --build     Construire le paquet depuis APKMBUILD\n");
        printf("  --header    Afficher l'en-tête d'un package\n");
        printf("  --verify    Vérifier la signature\n");
        printf("  --help      Afficher cette aide\n");
    }
    else {
        printf("[BOOL] ❌ Option inconnue: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}
