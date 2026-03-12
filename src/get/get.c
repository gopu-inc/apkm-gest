#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <toml.h>

void print_colored(const char* key, const char* value) {
    if (strstr(key, "color")) {
        // Applique le code ANSI dynamiquement via la valeur du TOML
        printf("\033[1;%sm%s: %s\033[0m\n", value, key, value);
    } else {
        printf("\033[1;36m%s\033[0m: %s\n", key, value);
    }
}

void print_table(const char* name, toml_table_t* table) {
    printf("\n\033[1;33m\033[0m\n");
    printf("\033[1;37m S:[%s]\033[0m\n", name);
    printf("\033[1;33m\033[0m\n");
    
    for (int i = 0; ; i++) {
        const char* key = toml_key_in(table, i);
        if (!key) break;
        
        toml_table_t* sub = toml_table_in(table, key);
        if (sub) {
            char new_name[256];
            snprintf(new_name, sizeof(new_name), "%s.%s", name, key);
            print_table(new_name, sub);
        } else {
            const char* val = toml_raw_in(table, key);
            if (val) {
                char clean[256];
                int j = 0, k = 0;
                while(val[j] != '\0') {
                    if (val[j] != '\"') clean[k++] = val[j];
                    j++;
                }
                clean[k] = '\0';
                print_colored(key, clean);
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;

    FILE* fp = fopen(argv[1], "r");
    if (!fp) return 1;

    char errbuf[200];
    toml_table_t* conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!conf) { printf("Error: %s\n", errbuf); return 1; }

    // Mode filtrage : si un 3ème argument est passé (ex: [security])
    if (argc == 3) {
        toml_table_t* tab = toml_table_in(conf, argv[2]);
        if (tab) print_table(argv[2], tab);
        else printf("Section %s introuvable.\n", argv[2]);
    } else {
        // Mode complet : parcourt toutes les sections racines
        for (int i = 0; ; i++) {
            const char* key = toml_key_in(conf, i);
            if (!key) break;
            toml_table_t* tab = toml_table_in(conf, key);
            if (tab) print_table(key, tab);
        }
    }

    toml_free(conf);
    return 0;
}

