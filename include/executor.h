#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "shell.h"

int ejecutar_comando(Comando *cmd);

int ejecutar_comandos(Comando comandos[], int cmd_count);

#endif