#define _POSIX_C_SOURCE 200809L  // Habilitamos las funciones POSIX avanzadas como clock_gettime y sigaction
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "pmon.h"
#include "background.h"
#include "signals.h"

#define MAX_PROC_CACHE 64  // Maximo numero de procesos que se guardan en cache para medir el consumo de la CPU

/*
Estructura para almacenar la lectura previa de un proceso
Permite calcular la diferencia de tiempo de CPU y tiempo real entre refrescos
*/
typedef struct {
    pid_t pid;              // PID del proceso monitoreado
    unsigned long ticks;    // Ticks totales de CPU acumulados (utime + stime)
    struct timespec time;   // Marca de tiempo absoluta de la lectura previa
} ProcCpuCache;

// Banderas globales atomicas y volatiles modificadas por los manejadores de señales
static volatile sig_atomic_t alrm_recibida = 0; // Se activa cuando salta la alarma de refresco (SIGALRM)
static volatile sig_atomic_t stop_pmon = 0;    // Se activa cuando el usuario presiona Ctrl + C (SIGINT)

/*
Manejador para la señal SIGALARM
Notifica al bucle principal de pmon que se cumplio el intervalo y debe redibujar
*/
static void manejar_sigalrm(int sig) {
    (void)sig;
    alrm_recibida = 1;
}

static void manejar_sigint_pmon(int sig) {
    (void)sig;
    stop_pmon = 1;
}

static const char *obtener_estado_str(char state) {
    switch (state) {
        case 'R': return "ejecutando";
        case 'S': return "durmiendo";
        case 'Z': return "zombie";
        case 'T': return "detenido";
        default:  return "desconocido";
    }
}

void ejecutar_pmon(int segundos) {
    if (segundos <= 0) segundos = 2;

    ProcCpuCache cache[MAX_PROC_CACHE];
    int num_cache = 0;

    struct sigaction sa_alrm, sa_int, sa_int_old;
    memset(&sa_alrm, 0, sizeof(sa_alrm));
    sa_alrm.sa_handler = manejar_sigalrm;
    sigaction(SIGALRM, &sa_alrm, NULL);

    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = manejar_sigint_pmon;
    sigaction(SIGINT, &sa_int, &sa_int_old);

    alrm_recibida = 1; // Para dibujar la primera iteracion inmediatamente
    stop_pmon = 0;

    long clk_tck = sysconf(_SC_CLK_TCK);

    while (!stop_pmon) {
        if (alrm_recibida) {
            alrm_recibida = 0;

            // Limpiar la pantalla
            printf("\033[H\033[J");

            ProcesoInfo procs[MAX_JOBS * MAX_COMMANDS];
            int total_procs = obtener_procesos_activos(procs, MAX_JOBS * MAX_COMMANDS);

            printf("%-8s | %-15s | %-10s | %-12s | %-8s\n",
                   "PID", "COMANDO", "ESTADO", "%CPU (aprox)", "RSS (KB)");
            printf("---------------------------------------------------------------\n");

            ProcCpuCache nuevos_cache[MAX_PROC_CACHE];
            int nuevos_num_cache = 0;

            struct timespec ahora;
            clock_gettime(CLOCK_MONOTONIC, &ahora);

            for (int i = 0; i < total_procs; i++) {
                pid_t pid = procs[i].pid;
                char stat_path[64], status_path[64];
                snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
                snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);

                FILE *fstat = fopen(stat_path, "r");
                if (!fstat) continue; // Si el proceso termino justo entre lecturas

                char buf[1024];
                char estado_char = '?';
                unsigned long utime = 0, stime = 0;

                if (fgets(buf, sizeof(buf), fstat)) {
                    // El campo comm esta entre parentesis (p. ej. (sleep)), se busca el ultimo ')'
                    char *last_paren = strrchr(buf, ')');
                    if (last_paren) {
                        sscanf(last_paren + 2, "%c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
                               &estado_char, &utime, &stime);
                    }
                }
                fclose(fstat);

                FILE *fstatus = fopen(status_path, "r");
                long rss_kb = 0;
                if (fstatus) {
                    char line[256];
                    while (fgets(line, sizeof(line), fstatus)) {
                        if (strncmp(line, "VmRSS:", 6) == 0) {
                            sscanf(line + 6, "%ld", &rss_kb);
                            break;
                        }
                    }
                    fclose(fstatus);
                }

                unsigned long total_ticks = utime + stime;
                double cpu_pct = 0.0;

                // Calcular %CPU comparando deltas con la lectura anterior
                for (int c = 0; c < num_cache; c++) {
                    if (cache[c].pid == pid) {
                        double delta_tiempo = (ahora.tv_sec - cache[c].time.tv_sec) +
                                             (ahora.tv_nsec - cache[c].time.tv_nsec) / 1e9;
                        double delta_cpu = (double)(total_ticks - cache[c].ticks) / (double)clk_tck;
                        if (delta_tiempo > 0) {
                            cpu_pct = (delta_cpu / delta_tiempo) * 100.0;
                            if (cpu_pct < 0.0) cpu_pct = 0.0;
                        }
                        break;
                    }
                }

                if (nuevos_num_cache < MAX_PROC_CACHE) {
                    nuevos_cache[nuevos_num_cache].pid = pid;
                    nuevos_cache[nuevos_num_cache].ticks = total_ticks;
                    nuevos_cache[nuevos_num_cache].time = ahora;
                    nuevos_num_cache++;
                }

                printf("%-8d | %-15.15s | %-10s | %-12.1f | %-8ld\n",
                       pid, procs[i].comando, obtener_estado_str(estado_char),
                       cpu_pct, rss_kb);
            }

            memcpy(cache, nuevos_cache, sizeof(ProcCpuCache) * nuevos_num_cache);
            num_cache = nuevos_num_cache;
            fflush(stdout);

            alarm(segundos);
        }

        pause(); // Esperar hasta recibir SIGALRM o SIGINT
    }

    alarm(0);                  // Desactivar la alarma al salir
    instalar_signals_shell();  // Restablecer el comportamiento de SIGINT/SIGQUIT para la shell
    printf("\n");
}