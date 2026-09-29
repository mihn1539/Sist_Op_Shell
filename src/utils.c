#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "shell.h"
#include "utils.h"

#define MAX_HISTORIAL 100

static char historial[MAX_HISTORIAL][MAX_INPUT];
static size_t historial_cantidad;

static void escribir_bytes(const char *texto, size_t largo) {
    while (largo > 0) {
        ssize_t escritos = write(STDOUT_FILENO, texto, largo);
        if (escritos <= 0)
            return;
        texto += escritos;
        largo -= (size_t)escritos;
    }
}

static void guardar_historial(const char *linea) {
    if (linea[0] == '\0' ||
        (historial_cantidad > 0 &&
         strcmp(historial[historial_cantidad - 1], linea) == 0))
        return;

    if (historial_cantidad == MAX_HISTORIAL) {
        memmove(historial, historial + 1,
                (MAX_HISTORIAL - 1) * sizeof(historial[0]));
        historial_cantidad--;
    }

    strncpy(historial[historial_cantidad], linea, MAX_INPUT - 1);
    historial[historial_cantidad][MAX_INPUT - 1] = '\0';
    historial_cantidad++;
}

static void reemplazar_linea(char *buffer, size_t *largo, size_t *cursor,
                             const char *nueva) {
    while (*cursor > 0) {
        escribir_bytes("\b", 1);
        (*cursor)--;
    }

    strncpy(buffer, nueva, MAX_INPUT - 1);
    buffer[MAX_INPUT - 1] = '\0';
    *largo = strlen(buffer);
    *cursor = *largo;
    escribir_bytes(buffer, *largo);
    escribir_bytes("\033[K", 3);
}

static int leer_linea_terminal(char *buffer, size_t capacidad) {
    struct termios original, modo_raw;
    size_t largo = 0;
    size_t cursor = 0;
    size_t indice_historial = historial_cantidad;

    if (tcgetattr(STDIN_FILENO, &original) == -1)
        return -1;

    modo_raw = original;
    modo_raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    modo_raw.c_cc[VMIN] = 1;
    modo_raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &modo_raw) == -1)
        return -1;

    while (1) {
        char caracter;
        ssize_t leidos = read(STDIN_FILENO, &caracter, 1);

        if (leidos == -1 && errno == EINTR)
            continue;
        if (leidos <= 0) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original);
            return -1;
        }

        if (caracter == '\n' || caracter == '\r') {
            buffer[largo] = '\0';
            escribir_bytes("\n", 1);
            tcsetattr(STDIN_FILENO, TCSANOW, &original);
            guardar_historial(buffer);
            return 0;
        }

        if (caracter == 127 || caracter == '\b') {
            if (cursor > 0) {
                memmove(buffer + cursor - 1, buffer + cursor,
                        largo - cursor + 1);
                largo--;
                cursor--;
                escribir_bytes("\b", 1);
                escribir_bytes(buffer + cursor, largo - cursor);
                escribir_bytes(" ", 1);
                for (size_t posicion = cursor; posicion <= largo; posicion++)
                    escribir_bytes("\b", 1);
            }
            continue;
        }

        if (caracter == '\033') {
            char secuencia[2];
            if (read(STDIN_FILENO, &secuencia[0], 1) != 1 ||
                read(STDIN_FILENO, &secuencia[1], 1) != 1 ||
                secuencia[0] != '[')
                continue;

            if (secuencia[1] == 'A' && indice_historial > 0) {
                indice_historial--;
                reemplazar_linea(buffer, &largo, &cursor,
                                 historial[indice_historial]);
            } else if (secuencia[1] == 'B' &&
                       indice_historial < historial_cantidad) {
                indice_historial++;
                reemplazar_linea(buffer, &largo, &cursor,
                                 indice_historial == historial_cantidad
                                     ? ""
                                     : historial[indice_historial]);
            } else if (secuencia[1] == 'C' && cursor < largo) {
                escribir_bytes("\033[C", 3);
                cursor++;
            } else if (secuencia[1] == 'D' && cursor > 0) {
                escribir_bytes("\033[D", 3);
                cursor--;
            }
            continue;
        }

        if ((unsigned char)caracter < 32 || largo + 1 >= capacidad)
            continue;

        memmove(buffer + cursor + 1, buffer + cursor, largo - cursor + 1);
        buffer[cursor++] = caracter;
        largo++;
        escribir_bytes(buffer + cursor - 1, largo - cursor + 1);
        for (size_t posicion = cursor; posicion < largo; posicion++)
            escribir_bytes("\b", 1);
    }
}

int leer_linea(char *buffer, size_t capacidad) {
    if (!isatty(STDIN_FILENO)) {
        if (fgets(buffer, capacidad, stdin) == NULL)
            return -1;
        buffer[strcspn(buffer, "\n")] = '\0';
        guardar_historial(buffer);
        return 0;
    }

    return leer_linea_terminal(buffer, capacidad);
}

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