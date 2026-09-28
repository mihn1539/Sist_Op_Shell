#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <sys/types.h>

#include "shell.h"

int inicializar_background(void);

void notificar_jobs_terminados(void);

void listar_jobs(void);

int registrar_job(Comando comandos[], int inicio, int fin, pid_t pgid,
                 pid_t procesos[], int cantidad);

#endif