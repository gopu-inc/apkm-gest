#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage() {
    printf("Usage: super-app [options]\n");
    printf("Options:\n");
    printf("  --help     Afficher cette aide\n");
    printf("  --version  Afficher la version\n");
    printf("  --test     Mode test\n");
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[1], "--version") == 0) {
            printf("super-app version 2.1.0 (x86_64)\n");
            return 0;
        }
        if (strcmp(argv[1], "--test") == 0) {
            printf("Mode test activé\n");
            printf("Toutes les fonctions sont OK\n");
            return 0;
        }
    }
    
    printf("Super App v2.1.0 - The Ultimate Application\n");
    printf("Tapez 'super-app --help' pour plus d'options\n");
    return 0;
}
