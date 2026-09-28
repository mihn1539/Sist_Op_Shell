#include <string.h>

#include "parser.h"

static char entrada_normalizada[MAX_INPUT];

static void normalizar_redirecciones(const char *input) {
    size_t destino = 0;

    for (size_t origen = 0; input[origen] != '\0' &&
                              destino + 1 < MAX_INPUT; origen++) {
        if (input[origen] == '<' || input[origen] == '>') {
            if (destino > 0 && entrada_normalizada[destino - 1] != ' ')
                entrada_normalizada[destino++] = ' ';

            entrada_normalizada[destino++] = input[origen];
            if (input[origen] == '>' && input[origen + 1] == '>') {
                if (destino + 1 >= MAX_INPUT)
                    break;
                entrada_normalizada[destino++] = input[++origen];
            }
            if (destino + 1 < MAX_INPUT)
                entrada_normalizada[destino++] = ' ';
        } else {
            entrada_normalizada[destino++] = input[origen];
        }
    }
    entrada_normalizada[destino] = '\0';
}

Operador identificar_operador(const char *pos) {
    if (strncmp(pos, "&&", 2) == 0) {
        return OP_AND;

    } else if (strncmp(pos, "||", 2) == 0) {
        return OP_OR;

    } else if (*pos == '|' && *(pos + 1) != '|') {
        return OP_PIPE;

    } else if (*pos == ';') {
        return OP_SEMICOLON;
    }

    return OP_NONE;
}

int get_largo_operador(Operador op) {
    switch (op) {
        case OP_AND:
        case OP_OR:
            return 2;

        case OP_PIPE:
        case OP_SEMICOLON:
            return 1;

        default:
            return 0;
    }
}

int parsear_comandos(char *input, Comando comandos[]) {
    int cmd_count = 0;
    normalizar_redirecciones(input);
    char *inicio = entrada_normalizada;

    size_t largo = strlen(entrada_normalizada);
    while (largo > 0 && (entrada_normalizada[largo - 1] == ' ' ||
                         entrada_normalizada[largo - 1] == '\t'))
        entrada_normalizada[--largo] = '\0';

    int background = 0;
    if (largo > 0 && entrada_normalizada[largo - 1] == '&') {
        background = 1;
        entrada_normalizada[--largo] = '\0';
        while (largo > 0 && (entrada_normalizada[largo - 1] == ' ' ||
                             entrada_normalizada[largo - 1] == '\t'))
            entrada_normalizada[--largo] = '\0';
    }

    while (*inicio != '\0' && cmd_count < MAX_COMMANDS) {

        char *pos = inicio;
        Operador op = OP_NONE;

        // Buscar el siguiente operador
        while (*pos != '\0') {
            op = identificar_operador(pos);

            if (op != OP_NONE)
                break;

            pos++;
        }

        // Separar el comando del operador
        *pos = '\0';

        comandos[cmd_count].entrada = NULL;
        comandos[cmd_count].salida = NULL;
        comandos[cmd_count].modo_append = 0;
        comandos[cmd_count].background = 0;

        char *token = strtok(inicio, " ");
        int arg_count = 0;

        while (token != NULL && arg_count < MAX_ARGS - 1) {

            // Redireccion de salida
            if (strcmp(token, ">") == 0) {

                token = strtok(NULL, " \t");
                comandos[cmd_count].salida = token;

                token = strtok(NULL, " \t");
                continue;
            }

            // Redireccion de salida append
            else if (strcmp(token, ">>") == 0) {

                token = strtok(NULL, " \t");
                comandos[cmd_count].salida = token;
                comandos[cmd_count].modo_append = 1;

                token = strtok(NULL, " \t");
                continue;
            }

            // Redireccion de entrada
            else if (strcmp(token, "<") == 0) {

                token = strtok(NULL, " \t");
                comandos[cmd_count].entrada = token;

                token = strtok(NULL, " \t");
                continue;
            }

            comandos[cmd_count].args[arg_count++] = token;
            token = strtok(NULL, " \t");
        }

        comandos[cmd_count].args[arg_count] = NULL;
        comandos[cmd_count].arg_count = arg_count;
        comandos[cmd_count].operador = op;

        if (op == OP_NONE)
            comandos[cmd_count].background = background;

        if (arg_count > 0 ||
            comandos[cmd_count].entrada ||
            comandos[cmd_count].salida) {

            cmd_count++;
        }

        if (op == OP_NONE)
            break;

        inicio = pos + get_largo_operador(op);

        while (*inicio == ' ' || *inicio == '\t')
            inicio++;
    }

    return cmd_count;
}