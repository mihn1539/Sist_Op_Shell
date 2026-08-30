#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT 1024

int main() {
    char input[MAX_INPUT];

    while (1) {
        printf("shell$ ");
        fflush(stdout); // obliga al sistema a escribir de inmediato el prompt

        if (fgets(input, sizeof(input), stdin) == NULL) break;

        input[strcspn(input, "\n")] = 0; // elimina el salto de linea

        // si la entrada es vacía,
        // avanza a la siguiente iteración imprimiendo el prompt nuevamente
        if (strlen(input) == 0) continue;

        if (strcmp(input, "exit") == 0) break;
    }

    return EXIT_SUCCESS;
}