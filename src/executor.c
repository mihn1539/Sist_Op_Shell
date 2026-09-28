#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "background.h"
#include "executor.h"
#include "redirection.h"
#include "signals.h"

// ejecuta el conjunto de comandos con todo el manejo de distintas operaciones
static int ejecutar_lanzamiento(Comando comandos[], int inicio, int fin,
                                int background) {
    int cantidad = fin - inicio + 1;
    int pipes[cantidad > 1 ? cantidad - 1 : 1][2];
    pid_t procesos[cantidad];
    pid_t pgid = 0;
    sigset_t mascara_anterior;

    if (bloquear_sigchld(&mascara_anterior) == -1)
        return -1;

    for (int i = 0; i < cantidad - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Error al crear la pipe");
            restaurar_mascara_signals(&mascara_anterior);
            return -1;
        }
    }

    for (int i = 0; i < cantidad; i++) {
        procesos[i] = fork();
        if (procesos[i] == -1) {
            perror("Error al crear el proceso");
            for (int j = 0; j < cantidad - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            restaurar_mascara_signals(&mascara_anterior);
            return -1;
        }

        // proceso hijo
        if (procesos[i] == 0) {
            restaurar_signals_hijo();

            // si el comando se ejecuta en segundo plano, establecer el grupo de procesos del hijo
            if (background) {
                if (i == 0)
                    setpgid(0, 0);
                else
                    setpgid(0, pgid);
            }

            // redirigir la entrada y salida estándar según corresponda para el comando actual
            if (i > 0 && dup2(pipes[i - 1][0], STDIN_FILENO) == -1)
                _exit(EXIT_FAILURE);
            if (i < cantidad - 1 && dup2(pipes[i][1], STDOUT_FILENO) == -1)
                _exit(EXIT_FAILURE);

            for (int j = 0; j < cantidad - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            if (aplicar_redirecciones(&comandos[inicio + i]) == -1)
                _exit(EXIT_FAILURE);

            execvp(comandos[inicio + i].args[0], comandos[inicio + i].args);
            perror("exec fallido");
            _exit(EXIT_FAILURE);
        }

        if (i == 0)
            pgid = procesos[i];
        if (background)
            setpgid(procesos[i], pgid);
    }

    for (int i = 0; i < cantidad - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // si el comando se ejecuta en segundo plano, registrar el job y devolver inmediatamente el control al usuario
    if (background) {
        if (registrar_job(comandos, inicio, fin, pgid, procesos, cantidad) == -1) {
            kill(-pgid, SIGTERM);
            restaurar_mascara_signals(&mascara_anterior);
            return -1;
        }
        restaurar_mascara_signals(&mascara_anterior);
        return EXIT_SUCCESS;
    }

    int ultimo_estado = EXIT_FAILURE;

    // esperar a que todos los procesos hijos terminen y obtener el estado de salida del último proceso
    for (int i = 0; i < cantidad; i++) {
        int status;
        while (waitpid(procesos[i], &status, 0) == -1) {
            if (errno != EINTR) {
                restaurar_mascara_signals(&mascara_anterior);
                return -1;
            }
        }
        if (i == cantidad - 1 && WIFEXITED(status))
            ultimo_estado = WEXITSTATUS(status);
    }

    // restaurar la máscara de señales anterior para permitir que se manejen las señales nuevamente
    restaurar_mascara_signals(&mascara_anterior);
    return ultimo_estado;
}

// ejecuta un comando individual, manejando el caso especial del comando interno "cd"
int ejecutar_comando(Comando *cmd) {
    if (cmd->args[0] == NULL)
        return EXIT_SUCCESS;

    if (strcmp(cmd->args[0], "cd") == 0) {
        const char *path = cmd->args[1];
        if (path == NULL || strcmp(path, "~") == 0)
            path = getenv("HOME");
        if (path == NULL || chdir(path) == -1) {
            perror("cd fallido");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    return ejecutar_lanzamiento(cmd, 0, 0, cmd->background);
}


// ejecuta un conjunto de comandos, manejando las operaciones lógicas y de tuberías entre ellos
int ejecutar_comandos(Comando comandos[], int cmd_count) {
    int ultimo_estado = EXIT_SUCCESS;

    // iterar sobre los comandos y ejecutar cada uno según su operador lógico y de tuberías
    for (int i = 0; i < cmd_count;) {
        int fin = i; // variable para determinar el índice del último comando a ejecutar en la secuencia actual
        while (fin < cmd_count - 1 && comandos[fin].operador == OP_PIPE)
            fin++;

        if (i > 0) {
            Operador anterior = comandos[i - 1].operador;
            if ((anterior == OP_AND && ultimo_estado != EXIT_SUCCESS) ||
                (anterior == OP_OR && ultimo_estado == EXIT_SUCCESS)) {
                i = fin + 1;
                continue;
            }
        }

        // si el comando es "cd" y no está en una tubería, ejecutarlo directamente y actualizar el estado de salida
        if (strcmp(comandos[i].args[0], "cd") == 0 && fin == i)
            ultimo_estado = ejecutar_comando(&comandos[i]);
        else
            ultimo_estado = ejecutar_lanzamiento(
                comandos, i, fin, comandos[fin].background);

        if (ultimo_estado == -1)
            return -1;
        i = fin + 1;
    }

    return ultimo_estado;
}