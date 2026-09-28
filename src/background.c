#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "background.h"

#define MAX_JOBS 64
#define JOB_COMMAND_LENGTH 128

typedef struct {
    int id;
    pid_t pgid;
    int remaining;
    int done;
    pid_t pids[MAX_COMMANDS];
    char command[JOB_COMMAND_LENGTH];
} Job;

// arreglo de jobs en el que cada elemento representa un job en ejecucion o terminado
static Job jobs[MAX_JOBS];
static volatile sig_atomic_t siguiente_id = 1;

// manejador de SIGCHLD para actualizar el estado de los jobs cuando un proceso hijo termina
static void manejar_sigchld(int sig) {
    int saved_errno = errno;
    int status;
    pid_t pid;

    // ignorar el argumento sig para evitar advertencias de compilación
    (void)sig;

    // esperar a que todos los procesos hijos terminen y actualizar el estado de los jobs
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].remaining > 0) {
                for (int j = 0; j < MAX_COMMANDS; j++) {
                    if (jobs[i].pids[j] == pid) {
                        jobs[i].pids[j] = 0;
                        jobs[i].remaining--;
                        if (jobs[i].remaining == 0)
                            jobs[i].done = 1;
                        break;
                    }
                }
            }
        }
    }
    errno = saved_errno; // restaurar errno para no afectar el flujo del programa
}

// configura la acción de la señal SIGCHLD para que se maneje con el manejador definido
static int configurar_sigchld(void) {
    struct sigaction action;

    memset(&action, 0, sizeof(action)); // inicializar la estructura a cero
    sigemptyset(&action.sa_mask); // no bloquear ninguna señal mientras se ejecuta el manejador
    action.sa_handler = manejar_sigchld;
    action.sa_flags = SA_RESTART;
    return sigaction(SIGCHLD, &action, NULL);
}

int inicializar_background(void) {
    if (configurar_sigchld() == -1) {
        perror("No se pudo instalar SIGCHLD");
        return -1;
    }
    return 0;
}

// copia el nombre del job a partir de los comandos ejecutados para mostrarlo en la lista de jobs
static void copiar_nombre_job(Job *job, Comando comandos[], int inicio,
                              int fin) {
    size_t usado = 0;

    job->command[0] = '\0';

    // copiar los argumentos de los comandos al nombre del job, separando por espacios y agregando '|' entre comandos si hay más de uno
    for (int i = inicio; i <= fin && usado + 1 < sizeof(job->command); i++) {

        // copiar los argumentos del comando actual al nombre del job
        for (int j = 0; comandos[i].args[j] != NULL && usado + 1 < sizeof(job->command); j++) {
            if (usado > 0)
                job->command[usado++] = ' ';
            size_t disponible = sizeof(job->command) - usado - 1;
            size_t largo = strlen(comandos[i].args[j]);
            if (largo > disponible)
                largo = disponible;
            memcpy(job->command + usado, comandos[i].args[j], largo);
            usado += largo;
            job->command[usado] = '\0';
        }
        if (i < fin && usado + 2 < sizeof(job->command)) {
            job->command[usado++] = ' ';
            job->command[usado++] = '|';
            job->command[usado] = '\0';
        }
    }
}

// regista un nuevo job en el arreglo asignando id, pgid, cantidad de procesos y copiando el nombre del job a partir de los comandos ejecutados
int registrar_job(Comando comandos[], int inicio, int fin, pid_t pgid,
                 pid_t procesos[], int cantidad) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].remaining == 0 && !jobs[i].done) {
            jobs[i].id = siguiente_id++;
            jobs[i].pgid = pgid;
            jobs[i].remaining = cantidad;
            jobs[i].done = 0;
            for (int j = 0; j < MAX_COMMANDS; j++)
                jobs[i].pids[j] = 0;
            for (int j = 0; j < cantidad; j++)
                jobs[i].pids[j] = procesos[j];
            copiar_nombre_job(&jobs[i], comandos, inicio, fin);
            printf("[%d] %d\n", jobs[i].id, (int)pgid);
            return 0;
        }
    }
    fprintf(stderr, "No se pueden administrar mas de %d jobs\n", MAX_JOBS);
    return -1;
}

void notificar_jobs_terminados(void) {
    sigset_t mascara;

    sigemptyset(&mascara);
    sigaddset(&mascara, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mascara, NULL);

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].done) {
            printf("[%d]+ Done %s\n", jobs[i].id, jobs[i].command);
            jobs[i].done = 0;
            jobs[i].remaining = 0;
        }
    }

    sigprocmask(SIG_UNBLOCK, &mascara, NULL);
}

void listar_jobs(void) {
    sigset_t mascara;

    sigemptyset(&mascara);
    sigaddset(&mascara, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mascara, NULL);

    // listar todos los jobs que están en ejecución o que han terminado, mostrando su id, pgid, estado y nombre del job
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].remaining > 0 || jobs[i].done) {
            const char *estado = jobs[i].done ? "Done" : "Running";
            printf("[%d] %d %s %s\n", jobs[i].id, (int)jobs[i].pgid,
                   estado, jobs[i].command);
        }
    }

    sigprocmask(SIG_UNBLOCK, &mascara, NULL);
}

int obtener_procesos_activos(ProcesoInfo *lista, int max_procesos) {
    sigset_t mascara;
    sigaddset(&mascara, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mascara, NULL);

    int count = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].remaining > 0) {
            for (int j = 0; j < MAX_COMMANDS; j++) {
                if (jobs[i].pids[j] > 0 && count < max_procesos) {
                    lista[count].pid = jobs[i].pids[j];
                    snprintf(lista[count].comando, sizeof(lista[count].comando), "%s", jobs[i].command);
                    count++;
                }
            }
        }
    }

    sigprocmask(SIG_UNBLOCK, &mascara, NULL);
    return count;
}