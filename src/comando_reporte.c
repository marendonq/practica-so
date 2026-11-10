#define _GNU_SOURCE
#include "comando_reporte.h"
#include "definiciones_globales.h"
#include "memoria_compartida.h"
#include "semaforos_sistema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

static volatile sig_atomic_t terminar_reporte = 0;

static void manejador_senales_rep(int sig) {
    (void)sig;
    terminar_reporte = 1;
}

static void extraer_e_imprimir_muestra(MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos) {
    sem_wait(semaforos->salida.mutex);
    ColaSalidaCompartida *salida = &memoria->bandeja_salida;
    MuestraClinica muestra = salida->ranuras[salida->cabeza];
    salida->cabeza = (salida->cabeza + 1) % salida->capacidad_maxima;
    salida->cantidad_actual--;
    sem_post(semaforos->salida.mutex);

    sem_post(semaforos->salida.libres);

    // Formato de salida exigido en la Sección 3.2.6: [id i k r]
    // donde r es: N para valores negativos, P para valores positivos y ?
    printf("[%lu %u %c %c]\n",
           (unsigned long)muestra.id,
           muestra.cola_origen,
           muestra.tipo,
           muestra.resultado_simbolo);
    fflush(stdout);
}

int ejecutar_comando_reporte(int argc, char *argv[]) {
    char nombre_shm[LONGITUD_NOMBRE_SHM] = NOMBRE_MEMORIA_DEFECTO;
    int modo_tiempo = -1;
    int modo_cantidad = -1;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "-n") == 0) && i + 1 < argc) {
            strncpy(nombre_shm, argv[++i], sizeof(nombre_shm) - 1);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            modo_tiempo = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            modo_cantidad = atoi(argv[++i]);
        }
    }

    if (modo_tiempo < 0 && modo_cantidad < 0) {
        fprintf(stderr, "Uso: evaluator rep [-s <string>] { -i <integer> | -m <integer> }\n");
        return 1;
    }

    MemoriaCompartidaSistema *memoria = NULL;
    if (conectar_memoria_compartida(nombre_shm, &memoria) != 0) {
        fprintf(stderr, "Error: No se pudo conectar a la memoria compartida '%s'. Asegúrese de que 'evaluator init' esté corriendo.\n", nombre_shm);
        return 2;
    }

    ManejadorSemaforos semaforos;
    if (abrir_semaforos_existentes(&semaforos, nombre_shm, memoria->estado.numero_entradas) != 0) {
        fprintf(stderr, "Error al abrir los semáforos del sistema.\n");
        desconectar_memoria_compartida(memoria);
        return 3;
    }

    signal(SIGINT, manejador_senales_rep);
    signal(SIGTERM, manejador_senales_rep);

    if (modo_cantidad > 0) {
        for (int c = 0; c < modo_cantidad && !terminar_reporte; c++) {
            if (sem_wait(semaforos.salida.ocupados) == -1) {
                if (terminar_reporte) break;
                continue;
            }
            extraer_e_imprimir_muestra(memoria, &semaforos);
        }
    } else if (modo_tiempo > 0) {
        while (!terminar_reporte) {
            while (sem_trywait(semaforos.salida.ocupados) == 0) {
                extraer_e_imprimir_muestra(memoria, &semaforos);
            }
            sleep((unsigned int)modo_tiempo);
        }
    }

    cerrar_semaforos_sistema(&semaforos);
    desconectar_memoria_compartida(memoria);
    return 0;
}
