#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <sys/types.h>
#include "shell.h"

#define MAX_JOBS 64

int inicializar_background(void);
void notificar_jobs_terminados(void);
void listar_jobs(void);
int registrar_job(Comando comandos[], int inicio, int fin, pid_t pgid,
                 pid_t procesos[], int cantidad);

typedef struct {
    pid_t pid;
    char comando[128];
} ProcesoInfo;

// Retorna la cantidad de procesos activos en background y llena el arreglo
int obtener_procesos_activos(ProcesoInfo *lista, int max_procesos);

#endif

