#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <sys/types.h>
#include "shell.h"

int inicializar_background(void);
void notificar_jobs_terminados(void);
void listar_jobs(void);
int reanudar_job_background(int id);
int reanudar_job_foreground(int id);

typedef struct {
    pid_t pid;
    char comando[128];
} ProcesoInfo;

typedef enum {
    PROCESO_EJECUTANDO,
    PROCESO_DETENIDO,
    PROCESO_TERMINADO
} EstadoProceso;

int registrar_job(Comando comandos[], int inicio, int fin, pid_t pgid,
                 pid_t procesos[], int cantidad,
                 const EstadoProceso estados[]);

// Reserva y llena la lista de procesos activos en background.
int obtener_procesos_activos(ProcesoInfo **lista);

#endif

