#define _GNU_SOURCE
#include "comando_registro.h"
#include "definiciones_globales.h"
#include "memoria_compartida.h"
#include "semaforos_sistema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

static uint64_t registrar_muestra_en_sistema(MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos,
                                              uint32_t entrada, char tipo, uint8_t cantidad) {
    if (entrada >= memoria->estado.numero_entradas) {
        return 0; // Cola inválida
    }
    tipo = (char)toupper((unsigned char)tipo);
    if (tipo != 'B' && tipo != 'D' && tipo != 'S') {
        return 0; // Tipo inválido
    }
    if (cantidad < 1 || cantidad > 5) {
        return 0; // Cantidad fuera de rango
    }

    // 1. Esperar slot libre en la bandeja de entrada especificada (Productor)
    if (sem_wait(semaforos->entradas[entrada].libres) == -1) {
        return 0;
    }

    // 2. Exclusión mutua para insertar
    sem_wait(semaforos->entradas[entrada].mutex);

    // Obtener ID único atómico mediante exclusión mutua global
    sem_wait(semaforos->mutex_estado_global);
    uint64_t id_asignado = memoria->estado.proximo_id_muestra++;
    sem_post(semaforos->mutex_estado_global);

    ColaEntradaCompartida *q = &memoria->bandejas_entrada[entrada];
    MuestraClinica *slot = &q->ranuras[q->cola];
    slot->id = id_asignado;
    slot->cola_origen = entrada;
    slot->tipo = tipo;
    slot->cantidad = cantidad;
    slot->estado = ESTADO_ESPERANDO;
    slot->resultado[0] = '\0';
    slot->resultado_simbolo = '?';
    slot->tiempo_registro = (int64_t)time(NULL);
    slot->segundos_proceso = 0;
    slot->segundos_restante = 0;

    q->cola = (q->cola + 1) % q->capacidad_maxima;
    q->cantidad_actual++;

    sem_post(semaforos->entradas[entrada].mutex);

    // 3. Notificar al consumidor (despachador) que hay una muestra disponible
    sem_post(semaforos->entradas[entrada].ocupados);

    return id_asignado;
}

static void cambiar_extension_a_spl(const char *origen, char *destino, size_t max_len) {
    strncpy(destino, origen, max_len - 1);
    char *punto = strrchr(destino, '.');
    if (punto != NULL) {
        *punto = '\0';
    }
    strncat(destino, ".spl", max_len - strlen(destino) - 1);
}

static void procesar_archivo_muestras(const char *ruta_archivo, MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos) {
    FILE *f_in = fopen(ruta_archivo, "r");
    if (!f_in) {
        // La especificación dice: si el fichero no existe se ignora y no se envía ningún mensaje.
        return;
    }

    char ruta_spl[512];
    cambiar_extension_a_spl(ruta_archivo, ruta_spl, sizeof(ruta_spl));

    FILE *f_spl = fopen(ruta_spl, "w");
    if (!f_spl) {
        fclose(f_in);
        return;
    }

    char linea[256];
    while (fgets(linea, sizeof(linea), f_in)) {
        // Formato esperado: <integer> {B|D|S} <integer>
        int entrada_int = 0;
        char tipo_char = '\0';
        int cantidad_int = 0;

        if (sscanf(linea, "%d %c %d", &entrada_int, &tipo_char, &cantidad_int) == 3) {
            if (entrada_int >= 0 && cantidad_int >= 1 && cantidad_int <= 5) {
                uint64_t id = registrar_muestra_en_sistema(memoria, semaforos, (uint32_t)entrada_int, tipo_char, (uint8_t)cantidad_int);
                if (id > 0) {
                    fprintf(f_spl, "%lu\n", (unsigned long)id);
                }
            }
        }
    }

    fclose(f_in);
    fclose(f_spl);
}

static void procesar_modo_interactivo(MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos) {
    char linea[256];
    while (fgets(linea, sizeof(linea), stdin)) {
        int entrada_int = 0;
        char tipo_char = '\0';
        int cantidad_int = 0;

        if (sscanf(linea, "%d %c %d", &entrada_int, &tipo_char, &cantidad_int) == 3) {
            if (entrada_int >= 0 && cantidad_int >= 1 && cantidad_int <= 5) {
                uint64_t id = registrar_muestra_en_sistema(memoria, semaforos, (uint32_t)entrada_int, tipo_char, (uint8_t)cantidad_int);
                if (id > 0) {
                    // La especificación dice: después de un examen se retorna el número de la muestra
                    printf("%lu\n", (unsigned long)id);
                    fflush(stdout);
                }
            }
        }
    }
}

int ejecutar_comando_registro(int argc, char *argv[]) {
    char nombre_shm[LONGITUD_NOMBRE_SHM] = NOMBRE_MEMORIA_DEFECTO;
    int inicio_ficheros = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            strncpy(nombre_shm, argv[++i], sizeof(nombre_shm) - 1);
            inicio_ficheros = i + 1;
        }
    }

    if (inicio_ficheros >= argc) {
        fprintf(stderr, "Uso: evaluator reg [-n <string>] {{<filename>} ... | - }\n");
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

    for (int i = inicio_ficheros; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            procesar_modo_interactivo(memoria, &semaforos);
        } else {
            procesar_archivo_muestras(argv[i], memoria, &semaforos);
        }
    }

    cerrar_semaforos_sistema(&semaforos);
    desconectar_memoria_compartida(memoria);
    return 0;
}
