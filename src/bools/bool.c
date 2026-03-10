#include "bool.h"
#include <getopt.h>
#include <libgen.h>

void print_banner() {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║     BOOL v%s - SELP Archive Tool          ║\n", BOOL_VERSION);
    printf("║     [2.0]: SELP bool (c) 2026 003x2022 223222x22 ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");
}

void print_help() {
    printf("Usage: bool [options] <input>... [output]\n\n");
    printf("Options:\n");
    printf("  -c, --compress      Compress files/directories to .selp.bool\n");
    printf("  -x, --extract       Extract .selp.bool archive\n");
    printf("  -l, --list          List contents of archive\n");
    printf("  -v, --verify        Verify archive signature\n");
    printf("  -i, --info          Show archive information\n");
    printf("  -m, --magic         Show SELP magic information\n\n");
    
    printf("Compression options:\n");
    printf("  -L, --level <0-3>   Compression level (0=none, 1=fast, 2=best, 3=ultra)\n");
    printf("  -E, --encrypt <0-3> Encryption level (0=none, 1=light, 2=medium, 3=strong)\n");
    printf("  -d, --dir-mode      Directory mode (follow symlinks)\n");
    printf("  -D, --no-deref      Don't follow symlinks\n");
    printf("  -o, --output <file> Output filename\n");
    printf("  -a, --author <name> Set author name\n");
    printf("  -t, --comment <str> Add comment\n\n");
    
    printf("Other options:\n");
    printf("  -q, --quiet         Suppress output\n");
    printf("  -h, --help          Show this help\n\n");
    
    printf("Examples:\n");
    printf("  bool -c file1.txt file2.txt -o archive.selp.bool\n");
    printf("  bool -c -L3 -E3 -a \"Mauricio\" -t \"Backup\" dossier/\n");
    printf("  bool -c -d dossier/                    # Follow symlinks\n");
    printf("  bool -x archive.selp.bool extracted/   # Extract to folder\n");
    printf("  bool -l archive.selp.bool              # List contents\n");
    printf("  bool -v archive.selp.bool              # Verify signature\n");
}

int main(int argc, char *argv[]) {
    print_banner();
    
    if (argc < 2) {
        print_help();
        return 1;
    }
    
    int compress = 0;
    int extract = 0;
    int list = 0;
    int verify = 0;
    int info = 0;
    int magic = 0;
    int level = 2;
    int encrypt = 0;
    int dir_mode = 0;
    int follow_links = 1;
    int quiet = 0;
    char *output = NULL;
    char *author = NULL;
    char *comment = NULL;
    
    static struct option long_options[] = {
        {"compress",  no_argument,       0, 'c'},
        {"extract",   no_argument,       0, 'x'},
        {"list",      no_argument,       0, 'l'},
        {"verify",    no_argument,       0, 'v'},
        {"info",      no_argument,       0, 'i'},
        {"magic",     no_argument,       0, 'm'},
        {"level",     required_argument, 0, 'L'},
        {"encrypt",   required_argument, 0, 'E'},
        {"dir-mode",  no_argument,       0, 'd'},
        {"no-deref",  no_argument,       0, 'D'},
        {"output",    required_argument, 0, 'o'},
        {"author",    required_argument, 0, 'a'},
        {"comment",   required_argument, 0, 't'},
        {"quiet",     no_argument,       0, 'q'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "cxlvimL:E:dDo:a:t:qh", 
                              long_options, NULL)) != -1) {
        switch (opt) {
            case 'c': compress = 1; break;
            case 'x': extract = 1; break;
            case 'l': list = 1; break;
            case 'v': verify = 1; break;
            case 'i': info = 1; break;
            case 'm': magic = 1; break;
            case 'L': level = atoi(optarg); break;
            case 'E': encrypt = atoi(optarg); break;
            case 'd': dir_mode = 1; follow_links = 1; break;
            case 'D': follow_links = 0; break;
            case 'o': output = optarg; break;
            case 'a': author = optarg; break;
            case 't': comment = optarg; break;
            case 'q': quiet = 1; break;
            case 'h': print_help(); return 0;
            default: print_help(); return 1;
        }
    }
    
    if (optind >= argc) {
        printf("❌ No input specified\n");
        return 1;
    }
    
    // Si c'est un seul fichier/dossier
    char *input = argv[optind];
    
    // Déterminer le nom de sortie si non spécifié
    if (!output) {
        if (compress) {
            output = malloc(strlen(input) + 16);
            sprintf(output, "%s.selp.bool", input);
        } else if (extract) {
            output = "extracted";
        }
    }
    
    int result = 0;
    
    if (compress) {
        struct stat st;
        stat(input, &st);
        
        if (S_ISDIR(st.st_mode) || dir_mode) {
            // Compression de dossier
            printf("📁 Directory mode: %s\n", input);
            result = selp_compress_directory(input, output, level, encrypt,
                                            author, comment, follow_links);
        } else {
            // Compression de fichiers multiples
            int file_count = argc - optind;
            if (file_count > 1) {
                printf("📦 Multi-file mode: %d files\n", file_count);
                result = selp_compress_files(file_count, &argv[optind], 
                                            output, level, encrypt,
                                            author, comment);
            } else {
                // Fichier unique (à implémenter avec la compression simple)
                printf("📦 Single file mode: %s\n", input);
                // Appeler la fonction de compression simple
            }
        }
    }
    else if (extract) {
        result = selp_extract(input, output);
    }
    else if (list) {
        result = selp_list(input);
    }
    else if (verify) {
        result = selp_verify(input);
    }
    else if (info) {
        result = selp_info(input);
    }
    else if (magic) {
        result = selp_magic_info(input);
    }
    
    if (result == SELP_OK) {
        printf("\n✅ Operation completed successfully!\n");
        return 0;
    } else {
        printf("\n❌ Operation failed with code: %d\n", result);
        return 1;
    }
}
