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

static void terminar_procesos(pid_t procesos[], int cantidad, pid_t pgid) {
    if (pgid > 0)
        kill(-pgid, SIGCONT);
    for (int i = 0; i < cantidad; i++)
        kill(procesos[i], SIGTERM);

    for (int i = 0; i < cantidad; i++) {
        while (waitpid(procesos[i], NULL, 0) == -1 && errno == EINTR)
            ;
    }
}

// ejecuta el conjunto de comandos con todo el manejo de distintas operaciones
static int ejecutar_lanzamiento(Comando comandos[], int inicio, int fin,
                                int background) {
    int cantidad = fin - inicio + 1;
    int (*pipes)[2] = NULL;
    pid_t *procesos = malloc((size_t)cantidad * sizeof(*procesos));
    EstadoProceso *estados = calloc((size_t)cantidad, sizeof(*estados));
    pid_t pgid = 0;
    sigset_t mascara_anterior;

    if (procesos == NULL || estados == NULL) {
        free(procesos);
        free(estados);
        return -1;
    }

    if (cantidad > 1) {
        pipes = malloc((size_t)(cantidad - 1) * sizeof(*pipes));
        if (pipes == NULL) {
            free(procesos);
            free(estados);
            return -1;
        }
    }

    if (bloquear_sigchld(&mascara_anterior) == -1) {
        free(pipes);
        free(procesos);
        free(estados);
        return -1;
    }

    for (int i = 0; i < cantidad - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Error al crear la pipe");
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            restaurar_mascara_signals(&mascara_anterior);
            free(pipes);
            free(procesos);
            free(estados);
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
            terminar_procesos(procesos, i, pgid);
            restaurar_mascara_signals(&mascara_anterior);
            free(pipes);
            free(procesos);
            free(estados);
            return -1;
        }

        // proceso hijo
        if (procesos[i] == 0) {
            restaurar_signals_hijo();

            // si el comando se ejecuta en segundo plano, establecer el grupo de procesos del hijo
            if (i == 0)
                setpgid(0, 0);
            else
                setpgid(0, pgid);

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
        setpgid(procesos[i], pgid);
    }

    for (int i = 0; i < cantidad - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // si el comando se ejecuta en segundo plano, registrar el job y devolver inmediatamente el control al usuario
    if (background) {
        if (registrar_job(comandos, inicio, fin, pgid, procesos, cantidad,
                          estados) == -1) {
            terminar_procesos(procesos, cantidad, pgid);
            restaurar_mascara_signals(&mascara_anterior);
            free(pipes);
            free(procesos);
            free(estados);
            return -1;
        }
        restaurar_mascara_signals(&mascara_anterior);
        free(pipes);
        free(procesos);
        free(estados);
        return EXIT_SUCCESS;
    }

    if (entregar_terminal(pgid) == -1) {
        terminar_procesos(procesos, cantidad, pgid);
        restaurar_mascara_signals(&mascara_anterior);
        free(pipes);
        free(procesos);
        free(estados);
        return -1;
    }

    int ultimo_estado = EXIT_FAILURE;
    int hay_detenidos = 0;

    // esperar a que todos los procesos hijos terminen y obtener el estado de salida del último proceso
    for (int i = 0; i < cantidad; i++) {
        int status;
        while (waitpid(procesos[i], &status, WUNTRACED) == -1) {
            if (errno != EINTR) {
                recuperar_terminal();
                restaurar_mascara_signals(&mascara_anterior);
                free(pipes);
                free(procesos);
                free(estados);
                return -1;
            }
        }
        if (WIFSTOPPED(status)) {
            estados[i] = PROCESO_DETENIDO;
            hay_detenidos = 1;
        } else {
            estados[i] = PROCESO_TERMINADO;
        }
        if (i == cantidad - 1 && WIFEXITED(status))
            ultimo_estado = WEXITSTATUS(status);
    }

    // restaurar la máscara de señales anterior para permitir que se manejen las señales nuevamente
    recuperar_terminal();
    if (hay_detenidos) {
        if (registrar_job(comandos, inicio, fin, pgid, procesos, cantidad,
                          estados) == -1) {
            terminar_procesos(procesos, cantidad, pgid);
            restaurar_mascara_signals(&mascara_anterior);
            free(pipes);
            free(procesos);
            free(estados);
            return -1;
        }
        ultimo_estado = 128 + SIGTSTP;
    }
    restaurar_mascara_signals(&mascara_anterior);
    free(pipes);
    free(procesos);
    free(estados);
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
        if (comandos[i].arg_count > 0 &&
            strcmp(comandos[i].args[0], "cd") == 0 && fin == i)
            ultimo_estado = ejecutar_comando(&comandos[i]);
        else if (comandos[i].arg_count > 0)
            ultimo_estado = ejecutar_lanzamiento(
                comandos, i, fin, comandos[fin].background);

        if (ultimo_estado == -1)
            return -1;
        i = fin + 1;
    }

    return ultimo_estado;
}