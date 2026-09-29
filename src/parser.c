#include <stdlib.h>
#include <string.h>

#include "parser.h"

static char entrada_normalizada[MAX_INPUT];

// funcion para obtener el proximo token, manejando comillas simples y dobles
static char *siguiente_token(char **cursor) {
    char *inicio = *cursor;
    char *token;
    char *destino;
    char comilla = '\0';

    while (*inicio == ' ' || *inicio == '\t')
        inicio++;

    if (*inicio == '\0') {
        *cursor = inicio;
        return NULL;
    }

    token = inicio;
    destino = inicio;
    while (*inicio != '\0') {
        if (comilla != '\0') {
            if (*inicio == comilla)
                comilla = '\0';
            else
                *destino++ = *inicio;
        } else if (*inicio == '\'' || *inicio == '"') {
            comilla = *inicio;
        } else if (*inicio == ' ' || *inicio == '\t') {
            break;
        } else {
            *destino++ = *inicio;
        }
        inicio++;
    }

    int habia_separador = *inicio != '\0';
    *destino = '\0';
    if (habia_separador)
        inicio++;
    *cursor = inicio;
    return token;
}

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

int parsear_comandos(char *input, Comando **comandos) {
    int cmd_count = 0;
    size_t capacidad = 0;
    Comando *lista = NULL;

    *comandos = NULL;
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

    while (*inicio != '\0') {

        if ((size_t)cmd_count == capacidad) {
            size_t nueva_capacidad = capacidad == 0 ? 4 : capacidad * 2;
            Comando *nueva_lista = realloc(lista,
                                           nueva_capacidad * sizeof(*lista));
            if (nueva_lista == NULL) {
                free(lista);
                return -1;
            }
            lista = nueva_lista;
            capacidad = nueva_capacidad;
        }

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

        lista[cmd_count].entrada = NULL;
        lista[cmd_count].salida = NULL;
        lista[cmd_count].modo_append = 0;
        lista[cmd_count].background = 0;

        char *cursor = inicio;
        char *token = siguiente_token(&cursor);
        int arg_count = 0;

        while (token != NULL && arg_count < MAX_ARGS - 1) {

            // Redireccion de salida
            if (strcmp(token, ">") == 0) {

                token = siguiente_token(&cursor);
                lista[cmd_count].salida = token;

                token = siguiente_token(&cursor);
                continue;
            }

            // Redireccion de salida append
            else if (strcmp(token, ">>") == 0) {

                token = siguiente_token(&cursor);
                lista[cmd_count].salida = token;
                lista[cmd_count].modo_append = 1;

                token = siguiente_token(&cursor);
                continue;
            }

            // Redireccion de entrada
            else if (strcmp(token, "<") == 0) {

                token = siguiente_token(&cursor);
                lista[cmd_count].entrada = token;

                token = siguiente_token(&cursor);
                continue;
            }

            lista[cmd_count].args[arg_count++] = token;
            token = siguiente_token(&cursor);
        }

        lista[cmd_count].args[arg_count] = NULL;
        lista[cmd_count].arg_count = arg_count;
        lista[cmd_count].operador = op;

        if (op == OP_NONE)
            lista[cmd_count].background = background;

        if (arg_count > 0 || lista[cmd_count].entrada ||
            lista[cmd_count].salida) {

            cmd_count++;
        }

        if (op == OP_NONE)
            break;

        inicio = pos + get_largo_operador(op);

        while (*inicio == ' ' || *inicio == '\t')
            inicio++;
    }

    *comandos = lista;
    return cmd_count;
}