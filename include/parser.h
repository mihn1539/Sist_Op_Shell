#ifndef PARSER_H
#define PARSER_H

#include "shell.h"

Operador identificar_operador(const char *pos);

int get_largo_operador(Operador op);

int parsear_comandos(char *input, Comando **comandos);

#endif