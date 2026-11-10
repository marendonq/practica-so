#ifndef DEFINICIONES_GLOBALES_H
#define DEFINICIONES_GLOBALES_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <semaphore.h>

/* =========================================================================
 * Constantes Generales y Valores por Defecto (según especificación PDF)
 * ========================================================================= */
#define NOMBRE_MEMORIA_DEFECTO    "evaluator"
#define ENTRADAS_DEFECTO           5      // Parámetro -i
#define POSICIONES_ENTRADA_DEFECTO 6      // Parámetro -ie
#define POSICIONES_SALIDA_DEFECTO  10     // Parámetro -oe
#define REACTIVO_MAXIMO_DEFECTO    100    // Parámetros -b, -d, -s
#define CAPACIDAD_ROBOT_DEFECTO    10     // Parámetro -ee
#define COLAS_INTERNAS_DEFECTO     6      // Parámetro -q
#define UMBRAL_ENVEJECIMIENTO_SEG  15     // Bounded waiting / Aging

#define MAX_ENTRADAS_SISTEMA       32     // Límite máximo configurable de entradas
#define MAX_SLOTS_ENTRADA          128    // Límite máximo de capacidad por cola
#define MAX_SLOTS_SALIDA           256    // Límite máximo de capacidad en salida
#define LONGITUD_NOMBRE_SHM        64

/* Estados de una muestra */
typedef enum {
    ESTADO_LIBRE = 0,
    ESTADO_ESPERANDO,        // En cola de entrada o cola interna
    ESTADO_PROCESANDO,       // En ejecución dentro de un analizador
    ESTADO_REPORTADO         // En cola de salida, esperando por rep
} EstadoMuestra;

/* Tipo de muestra */
typedef enum {
    TIPO_SANGRE   = 'B',     // Blood
    TIPO_DETRITOS = 'D',     // Detritus
    TIPO_PIEL     = 'S'      // Skin
} TipoMuestra;

/* =========================================================================
 * Estructura de Muestra Clínica (Sample)
 * ========================================================================= */
typedef struct {
    uint64_t id;               // Identificador único correlativo
    uint32_t cola_origen;      // Número de bandeja de entrada de donde provino [0..i-1]
    char     tipo;             // 'B', 'D', 'S'
    uint8_t  cantidad;         // Cantidad requerida de muestra/reactivo (1..5)
    uint8_t  estado;           // EstadoMuestra
    char     resultado[16];    // Formato largo (Sección 3.1.3): "negative", "positivo", "-"
    char     resultado_simbolo;// Formato corto (Sección 3.2.6): 'N', 'P', '?'
    int64_t  tiempo_registro;  // Timestamp de entrada para política de envejecimiento (aging)
    int      segundos_proceso; // Tiempo total de procesamiento asignado
    int      segundos_restante;// Tiempo restante en segundos (para list processing)
} MuestraClinica;

/* =========================================================================
 * Estructuras de Colas de Memoria Compartida (Búfer Acotado)
 * ========================================================================= */
typedef struct {
    uint32_t cabeza;                      // Puntero de extracción (consumidor)
    uint32_t cola;                        // Puntero de inserción (productor)
    uint32_t cantidad_actual;             // Número de muestras actualmente en cola
    uint32_t capacidad_maxima;            // Tamaño de la cola (-ie)
    MuestraClinica ranuras[MAX_SLOTS_ENTRADA];
} ColaEntradaCompartida;

typedef struct {
    uint32_t cabeza;
    uint32_t cola;
    uint32_t cantidad_actual;
    uint32_t capacidad_maxima;            // Tamaño (-q)
    MuestraClinica ranuras[MAX_SLOTS_ENTRADA];
} ColaInternaCompartida;

typedef struct {
    uint32_t cabeza;                      // Puntero de extracción (consumidor rep)
    uint32_t cola;                        // Puntero de inserción (analizadores)
    uint32_t cantidad_actual;             // Número de resultados listos
    uint32_t capacidad_maxima;            // Capacidad de la bandeja de salida (-oe)
    MuestraClinica ranuras[MAX_SLOTS_SALIDA];
} ColaSalidaCompartida;

/* =========================================================================
 * Estado Global y Almacén Central de Reactivos
 * ========================================================================= */
typedef struct {
    // Parámetros de configuración del sistema
    uint32_t numero_entradas;             // -i
    uint32_t capacidad_por_entrada;       // -ie
    uint32_t capacidad_salida;            // -oe
    uint32_t capacidad_cola_interna;      // -q
    uint32_t capacidad_robot;             // -ee
    char     nombre_shm[LONGITUD_NOMBRE_SHM];

    // Almacén Central de Reactivos (incrementado con evaluator ctrl update)
    uint32_t almacen_reactivo_sangre;     // Stock central Sangre (B)
    uint32_t almacen_reactivo_detritos;   // Stock central Detritos (D)
    uint32_t almacen_reactivo_piel;       // Stock central Piel (S)

    // Reactivos disponibles en cada analizador (-b, -d, -s)
    uint32_t analizador_reactivo_sangre;  // Nivel actual en analizador B
    uint32_t analizador_reactivo_detritos;// Nivel actual en analizador D
    uint32_t analizador_reactivo_piel;    // Nivel actual en analizador S

    // Estado del Robot de Transporte
    bool     robot_ocupado;               // true si está en movimiento/recargando
    char     robot_tipo_reactivo;         // 'B', 'D', 'S' o '\0'
    uint32_t robot_cantidad_carga;        // Cantidad transportada en el viaje

    // Muestras actualmente en proceso en los analizadores
    MuestraClinica en_proceso_sangre;
    MuestraClinica en_proceso_detritos;
    MuestraClinica en_proceso_piel;

    // Métricas del sistema
    uint64_t proximo_id_muestra;          // Generador atómico de identificadores únicos
    uint32_t total_muestras_atendidas;
    volatile bool sistema_activo;         // 1 = corriendo, 0 = señal de apagado recibida
} EstadoGlobalSistema;

/* =========================================================================
 * Estructura Raíz de la Memoria Compartida
 * ========================================================================= */
typedef struct {
    EstadoGlobalSistema   estado;
    ColaSalidaCompartida  bandeja_salida;
    ColaEntradaCompartida bandejas_entrada[MAX_ENTRADAS_SISTEMA];
    ColaInternaCompartida cola_interna_sangre;   // Visible para ctrl list waiting
    ColaInternaCompartida cola_interna_detritos; // Visible para ctrl list waiting
    ColaInternaCompartida cola_interna_piel;     // Visible para ctrl list waiting
} MemoriaCompartidaSistema;

#endif // DEFINICIONES_GLOBALES_H
