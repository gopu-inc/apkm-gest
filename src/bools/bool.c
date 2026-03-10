#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "bool.h"

// Déclarations externes
int selp_compress(const char *input, const char *output, int comp, int crypt);
int selp_decompress(const char *input, const char *output);
int selp_create_archive(const char *dir, const char *output, int comp, int crypt);
int selp_extract_archive(const char *archive, const char *output);
int selp_verify_signature_file(const char *path);

void print_banner() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║     BOOL v%s - SELP Archive Tool          ║\n", BOOL_VERSION);
    printf("║     [2.0]: SELP bool (c) 2026 003x2022 223222x22 ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
}

void print_help() {
    printf("Usage: bool [options] <input> [output]\n\n");
    printf("Options:\n");
    printf("  -c, --compress      Compress file/directory to .selp.bool\n");
    printf("  -x, --extract       Extract .selp.bool archive\n");
    printf("  -l, --level <0-3>   Compression level (0=none, 1=fast, 2=best, 3=ultra)\n");
    printf("  -e, --encrypt <0-3> Encryption level (0=none, 1=light, 2=medium, 3=strong)\n");
    printf("  -v, --verify        Verify archive signature\n");
    printf("  -i, --info          Show archive information\n");
    printf("  -h, --help          Show this help\n\n");
    printf("Examples:\n");
    printf("  bool -c myfolder                  # Create myfolder.selp.bool\n");
    echo"  bool -c -l3 -e3 myfolder           # Ultra compression + strong encryption\n");
    printf("  bool -x archive.selp.bool         # Extract archive\n");
    printf("  bool -v archive.selp.bool         # Verify signature\n");
}

int main(int argc, char *argv[]) {
    print_banner();
    
    if (argc < 2) {
        print_help();
        return 1;
    }
    
    int compress = 0;
    int extract = 0;
    int verify = 0;
    int info = 0;
    int level = 2;      // Default: best
    int encrypt = 0;    // Default: none
    char *input = NULL;
    char *output = NULL;
    
    static struct option long_options[] = {
        {"compress", no_argument, 0, 'c'},
        {"extract", no_argument, 0, 'x'},
        {"level", required_argument, 0, 'l'},
        {"encrypt", required_argument, 0, 'e'},
        {"verify", no_argument, 0, 'v'},
        {"info", no_argument, 0, 'i'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "cxl:e:vih", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c': compress = 1; break;
            case 'x': extract = 1; break;
            case 'l': level = atoi(optarg); break;
            case 'e': encrypt = atoi(optarg); break;
            case 'v': verify = 1; break;
            case 'i': info = 1; break;
            case 'h': print_help(); return 0;
            default: print_help(); return 1;
        }
    }
    
    if (optind < argc) {
        input = argv[optind++];
    }
    if (optind < argc) {
        output = argv[optind];
    }
    
    if (!input) {
        printf("❌ No input specified\n");
        return 1;
    }
    
    int result = 0;
    
    if (compress) {
        printf("📦 Compressing: %s\n", input);
        if (!output) {
            output = malloc(strlen(input) + 16);
            sprintf(output, "%s.selp.bool", input);
        }
        result = selp_compress(input, output, level, encrypt);
    }
    else if (extract) {
        printf("📂 Extracting: %s\n", input);
        if (!output) output = "extracted";
        result = selp_decompress(input, output);
    }
    else if (verify) {
        printf("🔐 Verifying: %s\n", input);
        result = selp_verify_signature_file(input);
    }
    else if (info) {
        printf("ℹ️  Info: %s\n", input);
        // Afficher les infos de l'archive
    }
    
    if (result == SELP_OK) {
        printf("\n✅ Operation completed successfully!\n");
        return 0;
    } else {
        printf("\n❌ Operation failed with code: %d\n", result);
        return 1;
    }
}
