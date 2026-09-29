#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "shell.h"
#include "parser.h"
#include "executor.h"
#include "background.h"
#include "signals.h"
#include "utils.h"
#include "pmon.h"

static int obtener_codigo_salida(char *argumento, int *codigo) {
    char *final;
    long valor;

    if (argumento == NULL) {
        *codigo = EXIT_SUCCESS;
        return 0;
    }

    valor = strtol(argumento, &final, 10);
    if (*argumento == '\0' || *final != '\0' || valor < 0 || valor > 255)
        return -1;

    *codigo = (int)valor;
    return 0;
}

static int obtener_id_job(char *argumento, int *id) {
    char *final;
    long valor;

    if (argumento == NULL) {
        *id = 0;
        return 0;
    }

    errno = 0;
    valor = strtol(argumento, &final, 10);
    if (errno == ERANGE || *argumento == '\0' || *final != '\0' ||
        valor <= 0 || valor > INT32_MAX)
        return -1;

    *id = (int)valor;
    return 0;
}

int main(void) {

    char input[MAX_INPUT];
    Comando *comandos = NULL;

    if (instalar_signals_shell() == -1)
        return EXIT_FAILURE;

    if (inicializar_control_terminal() == -1)
        return EXIT_FAILURE;

    if (inicializar_background() == -1)
        return EXIT_FAILURE;

    while (1) {

        notificar_jobs_terminados();
        imprimir_prompt();
        fflush(stdout);

        if (leer_linea(input, sizeof(input)) == -1)
            break;

        // Entrada vacia
        if (strlen(input) == 0)
            continue;

        // Parsear comandos
        int cmd_count = parsear_comandos(input, &comandos);
        if (cmd_count == -1) {
            perror("No se pudo reservar memoria para los comandos");
            continue;
        }

        if (cmd_count == 1 && comandos[0].arg_count > 0 &&
            comandos[0].operador == OP_NONE &&
            strcmp(comandos[0].args[0], "exit") == 0) {
            int codigo;
            if (obtener_codigo_salida(comandos[0].args[1], &codigo) == -1) {
                fprintf(stderr, "exit: codigo debe estar entre 0 y 255\n");
                free(comandos);
                continue;
            }
            free(comandos);
            return codigo;
        }

        if (cmd_count == 1 && comandos[0].arg_count > 0 &&
            comandos[0].operador == OP_NONE &&
            strcmp(comandos[0].args[0], "jobs") == 0) {
            listar_jobs();
            free(comandos);
            continue;
        }

        if (cmd_count == 1 && comandos[0].arg_count > 0 &&
            comandos[0].operador == OP_NONE &&
            (strcmp(comandos[0].args[0], "fg") == 0 ||
             strcmp(comandos[0].args[0], "bg") == 0)) {
            int id;
            int es_fg = strcmp(comandos[0].args[0], "fg") == 0;
            if (comandos[0].args[2] != NULL ||
                obtener_id_job(comandos[0].args[1], &id) == -1) {
                fprintf(stderr, "%s: id de job invalido\n",
                        es_fg ? "fg" : "bg");
                free(comandos);
                continue;
            }
            if ((es_fg ? reanudar_job_foreground(id)
                       : reanudar_job_background(id)) == -1) {
                free(comandos);
                continue;
            }
            free(comandos);
            continue;
        }

        // ---> Comando pmon
        if (cmd_count == 1 && comandos[0].arg_count > 0 &&
            comandos[0].operador == OP_NONE &&
            strcmp(comandos[0].args[0], "pmon") == 0) {
            int segundos = 2; // Valor por defecto
            if (comandos[0].args[1] != NULL) {
                segundos = atoi(comandos[0].args[1]);
            }
            ejecutar_pmon(segundos);
            free(comandos);
            continue;
        }

        // Ejecutar comandos
        int estado = ejecutar_comandos(comandos, cmd_count);
        free(comandos);
        if (estado == -1)
            return EXIT_FAILURE;
    }

    printf("\n");

    return EXIT_SUCCESS;
}