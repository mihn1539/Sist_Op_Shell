#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_COMMANDS 32

// definir los operadores para la ejecucion de multiples comandos
typedef enum {
    OP_NONE,
    OP_AND,
    OP_OR,
    OP_PIPE,
    OP_SEMICOLON
} Operador;

typedef struct {
    char *args[MAX_ARGS];
    int arg_count;
    Operador operador;
    char *entrada;      
    char *salida;       
    int modo_append;    
} Comando;

Operador identificar_operador(const char *pos) {
    if (strncmp(pos, "&&", 2) == 0) {
        return OP_AND;
    } else if (strncmp(pos, "||", 2) == 0) {
        return OP_OR;
    } else if (*pos == '|' && *(pos + 1) != '|') {
        return OP_PIPE;
    } else if (*pos == ';') {
        return OP_SEMICOLON;
    }
    return OP_NONE;
}

int get_largo_operador(Operador op) {
    switch (op) {
        case OP_AND:
        case OP_OR:
            return 2;
        case OP_PIPE:
        case OP_SEMICOLON:
            return 1;
        default:
            return 0;
    }
}

// funcion para parsear los comandos de entrada y almacenarlos en un arreglo
int parsear_comandos(char *input, Comando comandos[]) {
    int cmd_count = 0;
    char *inicio = input;

    while (*inicio != '\0' && cmd_count < MAX_COMMANDS) {
        char *pos = inicio;
        Operador op = OP_NONE;

        // buscar el siguiente operador en la entrada
        while (*pos != '\0') {
            op = identificar_operador(pos);
            if (op != OP_NONE) break;
            pos++;
        }

        // si se encuentra un operador, reemplazarlo con '\0' para separar el comando
        *pos = '\0';

        comandos[cmd_count].entrada = NULL;
        comandos[cmd_count].salida = NULL;
        comandos[cmd_count].modo_append = 0;

        char *token = strtok(inicio, " ");
        int arg_count = 0;

        // almacenar los args del comando
        while (token != NULL && arg_count < MAX_ARGS - 1) {
            
            // Detección redirecciones entrada/salida.
            if(strcmp(token, ">") == 0){
                token = strtok(NULL, " \t");
                comandos[cmd_count].salida = token;
                token = strtok(NULL, " \t");
                continue;
            }else if(strcmp(token, ">>") == 0){
                token = strtok(NULL, " \t");
                comandos[cmd_count].salida = token;
                comandos[cmd_count].modo_append = 1;
                token = strtok(NULL, " \t");
                continue;
            }else if(strcmp(token, "<") == 0){
                token = strtok(NULL, " \t");
                comandos[cmd_count].entrada = token;
                token = strtok(NULL, " \t");
                continue;
            }

            comandos[cmd_count].args[arg_count++] = token;
            token = strtok(NULL, " \t");
        }

        comandos[cmd_count].args[arg_count] = NULL;
        comandos[cmd_count].arg_count = arg_count;
        comandos[cmd_count].operador = op;

        if (arg_count > 0 || comandos[cmd_count].entrada || comandos[cmd_count].salida) {
            cmd_count++; 
        }

        if (op == OP_NONE) break;

        // este bloque logra que los comandos se parseen correctamente, haya o no haya espacios en blanco entre el operador y el comando
        inicio = pos + get_largo_operador(op);
        while (*inicio == ' ' || *inicio == '\t') inicio++;
    }

    return cmd_count;
}

// funcion para ejecutar un comando individual
int ejecutar_comando(Comando *cmd) {
    if (cmd->args[0] == NULL) return 0;

    // manejar el comando "cd"
    if (strcmp(cmd->args[0], "cd") == 0) {
        const char *path;

        // si el argumento es nulo o "~", se cambia al directorio HOME
        if (cmd->args[1] == NULL || strcmp(cmd->args[1], "~") == 0) {
            path = getenv("HOME");

            if (path == NULL) {
                fprintf(stderr, "\e[31mcd: HOME no definido\e[0m\n");
                return EXIT_FAILURE;
            }
        } else {
            path = cmd->args[1];
        }

        if (chdir(path) < 0) {
            perror("\e[31mcd fallido\e[0m");
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    // crear un proceso hijo para ejecutar el comando
    pid_t pid = fork();

    if (pid < 0) {
        perror("\e[31mfork fallido\e[0m");
        return -1;
    }

    // el proceso hijo ejecuta el comando solicitado
    if (pid == 0) {
        
        // Revisión direcciones entrada/salida.
        if(cmd->entrada != NULL){
            int archivo_entrada = open(cmd->entrada, O_RDONLY);
            if (archivo_entrada == -1){
                printf("Error al intentar operar el archivo de entrada.\n");
                exit(EXIT_FAILURE);
            }
            if(dup2(archivo_entrada, 0) == -1){
                printf("Error al intentar conectar el archivo como entrada.\n");
                exit(EXIT_FAILURE);
            }
            if (close(archivo_entrada) == -1) {
                perror("Advertencia: No se pudo cerrar el descriptor de archivo");
            }
        }

        if(cmd->salida != NULL && cmd->modo_append == 0){
            int archivo_salida = open(cmd->salida, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (archivo_salida == -1){
                printf("Error al abrir el archivo de salida.\n");
                exit(EXIT_FAILURE);
            }
            if(dup2(archivo_salida, 1) == -1){
                printf("Error al intentar conectar el archivo como salida.\n"); 
                exit(EXIT_FAILURE);
            }
            if (close(archivo_salida) == -1) {
                perror("Advertencia: No se pudo cerrar el descriptor de archivo");
            }
        } else if(cmd->salida != NULL && cmd->modo_append != 0){
            int archivo_salida = open(cmd->salida, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (archivo_salida == -1){
                printf("Error al abrir el archivo de salida.\n");
                exit(EXIT_FAILURE);
            }
            if(dup2(archivo_salida, 1) == -1){
                printf("Error al intentar conectar el archivo como salida.\n");
                exit(EXIT_FAILURE);
            }
            if (close(archivo_salida) == -1) {
                perror("Advertencia: No se pudo cerrar el descriptor de archivo");
            }
        }

        execvp(cmd->args[0], cmd->args);
        perror("\e[31mexec fallido\e[0m");
        exit(EXIT_FAILURE);
    }

    // el proceso padre espera a que el hijo termine y obtiene su estado de salida
    int status;
    waitpid(pid, &status, 0);
    
    // devuelve el código de salida del proceso hijo si terminó normalmente
    if (WIFEXITED(status)) return WEXITSTATUS(status);

    return -1;
}

// funcion para ejecutar una lista de comandos (funciona de a pares si hay mas de dos comandos)
void ejecutar_comandos(Comando comandos[], int cmd_count) {
    int ultimo_exit_code = EXIT_SUCCESS;

    for (int i = 0; i < cmd_count; i++) {
        if (i > 0) {
            Operador last_op = comandos[i - 1].operador;

            // salta el comando si el ultimo comando antes del && fallo
            if (last_op == OP_AND && ultimo_exit_code != EXIT_SUCCESS) continue;

            // salta el comando si el ultimo comando antes del || tuvo exito
            if (last_op == OP_OR && ultimo_exit_code == EXIT_SUCCESS) continue;
        }

        ultimo_exit_code = ejecutar_comando(&comandos[i]);
    }
}

// funcion para imprimir el prompt de la terminal con el directorio actual
void imprimir_prompt() {
    char cwd[1024];
    char *home = getenv("HOME");

    // imprimir ~ en vez del path completo de HOME
    if (home != NULL && getcwd(cwd, sizeof(cwd)) != NULL) {
        if (strncmp(cwd, home, strlen(home)) == 0) {
            printf("\e[32mshell\e[0m:\e[34m~%s\e[0m$ ", cwd + strlen(home));
            return; 
        } else {
            printf("\e[32mshell\e[0m:\e[34m%s\e[0m$ ", cwd);
            return;
        }
    }
}


// manejador de señales para Ctrl+C
void signal_handler(int sig) {
    if (sig == SIGINT) {
        printf("\n");
        imprimir_prompt();
        fflush(stdout);
    }
}

int main() {
    char input[MAX_INPUT];
    Comando comandos[MAX_COMMANDS];

    signal(SIGINT, signal_handler); // registrar el manejador de señales para Ctrl+C

    while (1) {
        imprimir_prompt();
        fflush(stdout); // obliga al sistema a escribir de inmediato el prompt

        if (fgets(input, sizeof(input), stdin) == NULL) break;

        input[strcspn(input, "\n")] = 0; // elimina el salto de linea

        // si la entrada es vacía,
        // avanza a la siguiente iteración imprimiendo el prompt nuevamente
        if (strlen(input) == 0) continue;

        if (strcmp(input, "exit") == 0) break; // salir del bucle y terminar el programa

        // almacena los comandos parseados en un arreglo junto a la cantidad de comandos
        int cmd_count = parsear_comandos(input, comandos);

        ejecutar_comandos(comandos, cmd_count);
    }

    printf("\n");
    return EXIT_SUCCESS;
}