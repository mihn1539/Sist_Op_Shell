#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "background.h"
#include "signals.h"

#define JOB_COMMAND_LENGTH 128

typedef struct Job {
    int id;
    pid_t pgid;
    int remaining;
    int done;
    int cantidad;
    pid_t *pids;
    EstadoProceso *estados;
    int detenidos;
    char command[JOB_COMMAND_LENGTH];
    struct Job *next;
} Job;

static Job *jobs;
static volatile sig_atomic_t siguiente_id = 1;

static void actualizar_estado_job(Job *job, pid_t pid, int status) {
    for (int i = 0; i < job->cantidad; i++) {
        if (job->pids[i] != pid)
            continue;

        if (WIFSTOPPED(status) &&
            job->estados[i] == PROCESO_EJECUTANDO) {
            job->estados[i] = PROCESO_DETENIDO;
            job->detenidos++;
        } else if (WIFCONTINUED(status) &&
                   job->estados[i] == PROCESO_DETENIDO) {
            job->estados[i] = PROCESO_EJECUTANDO;
            job->detenidos--;
        } else if ((WIFEXITED(status) || WIFSIGNALED(status)) &&
                   job->estados[i] != PROCESO_TERMINADO) {
            if (job->estados[i] == PROCESO_DETENIDO)
                job->detenidos--;
            job->estados[i] = PROCESO_TERMINADO;
            job->pids[i] = 0;
            job->remaining--;
            if (job->remaining == 0)
                job->done = 1;
        }
        return;
    }
}

static Job *buscar_job(int id, int solo_activo, int solo_detenido) {
    for (Job *job = jobs; job != NULL; job = job->next) {
        if ((id == 0 || job->id == id) &&
            (!solo_activo || job->remaining > 0) &&
            (!solo_detenido || job->detenidos > 0))
            return job;
    }
    return NULL;
}

// manejador de SIGCHLD para actualizar el estado de los jobs cuando un proceso hijo termina
static void manejar_sigchld(int sig) {
    int saved_errno = errno;
    int status;
    pid_t pid;

    // ignorar el argumento sig para evitar advertencias de compilación
    (void)sig;

    // esperar a que todos los procesos hijos terminen y actualizar el estado de los jobs
    while ((pid = waitpid(-1, &status,
                          WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (Job *job = jobs; job != NULL; job = job->next) {
            if (job->remaining > 0 || job->detenidos > 0) {
                for (int j = 0; j < job->cantidad; j++) {
                    if (job->pids[j] == pid) {
                        actualizar_estado_job(job, pid, status);
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
                 pid_t procesos[], int cantidad,
                 const EstadoProceso estados[]) {
    Job *job = malloc(sizeof(*job));
    if (job == NULL)
        return -1;

    job->pids = malloc((size_t)cantidad * sizeof(*job->pids));
    job->estados = malloc((size_t)cantidad * sizeof(*job->estados));
    if (job->pids == NULL || job->estados == NULL) {
        free(job->pids);
        free(job->estados);
        free(job);
        return -1;
    }

    job->id = siguiente_id++;
    job->pgid = pgid;
    job->remaining = 0;
    job->done = 0;
    job->cantidad = cantidad;
    memcpy(job->pids, procesos, (size_t)cantidad * sizeof(*job->pids));
    memcpy(job->estados, estados,
           (size_t)cantidad * sizeof(*job->estados));
    job->detenidos = 0;
    for (int i = 0; i < cantidad; i++) {
        if (job->estados[i] == PROCESO_DETENIDO)
            job->detenidos++;
        if (job->estados[i] != PROCESO_TERMINADO)
            job->remaining++;
        else
            job->pids[i] = 0;
    }
    if (job->remaining == 0)
        job->done = 1;
    copiar_nombre_job(job, comandos, inicio, fin);
    job->next = jobs;
    jobs = job;
    printf("[%d] %d\n", job->id, (int)pgid);
    return 0;
}

void notificar_jobs_terminados(void) {
    sigset_t mascara;
    sigset_t mascara_anterior;

    sigemptyset(&mascara);
    sigaddset(&mascara, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mascara, &mascara_anterior) == -1)
        return;

    Job **cursor = &jobs;
    while (*cursor != NULL) {
        Job *job = *cursor;
        if (job->done) {
            printf("[%d]+ Done %s\n", job->id, job->command);
            *cursor = job->next;
            free(job->pids);
            free(job->estados);
            free(job);
        } else {
            cursor = &job->next;
        }
    }

    sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
}

void listar_jobs(void) {
    sigset_t mascara;
    sigset_t mascara_anterior;

    sigemptyset(&mascara);
    sigaddset(&mascara, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mascara, &mascara_anterior) == -1)
        return;

    // listar todos los jobs que están en ejecución o que han terminado, mostrando su id, pgid, estado y nombre del job
    for (Job *job = jobs; job != NULL; job = job->next) {
        if (job->remaining > 0 || job->done) {
            const char *estado = job->done ? "Done"
                                  : job->detenidos > 0 ? "Stopped" : "Running";
            printf("[%d] %d %s %s\n", job->id, (int)job->pgid,
                   estado, job->command);
        }
    }

    sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
}

int reanudar_job_background(int id) {
    sigset_t mascara;
    sigset_t mascara_anterior;

    sigemptyset(&mascara);
    sigaddset(&mascara, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mascara, &mascara_anterior) == -1)
        return -1;

    Job *job = buscar_job(id, 1, 1);
    if (job == NULL) {
        fprintf(stderr, "bg: job no detenido\n");
        sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
        return -1;
    }

    if (kill(-job->pgid, SIGCONT) == -1) {
        perror("bg: no se pudo continuar el job");
        sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
        return -1;
    }

    for (int i = 0; i < job->cantidad; i++) {
        if (job->estados[i] == PROCESO_DETENIDO)
            job->estados[i] = PROCESO_EJECUTANDO;
    }
    job->detenidos = 0;
    printf("[%d] %s &\n", job->id, job->command);
    sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
    return 0;
}

int reanudar_job_foreground(int id) {
    sigset_t mascara;
    sigset_t mascara_anterior;

    sigemptyset(&mascara);
    sigaddset(&mascara, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mascara, &mascara_anterior) == -1)
        return -1;

    Job *job = buscar_job(id, 1, 0);
    if (job == NULL) {
        fprintf(stderr, "fg: job no encontrado\n");
        sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
        return -1;
    }

    if (job->detenidos > 0) {
        if (kill(-job->pgid, SIGCONT) == -1) {
            perror("fg: no se pudo continuar el job");
            sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
            return -1;
        }
        for (int i = 0; i < job->cantidad; i++) {
            if (job->estados[i] == PROCESO_DETENIDO)
                job->estados[i] = PROCESO_EJECUTANDO;
        }
        job->detenidos = 0;
    }

    if (entregar_terminal(job->pgid) == -1) {
        perror("fg: no se pudo tomar el terminal");
        sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
        return -1;
    }

    for (int i = 0; i < job->cantidad; i++) {
        if (job->pids[i] == 0)
            continue;

        int status;
        while (waitpid(job->pids[i], &status, WUNTRACED) == -1) {
            if (errno != EINTR) {
                recuperar_terminal();
                sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
                return -1;
            }
        }
        actualizar_estado_job(job, job->pids[i], status);
    }

    recuperar_terminal();
    if (job->done) {
        Job **cursor = &jobs;
        while (*cursor != job)
            cursor = &(*cursor)->next;
        *cursor = job->next;
        free(job->pids);
        free(job->estados);
        free(job);
    }

    sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
    return 0;
}

int obtener_procesos_activos(ProcesoInfo **lista) {
    sigset_t mascara;
    sigset_t mascara_anterior;
    int total = 0;

    *lista = NULL;
    sigemptyset(&mascara);
    sigaddset(&mascara, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mascara, &mascara_anterior) == -1)
        return -1;

    for (Job *job = jobs; job != NULL; job = job->next)
        total += job->remaining;

    if (total > 0) {
        *lista = malloc((size_t)total * sizeof(**lista));
        if (*lista == NULL) {
            sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
            return -1;
        }
    }

    int count = 0;
    for (Job *job = jobs; job != NULL; job = job->next) {
        if (job->remaining > 0) {
            for (int j = 0; j < job->cantidad; j++) {
                if (job->pids[j] > 0) {
                    (*lista)[count].pid = job->pids[j];
                    snprintf((*lista)[count].comando,
                             sizeof((*lista)[count].comando), "%s",
                             job->command);
                    count++;
                }
            }
        }
    }

    sigprocmask(SIG_SETMASK, &mascara_anterior, NULL);
    return count;
}