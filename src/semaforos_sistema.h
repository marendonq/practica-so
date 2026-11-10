#ifndef SEMAFOROS_SISTEMA_H
#define SEMAFOROS_SISTEMA_H

#include "definiciones_globales.h"
#include <semaphore.h>

/* Conjunto de semáforos para una bandeja de entrada */
typedef struct {
    sem_t *mutex;    // Exclusión mutua para modificar la cola
    sem_t *libres;   // Espacios disponibles (inicia en -ie)
    sem_t *ocupados; // Muestras disponibles para procesar (inicia en 0)
} SemaforosEntrada;

/* Conjunto de semáforos para la bandeja de salida */
typedef struct {
    sem_t *mutex;    // Exclusión mutua para la salida
    sem_t *libres;   // Espacios libres en salida (inicia en -oe)
    sem_t *ocupados; // Muestras listas para reporte (inicia en 0)
} SemaforosSalida;

/* Estructura con todos los descriptores de semáforos del sistema */
typedef struct {
    SemaforosEntrada entradas[MAX_ENTRADAS_SISTEMA];
    SemaforosSalida  salida;
    sem_t           *mutex_estado_global;
    sem_t           *solicitud_robot;
    sem_t           *notificacion_stock;
    uint32_t         total_entradas;
    char             nombre_base[LONGITUD_NOMBRE_SHM];
} ManejadorSemaforos;

/* Funciones de ciclo de vida */
int inicializar_semaforos_sistema(ManejadorSemaforos *manejador, const char *nombre_base, uint32_t num_entradas, uint32_t cap_entrada, uint32_t cap_salida);
int abrir_semaforos_existentes(ManejadorSemaforos *manejador, const char *nombre_base, uint32_t num_entradas);
void cerrar_semaforos_sistema(ManejadorSemaforos *manejador);
void destruir_semaforos_sistema(const char *nombre_base, uint32_t num_entradas);

#endif // SEMAFOROS_SISTEMA_H
