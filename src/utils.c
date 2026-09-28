#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "utils.h"

// imprime el prompt de la shell, mostrando el directorio actual y el nombre de usuario
void imprimir_prompt(void) {
    char cwd[1024];
    char *home = getenv("HOME");

    if (home != NULL && getcwd(cwd, sizeof(cwd)) != NULL) {
        if (strncmp(cwd, home, strlen(home)) == 0) {
            printf("\e[32mshell\e[0m:\e[34m~%s\e[0m$ ",
                   cwd + strlen(home));
        } else {
            printf("\e[32mshell\e[0m:\e[34m%s\e[0m$ ", cwd);
        }
    }
}