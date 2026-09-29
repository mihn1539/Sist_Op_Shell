#ifndef SHELL_H
#define SHELL_H

#define MAX_INPUT 1024
#define MAX_ARGS 64

typedef enum {
    OP_NONE,
    OP_AND,
    OP_OR,
    OP_PIPE,
    OP_SEMICOLON
} Operador;

typedef struct {
    char *args[MAX_ARGS];
    int arg_count;
    Operador operador;

    char *entrada;
    char *salida;
    int modo_append;
    int background;
} Comando;

#endif