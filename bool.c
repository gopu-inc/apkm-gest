#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#define BOOL_VERSION "2.0.0"

void print_banner() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║     BOOL v%s - Package Builder              ║\n", BOOL_VERSION);
    printf("║     [tar.bool] Archive Tool                     ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
}

void print_help() {
    printf("Usage: bool [options] <input>\n\n");
    printf("Options:\n");
    printf("  -b, --build         Build package from APKMBUILD\n");
    printf("  -c, --create        Create .tar.bool archive from directory\n");
    printf("  -x, --extract       Extract .tar.bool archive\n");
    printf("  -t, --list          List contents of archive\n");
    printf("  -m, --manifest      Generate manifest.toml\n");
    printf("  -h, --help          Show this help\n\n");
    printf("Examples:\n");
    printf("  bool -c mypackage/              # Create mypackage.tar.bool\n");
    printf("  bool -x mypackage.tar.bool      # Extract archive\n");
    printf("  bool -t mypackage.tar.bool      # List contents\n");
    printf("  bool -b                          # Build from APKMBUILD\n");
}

int create_archive(const char *dir, const char *output) {
    if (!output) {
        output = malloc(strlen(dir) + 16);
        sprintf((char*)output, "%s.tar.bool", dir);
    }
    
    printf("📦 Creating archive: %s\n", output);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -cf '%s' -C '%s' . 2>/dev/null", output, dir);
    
    int ret = system(cmd);
    if (ret == 0) {
        struct stat st;
        stat(output, &st);
        printf("✅ Archive created: %.2f KB\n", st.st_size / 1024.0);
        return 0;
    }
    
    printf("❌ Failed to create archive\n");
    return -1;
}

int extract_archive(const char *archive, const char *dest) {
    if (!dest) dest = ".";
    
    printf("📂 Extracting: %s\n", archive);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -xf '%s' -C '%s' 2>/dev/null", archive, dest);
    
    int ret = system(cmd);
    if (ret == 0) {
        printf("✅ Extraction complete\n");
        return 0;
    }
    
    printf("❌ Failed to extract archive\n");
    return -1;
}

int list_archive(const char *archive) {
    printf("📋 Contents of: %s\n", archive);
    printf("────────────────────────────\n");
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -tvf '%s' 2>/dev/null", archive);
    
    return system(cmd);
}

int generate_manifest(const char *dir) {
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.toml", dir);
    
    FILE *f = fopen(manifest_path, "w");
    if (!f) {
        printf("❌ Cannot create manifest\n");
        return -1;
    }
    
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    
    fprintf(f, "# BOOL Package Manifest\n");
    fprintf(f, "# Generated: %04d-%02d-%02d %02d:%02d:%02d\n\n",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    
    fprintf(f, "[metadata]\n");
    fprintf(f, "name = \"package\"\n");
    fprintf(f, "version = \"1.0.0\"\n");
    fprintf(f, "release = \"r0\"\n");
    fprintf(f, "arch = \"x86_64\"\n");
    fprintf(f, "maintainer = \"\"\n");
    fprintf(f, "description = \"\"\n");
    fprintf(f, "license = \"MIT\"\n\n");
    
    fprintf(f, "[apkm]\n");
    fprintf(f, "version = [\"2.0.0\", {apsm = \"2.0.0\"}, {bool = \"2.0.0\"}]\n");
    fprintf(f, "release = \"1\"\n\n");
    
    fprintf(f, "[dependencies]\n");
    fprintf(f, "# Format: name = \"version\"\n");
    fprintf(f, "# or: name = [\"version\", {dep = \"version\"}]\n\n");
    
    fclose(f);
    printf("✅ Manifest created: %s\n", manifest_path);
    return 0;
}

int build_package() {
    if (access("APKMBUILD", F_OK) != 0) {
        printf("❌ APKMBUILD not found\n");
        return -1;
    }
    
    printf("🔨 Building package from APKMBUILD...\n");
    
    // Ici on lirait APKMBUILD et on construirait le package
    // Version simplifiée pour l'exemple
    generate_manifest(".");
    
    printf("✅ Build complete\n");
    return 0;
}

int main(int argc, char *argv[]) {
    print_banner();
    
    if (argc < 2) {
        print_help();
        return 1;
    }
    
    if (strcmp(argv[1], "-b") == 0 || strcmp(argv[1], "--build") == 0) {
        return build_package();
    }
    else if (strcmp(argv[1], "-c") == 0 || strcmp(argv[1], "--create") == 0) {
        if (argc < 3) {
            printf("❌ Missing directory name\n");
            return 1;
        }
        return create_archive(argv[2], argc > 3 ? argv[3] : NULL);
    }
    else if (strcmp(argv[1], "-x") == 0 || strcmp(argv[1], "--extract") == 0) {
        if (argc < 3) {
            printf("❌ Missing archive name\n");
            return 1;
        }
        return extract_archive(argv[2], argc > 3 ? argv[3] : NULL);
    }
    else if (strcmp(argv[1], "-t") == 0 || strcmp(argv[1], "--list") == 0) {
        if (argc < 3) {
            printf("❌ Missing archive name\n");
            return 1;
        }
        return list_archive(argv[2]);
    }
    else if (strcmp(argv[1], "-m") == 0 || strcmp(argv[1], "--manifest") == 0) {
        return generate_manifest(argc > 2 ? argv[2] : ".");
    }
    else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }
    else {
        printf("❌ Unknown option: %s\n", argv[1]);
        print_help();
        return 1;
    }
    
    return 0;
}
