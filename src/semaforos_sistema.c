#include "semaforos_sistema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

static void construir_nombre(char *destino, size_t max_len, const char *nombre_base, const char *sufijo, int indice) {
    if (indice >= 0) {
        snprintf(destino, max_len, "/%s_%s_%d", nombre_base, sufijo, indice);
    } else {
        snprintf(destino, max_len, "/%s_%s", nombre_base, sufijo);
    }
}

int inicializar_semaforos_sistema(ManejadorSemaforos *manejador, const char *nombre_base, uint32_t num_entradas, uint32_t cap_entrada, uint32_t cap_salida) {
    memset(manejador, 0, sizeof(ManejadorSemaforos));
    strncpy(manejador->nombre_base, nombre_base, LONGITUD_NOMBRE_SHM - 1);
    manejador->total_entradas = num_entradas;

    char nombre_sem[128];

    // Primero destruir si quedaron rezagados de una ejecución anterior
    destruir_semaforos_sistema(nombre_base, num_entradas);

    // Semáforos para cada cola de entrada
    for (uint32_t i = 0; i < num_entradas; i++) {
        construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "in_mtx", (int)i);
        manejador->entradas[i].mutex = sem_open(nombre_sem, O_CREAT | O_EXCL, 0666, 1);
        if (manejador->entradas[i].mutex == SEM_FAILED) {
            perror("Error sem_open in_mtx");
            return -1;
        }

        construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "in_emp", (int)i);
        manejador->entradas[i].libres = sem_open(nombre_sem, O_CREAT | O_EXCL, 0666, cap_entrada);
        if (manejador->entradas[i].libres == SEM_FAILED) {
            perror("Error sem_open in_emp");
            return -1;
        }

        construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "in_ful", (int)i);
        manejador->entradas[i].ocupados = sem_open(nombre_sem, O_CREAT | O_EXCL, 0666, 0);
        if (manejador->entradas[i].ocupados == SEM_FAILED) {
            perror("Error sem_open in_ful");
            return -1;
        }
    }

    // Semáforos para cola de salida
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "out_mtx", -1);
    manejador->salida.mutex = sem_open(nombre_sem, O_CREAT | O_EXCL, 0666, 1);
    if (manejador->salida.mutex == SEM_FAILED) {
        perror("Error sem_open out_mtx");
        return -1;
    }

    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "out_emp", -1);
    manejador->salida.libres = sem_open(nombre_sem, O_CREAT | O_EXCL, 0666, cap_salida);
    if (manejador->salida.libres == SEM_FAILED) {
        perror("Error sem_open out_emp");
        return -1;
    }

    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "out_ful", -1);
    manejador->salida.ocupados = sem_open(nombre_sem, O_CREAT | O_EXCL, 0666, 0);
    if (manejador->salida.ocupados == SEM_FAILED) {
        perror("Error sem_open out_ful");
        return -1;
    }

    // Semáforo de estado global
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "glo_mtx", -1);
    manejador->mutex_estado_global = sem_open(nombre_sem, O_CREAT | O_EXCL, 0666, 1);
    if (manejador->mutex_estado_global == SEM_FAILED) {
        perror("Error sem_open glo_mtx");
        return -1;
    }

    // Semáforo para peticiones al robot
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "rob_req", -1);
    manejador->solicitud_robot = sem_open(nombre_sem, O_CREAT | O_EXCL, 0666, 0);
    if (manejador->solicitud_robot == SEM_FAILED) {
        perror("Error sem_open rob_req");
        return -1;
    }

    // Semáforo para avisar al robot cuando llega stock nuevo
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "stk_not", -1);
    manejador->notificacion_stock = sem_open(nombre_sem, O_CREAT | O_EXCL, 0666, 0);
    if (manejador->notificacion_stock == SEM_FAILED) {
        perror("Error sem_open stk_not");
        return -1;
    }

    return 0;
}

int abrir_semaforos_existentes(ManejadorSemaforos *manejador, const char *nombre_base, uint32_t num_entradas) {
    memset(manejador, 0, sizeof(ManejadorSemaforos));
    strncpy(manejador->nombre_base, nombre_base, LONGITUD_NOMBRE_SHM - 1);
    manejador->total_entradas = num_entradas;

    char nombre_sem[128];

    for (uint32_t i = 0; i < num_entradas; i++) {
        construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "in_mtx", (int)i);
        manejador->entradas[i].mutex = sem_open(nombre_sem, 0);
        if (manejador->entradas[i].mutex == SEM_FAILED) return -1;

        construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "in_emp", (int)i);
        manejador->entradas[i].libres = sem_open(nombre_sem, 0);
        if (manejador->entradas[i].libres == SEM_FAILED) return -1;

        construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "in_ful", (int)i);
        manejador->entradas[i].ocupados = sem_open(nombre_sem, 0);
        if (manejador->entradas[i].ocupados == SEM_FAILED) return -1;
    }

    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "out_mtx", -1);
    manejador->salida.mutex = sem_open(nombre_sem, 0);
    if (manejador->salida.mutex == SEM_FAILED) return -1;

    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "out_emp", -1);
    manejador->salida.libres = sem_open(nombre_sem, 0);
    if (manejador->salida.libres == SEM_FAILED) return -1;

    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "out_ful", -1);
    manejador->salida.ocupados = sem_open(nombre_sem, 0);
    if (manejador->salida.ocupados == SEM_FAILED) return -1;

    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "glo_mtx", -1);
    manejador->mutex_estado_global = sem_open(nombre_sem, 0);
    if (manejador->mutex_estado_global == SEM_FAILED) return -1;

    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "rob_req", -1);
    manejador->solicitud_robot = sem_open(nombre_sem, 0);
    if (manejador->solicitud_robot == SEM_FAILED) return -1;

    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "stk_not", -1);
    manejador->notificacion_stock = sem_open(nombre_sem, 0);
    if (manejador->notificacion_stock == SEM_FAILED) return -1;

    return 0;
}

void cerrar_semaforos_sistema(ManejadorSemaforos *manejador) {
    if (!manejador) return;
    for (uint32_t i = 0; i < manejador->total_entradas; i++) {
        if (manejador->entradas[i].mutex && manejador->entradas[i].mutex != SEM_FAILED)
            sem_close(manejador->entradas[i].mutex);
        if (manejador->entradas[i].libres && manejador->entradas[i].libres != SEM_FAILED)
            sem_close(manejador->entradas[i].libres);
        if (manejador->entradas[i].ocupados && manejador->entradas[i].ocupados != SEM_FAILED)
            sem_close(manejador->entradas[i].ocupados);
    }
    if (manejador->salida.mutex && manejador->salida.mutex != SEM_FAILED)
        sem_close(manejador->salida.mutex);
    if (manejador->salida.libres && manejador->salida.libres != SEM_FAILED)
        sem_close(manejador->salida.libres);
    if (manejador->salida.ocupados && manejador->salida.ocupados != SEM_FAILED)
        sem_close(manejador->salida.ocupados);
    if (manejador->mutex_estado_global && manejador->mutex_estado_global != SEM_FAILED)
        sem_close(manejador->mutex_estado_global);
    if (manejador->solicitud_robot && manejador->solicitud_robot != SEM_FAILED)
        sem_close(manejador->solicitud_robot);
    if (manejador->notificacion_stock && manejador->notificacion_stock != SEM_FAILED)
        sem_close(manejador->notificacion_stock);
}

void destruir_semaforos_sistema(const char *nombre_base, uint32_t num_entradas) {
    char nombre_sem[128];
    for (uint32_t i = 0; i < num_entradas; i++) {
        construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "in_mtx", (int)i);
        sem_unlink(nombre_sem);
        construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "in_emp", (int)i);
        sem_unlink(nombre_sem);
        construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "in_ful", (int)i);
        sem_unlink(nombre_sem);
    }
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "out_mtx", -1);
    sem_unlink(nombre_sem);
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "out_emp", -1);
    sem_unlink(nombre_sem);
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "out_ful", -1);
    sem_unlink(nombre_sem);
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "glo_mtx", -1);
    sem_unlink(nombre_sem);
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "rob_req", -1);
    sem_unlink(nombre_sem);
    construir_nombre(nombre_sem, sizeof(nombre_sem), nombre_base, "stk_not", -1);
    sem_unlink(nombre_sem);
}
