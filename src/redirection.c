#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "redirection.h"

int aplicar_redirecciones(const Comando *cmd) {
    int descriptor;

    if (cmd->entrada != NULL) {
        descriptor = open(cmd->entrada, O_RDONLY);
        if (descriptor == -1) {
            perror("Error al abrir el archivo de entrada");
            return -1;
        }
        if (dup2(descriptor, STDIN_FILENO) == -1) {
            perror("Error al conectar el archivo como entrada");
            close(descriptor);
            return -1;
        }
        close(descriptor);
    }

    if (cmd->salida != NULL) {
        int flags = O_WRONLY | O_CREAT |
                    (cmd->modo_append ? O_APPEND : O_TRUNC);
        descriptor = open(cmd->salida, flags, 0644);
        if (descriptor == -1) {
            perror("Error al abrir el archivo de salida");
            return -1;
        }
        if (dup2(descriptor, STDOUT_FILENO) == -1) {
            perror("Error al conectar el archivo como salida");
            close(descriptor);
            return -1;
        }
        close(descriptor);
    }

    return 0;
}