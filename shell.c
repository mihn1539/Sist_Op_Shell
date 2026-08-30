#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64

int ejecutar_comando(char *args[]) {
    if (args[0] == NULL) return 0;

    // crear un proceso hijo para ejecutar el comando
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork fallido");
        return -1;
    }

    // el proceso hijo ejecuta el comando solicitado
    if (pid == 0) {
        execvp(args[0], args);
        perror("exec fallido");
        exit(EXIT_FAILURE);
    }

    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status)) return WEXITSTATUS(status);

    return -1;
}

int main() {
    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    int arg_count;

    while (1) {
        printf("shell$ ");
        fflush(stdout); // obliga al sistema a escribir de inmediato el prompt

        if (fgets(input, sizeof(input), stdin) == NULL) break;

        input[strcspn(input, "\n")] = 0; // elimina el salto de linea

        // si la entrada es vacía,
        // avanza a la siguiente iteración imprimiendo el prompt nuevamente
        if (strlen(input) == 0) continue;

        if (strcmp(input, "exit") == 0) break;

        char *token = strtok(input, " ");
        arg_count = 0;

        while (token != NULL && arg_count < MAX_ARGS - 1) {
            args[arg_count++] = token;
            token = strtok(NULL, " ");
        }
        args[arg_count] = NULL;

        int exit_code = ejecutar_comando(args);
    }

    return EXIT_SUCCESS;
}