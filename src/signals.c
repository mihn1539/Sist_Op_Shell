#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "signals.h"

static pid_t grupo_shell;
static int terminal_shell = -1;

// configura la acción de una señal específica con un manejador y banderas dadas
static int configurar_accion(int signo, void (*handler)(int), int flags) {
    struct sigaction action;

    memset(&action, 0, sizeof(action)); // inicializar la estructura a cero
    sigemptyset(&action.sa_mask); // no bloquear ninguna señal mientras se ejecuta el manejador
    action.sa_handler = handler; // asignar el manejador de la señal
    action.sa_flags = flags; // asignar las banderas de la señal
    return sigaction(signo, &action, NULL); // configurar la acción de la señal y devolver el resultado
}

// instala las señales de la shell para ignorar SIGINT y SIGQUIT, permitiendo que la shell maneje estas señales en lugar de terminarlas
int instalar_signals_shell(void) {
    if (configurar_accion(SIGINT, SIG_IGN, SA_RESTART) == -1 ||
        configurar_accion(SIGQUIT, SIG_IGN, SA_RESTART) == -1 ||
        configurar_accion(SIGTSTP, SIG_IGN, SA_RESTART) == -1 ||
        configurar_accion(SIGTTIN, SIG_IGN, SA_RESTART) == -1 ||
        configurar_accion(SIGTTOU, SIG_IGN, SA_RESTART) == -1) {
        perror("No se pudieron instalar las signals de la shell");
        return -1;
    }
    return 0;
}

int inicializar_control_terminal(void) {
    if (!isatty(STDIN_FILENO))
        return 0;

    grupo_shell = getpgrp();
    terminal_shell = STDIN_FILENO;
    if (tcsetpgrp(terminal_shell, grupo_shell) == -1) {
        perror("No se pudo tomar control del terminal");
        terminal_shell = -1;
        return -1;
    }
    return 0;
}

int entregar_terminal(pid_t grupo) {
    if (terminal_shell == -1)
        return 0;
    return tcsetpgrp(terminal_shell, grupo);
}

int recuperar_terminal(void) {
    if (terminal_shell == -1)
        return 0;
    return tcsetpgrp(terminal_shell, grupo_shell);
}

// restaura las señales de un proceso hijo para funcionar de manera default
void restaurar_signals_hijo(void) {
    configurar_accion(SIGINT, SIG_DFL, 0);
    configurar_accion(SIGQUIT, SIG_DFL, 0);
    configurar_accion(SIGTSTP, SIG_DFL, 0);
    configurar_accion(SIGTTIN, SIG_DFL, 0);
    configurar_accion(SIGTTOU, SIG_DFL, 0);
}

// bloquea la señal SIGCHLD para evitar que el manejador de la señal se ejecute mientras se realizan operaciones críticas
int bloquear_sigchld(sigset_t *mascara_anterior) {
    sigset_t mascara;

    sigemptyset(&mascara);
    sigaddset(&mascara, SIGCHLD);
    return sigprocmask(SIG_BLOCK, &mascara, mascara_anterior);
}

// restaura la máscara de señales anterior, permitiendo que las señales bloqueadas previamente se manejen nuevamente
void restaurar_mascara_signals(const sigset_t *mascara_anterior) {
    sigprocmask(SIG_SETMASK, mascara_anterior, NULL);
}