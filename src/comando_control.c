#define _GNU_SOURCE
#include "comando_control.h"
#include "definiciones_globales.h"
#include "memoria_compartida.h"
#include "semaforos_sistema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

static void imprimir_processing(MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos) {
    printf("Processing:\n");
    sem_wait(semaforos->mutex_estado_global);

    MuestraClinica *en_proc[3] = {
        &memoria->estado.en_proceso_sangre,
        &memoria->estado.en_proceso_detritos,
        &memoria->estado.en_proceso_piel
    };

    for (int idx = 0; idx < 3; idx++) {
        if (en_proc[idx]->estado == ESTADO_PROCESANDO) {
            printf("[%lu %u %c %u %d]\n",
                   (unsigned long)en_proc[idx]->id,
                   en_proc[idx]->cola_origen,
                   en_proc[idx]->tipo,
                   en_proc[idx]->cantidad,
                   en_proc[idx]->segundos_restante);
        }
    }

    sem_post(semaforos->mutex_estado_global);
}

static void imprimir_waiting(MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos) {
    printf("Waiting:\n");

    // 1. Revisar todas las bandejas de entrada compartidas
    for (uint32_t k = 0; k < memoria->estado.numero_entradas; k++) {
        sem_wait(semaforos->entradas[k].mutex);
        ColaEntradaCompartida *q = &memoria->bandejas_entrada[k];
        uint32_t idx = q->cabeza;
        for (uint32_t c = 0; c < q->cantidad_actual; c++) {
            MuestraClinica *m = &q->ranuras[idx];
            printf("[%lu %u %c %u]\n",
                   (unsigned long)m->id,
                   m->cola_origen,
                   m->tipo,
                   m->cantidad);
            idx = (idx + 1) % q->capacidad_maxima;
        }
        sem_post(semaforos->entradas[k].mutex);
    }

    // 2. Revisar las colas internas en memoria compartida (Sangre, Detritos, Piel)
    sem_wait(semaforos->mutex_estado_global);
    ColaInternaCompartida *colas_int[3] = {
        &memoria->cola_interna_sangre,
        &memoria->cola_interna_detritos,
        &memoria->cola_interna_piel
    };

    for (int i = 0; i < 3; i++) {
        ColaInternaCompartida *cq = colas_int[i];
        uint32_t idx = cq->cabeza;
        for (uint32_t c = 0; c < cq->cantidad_actual; c++) {
            MuestraClinica *m = &cq->ranuras[idx];
            printf("[%lu %u %c %u]\n",
                   (unsigned long)m->id,
                   m->cola_origen,
                   m->tipo,
                   m->cantidad);
            idx = (idx + 1) % cq->capacidad_maxima;
        }
    }
    sem_post(semaforos->mutex_estado_global);
}

static void imprimir_reported(MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos) {
    printf("Reported:\n");
    sem_wait(semaforos->salida.mutex);
    ColaSalidaCompartida *salida = &memoria->bandeja_salida;
    uint32_t idx = salida->cabeza;
    for (uint32_t c = 0; c < salida->cantidad_actual; c++) {
        MuestraClinica *m = &salida->ranuras[idx];
        printf("[%lu %u %c %s]\n",
               (unsigned long)m->id,
               m->cola_origen,
               m->tipo,
               m->resultado);
        idx = (idx + 1) % salida->capacidad_maxima;
    }
    sem_post(semaforos->salida.mutex);
}

static void imprimir_reactive(MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos) {
    printf("Reactive:\n");
    sem_wait(semaforos->mutex_estado_global);
    printf("  Almacen Central:       [B: %u, D: %u, S: %u]\n",
           memoria->estado.almacen_reactivo_sangre,
           memoria->estado.almacen_reactivo_detritos,
           memoria->estado.almacen_reactivo_piel);
    printf("  Tanques Analizadores:  [B: %u, D: %u, S: %u]\n",
           memoria->estado.analizador_reactivo_sangre,
           memoria->estado.analizador_reactivo_detritos,
           memoria->estado.analizador_reactivo_piel);
    printf("  Robot de Reactivos:    [Ocupado: %s, Tipo: %c, Carga: %u]\n",
           memoria->estado.robot_ocupado ? "SI" : "NO",
           memoria->estado.robot_tipo_reactivo ? memoria->estado.robot_tipo_reactivo : '-',
           memoria->estado.robot_cantidad_carga);
    sem_post(semaforos->mutex_estado_global);
}

static void procesar_comando_list(char *arg, MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos) {
    if (!arg || strlen(arg) == 0 || strcmp(arg, "all") == 0) {
        imprimir_processing(memoria, semaforos);
        imprimir_waiting(memoria, semaforos);
        imprimir_reported(memoria, semaforos);
        imprimir_reactive(memoria, semaforos);
    } else if (strcmp(arg, "processing") == 0) {
        imprimir_processing(memoria, semaforos);
    } else if (strcmp(arg, "waiting") == 0) {
        imprimir_waiting(memoria, semaforos);
    } else if (strcmp(arg, "reported") == 0) {
        imprimir_reported(memoria, semaforos);
    } else if (strcmp(arg, "reactive") == 0) {
        imprimir_reactive(memoria, semaforos);
    } else {
        printf("Opción desconocida para list: '%s'. Opciones válidas: processing, waiting, reported, reactive, all.\n", arg);
    }
}

static void procesar_comando_update(char *tipo_str, char *cant_str, MemoriaCompartidaSistema *memoria, ManejadorSemaforos *semaforos) {
    if (!tipo_str || !cant_str) {
        printf("Uso: update { B | D | S } <integer>\n");
        return;
    }

    char tipo = (char)toupper((unsigned char)tipo_str[0]);
    int cantidad = atoi(cant_str);
    if (cantidad <= 0) {
        printf("Error: La cantidad a añadir debe ser un entero positivo.\n");
        return;
    }

    sem_wait(semaforos->mutex_estado_global);
    if (tipo == 'B') {
        memoria->estado.almacen_reactivo_sangre += (uint32_t)cantidad;
        printf("Almacén Sangre (B) actualizado: +%d (Nuevo total: %u)\n",
               cantidad, memoria->estado.almacen_reactivo_sangre);
    } else if (tipo == 'D') {
        memoria->estado.almacen_reactivo_detritos += (uint32_t)cantidad;
        printf("Almacén Detritos (D) actualizado: +%d (Nuevo total: %u)\n",
               cantidad, memoria->estado.almacen_reactivo_detritos);
    } else if (tipo == 'S') {
        memoria->estado.almacen_reactivo_piel += (uint32_t)cantidad;
        printf("Almacén Piel (S) actualizado: +%d (Nuevo total: %u)\n",
               cantidad, memoria->estado.almacen_reactivo_piel);
    } else {
        printf("Error: Tipo de reactivo desconocido '%c'. Use B, D o S.\n", tipo);
        sem_post(semaforos->mutex_estado_global);
        return;
    }
    sem_post(semaforos->mutex_estado_global);

    // Notificar al robot en caso de que esté esperando en el almacén
    sem_post(semaforos->notificacion_stock);
}

int ejecutar_comando_control(int argc, char *argv[]) {
    char nombre_shm[LONGITUD_NOMBRE_SHM] = NOMBRE_MEMORIA_DEFECTO;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "-n") == 0) && i + 1 < argc) {
            strncpy(nombre_shm, argv[++i], sizeof(nombre_shm) - 1);
        }
    }

    MemoriaCompartidaSistema *memoria = NULL;
    if (conectar_memoria_compartida(nombre_shm, &memoria) != 0) {
        fprintf(stderr, "Error: No se pudo conectar a la memoria compartida '%s'. Asegúrese de que 'evaluator init' esté corriendo.\n", nombre_shm);
        return 2;
    }

    ManejadorSemaforos semaforos;
    if (abrir_semaforos_existentes(&semaforos, nombre_shm, memoria->estado.numero_entradas) != 0) {
        fprintf(stderr, "Error al abrir semáforos del sistema.\n");
        desconectar_memoria_compartida(memoria);
        return 3;
    }

    printf("==================================================================\n");
    printf("   CONSOLA DE CONTROL Y DIAGNÓSTICO (EVALUATOR CTRL)             \n");
    printf("==================================================================\n");
    printf("Comandos disponibles:\n");
    printf("  list [ processing | waiting | reported | reactive | all ]\n");
    printf("  update { B | D | S } <integer>\n");
    printf("Presione Ctrl+D (EOF) para salir.\n");
    printf("------------------------------------------------------------------\n");

    char buffer[256];
    printf("> ");
    fflush(stdout);

    while (fgets(buffer, sizeof(buffer), stdin)) {
        buffer[strcspn(buffer, "\r\n")] = '\0';

        char *token = strtok(buffer, " \t");
        if (!token) {
            printf("> ");
            fflush(stdout);
            continue;
        }

        if (strcmp(token, "list") == 0) {
            char *subopc = strtok(NULL, " \t");
            procesar_comando_list(subopc, memoria, &semaforos);
        } else if (strcmp(token, "update") == 0) {
            char *tipo = strtok(NULL, " \t");
            char *cant = strtok(NULL, " \t");
            procesar_comando_update(tipo, cant, memoria, &semaforos);
        } else if (strcmp(token, "exit") == 0 || strcmp(token, "quit") == 0) {
            break;
        } else {
            printf("Comando desconocido: '%s'. Comandos válidos: list, update.\n", token);
        }

        printf("> ");
        fflush(stdout);
    }

    printf("\nCerrando consola de control.\n");
    cerrar_semaforos_sistema(&semaforos);
    desconectar_memoria_compartida(memoria);
    return 0;
}
