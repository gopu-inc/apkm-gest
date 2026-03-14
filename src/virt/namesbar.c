#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // Simulation du changement de prompt : [::<nameenv>::]alpine@root:~#
    // Dans une version finale, on modifierait la variable PS1 ici.
    char *prompt = "[::<anv_env>::]alpine@root:~# ";
    
    char buffer[1024];
    while(1) {
        printf("%s", prompt);
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        
        // Logique de filtrage des commandes (namesbar)
        if (strncmp(buffer, "rm -rf /", 8) == 0) {
            printf("Security Alert: Command forbidden in namesbar.\n");
            continue;
        }
        
        // Exécution de la commande autorisée...
    }
    return 0;
}

