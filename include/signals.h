#ifndef SIGNALS_H
#define SIGNALS_H

#include <signal.h>

int instalar_signals_shell(void);

void restaurar_signals_hijo(void);

int bloquear_sigchld(sigset_t *mascara_anterior);

void restaurar_mascara_signals(const sigset_t *mascara_anterior);

#endif