#define _GNU_SOURCE
#include "comando_inicializar.h"
#include "definiciones_globales.h"
#include "memoria_compartida.h"
#include "semaforos_sistema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

/* =========================================================================
 * Contexto Interno para Sincronización de Hilos
 * ========================================================================= */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  no_vacia;
    pthread_cond_t  no_llena;
} SincronizadorColaInterna;

typedef struct {
    MemoriaCompartidaSistema *memoria;
    ManejadorSemaforos        semaforos;

    SincronizadorColaInterna  sinc_sangre;
    SincronizadorColaInterna  sinc_detritos;
    SincronizadorColaInterna  sinc_piel;

    pthread_mutex_t           mutex_robot;
    pthread_cond_t            cond_solicitud_robot;
    char                      robot_solicitud_pendiente;
    pthread_cond_t           *cond_notificar_analizador;

    volatile sig_atomic_t     terminar;
} ContextoEvaluador;

static ContextoEvaluador ctx;

static void manejador_senales(int sig) {
    (void)sig;
    ctx.terminar = 1;
    if (ctx.memoria) {
        ctx.memoria->estado.sistema_activo = false;
    }
}

static void sinc_cola_init(SincronizadorColaInterna *sinc) {
    pthread_mutex_init(&sinc->mutex, NULL);
    pthread_cond_init(&sinc->no_vacia, NULL);
    pthread_cond_init(&sinc->no_llena, NULL);
}

static void sinc_cola_destroy(SincronizadorColaInterna *sinc) {
    pthread_mutex_destroy(&sinc->mutex);
    pthread_cond_destroy(&sinc->no_vacia);
    pthread_cond_destroy(&sinc->no_llena);
}

/* Inserción en cola interna compartida */
static void cola_interna_push(ColaInternaCompartida *cq, SincronizadorColaInterna *sinc, MuestraClinica item) {
    pthread_mutex_lock(&sinc->mutex);
    while (cq->cantidad_actual >= cq->capacidad_maxima && !ctx.terminar) {
        pthread_cond_wait(&sinc->no_llena, &sinc->mutex);
    }
    if (ctx.terminar) {
        pthread_mutex_unlock(&sinc->mutex);
        return;
    }

    // Se protege con semáforo global para asegurar consistencia con lectura de ctrl
    sem_wait(ctx.semaforos.mutex_estado_global);
    cq->ranuras[cq->cola] = item;
    cq->cola = (cq->cola + 1) % cq->capacidad_maxima;
    cq->cantidad_actual++;
    sem_post(ctx.semaforos.mutex_estado_global);

    pthread_cond_signal(&sinc->no_vacia);
    pthread_mutex_unlock(&sinc->mutex);
}

/* Extracción con política de envejecimiento (Aging) para Bounded Waiting */
static bool cola_interna_pop(ColaInternaCompartida *cq, SincronizadorColaInterna *sinc, MuestraClinica *item) {
    pthread_mutex_lock(&sinc->mutex);
    while (cq->cantidad_actual == 0 && !ctx.terminar) {
        pthread_cond_wait(&sinc->no_vacia, &sinc->mutex);
    }
    if (ctx.terminar && cq->cantidad_actual == 0) {
        pthread_mutex_unlock(&sinc->mutex);
        return false;
    }

    sem_wait(ctx.semaforos.mutex_estado_global);
    int64_t ahora = (int64_t)time(NULL);
    uint32_t indice_elegido = cq->cabeza;

    // Búsqueda de muestras envejecidas (Aging)
    uint32_t cursor = cq->cabeza;
    for (uint32_t c = 0; c < cq->cantidad_actual; c++) {
        if ((ahora - cq->ranuras[cursor].tiempo_registro) >= UMBRAL_ENVEJECIMIENTO_SEG) {
            indice_elegido = cursor;
            break; // Priorizar la muestra más antigua que excedió el umbral
        }
        cursor = (cursor + 1) % cq->capacidad_maxima;
    }

    *item = cq->ranuras[indice_elegido];

    // Si no fue la cabeza, desplazar elementos para mantener orden FIFO
    if (indice_elegido != cq->cabeza) {
        uint32_t cur = indice_elegido;
        while (cur != cq->cabeza) {
            uint32_t prev = (cur == 0) ? (cq->capacidad_maxima - 1) : (cur - 1);
            cq->ranuras[cur] = cq->ranuras[prev];
            cur = prev;
        }
    }

    cq->cabeza = (cq->cabeza + 1) % cq->capacidad_maxima;
    cq->cantidad_actual--;
    sem_post(ctx.semaforos.mutex_estado_global);

    pthread_cond_signal(&sinc->no_llena);
    pthread_mutex_unlock(&sinc->mutex);
    return true;
}

/* =========================================================================
 * Hilos de Entrada (Dispatchers de Bandejas Compartidas a Colas Internas)
 * ========================================================================= */
typedef struct {
    uint32_t id_cola;
} ContextoHiloEntrada;

static void *hilo_despachador_entrada(void *arg) {
    ContextoHiloEntrada *info = (ContextoHiloEntrada *)arg;
    uint32_t k = info->id_cola;
    free(info);

    while (!ctx.terminar) {
        if (sem_wait(ctx.semaforos.entradas[k].ocupados) == -1) {
            if (ctx.terminar) break;
            continue;
        }

        sem_wait(ctx.semaforos.entradas[k].mutex);
        ColaEntradaCompartida *q = &ctx.memoria->bandejas_entrada[k];
        MuestraClinica muestra = q->ranuras[q->cabeza];
        q->cabeza = (q->cabeza + 1) % q->capacidad_maxima;
        q->cantidad_actual--;
        sem_post(ctx.semaforos.entradas[k].mutex);

        sem_post(ctx.semaforos.entradas[k].libres);

        printf("[Evaluador] Muestra ID #%lu (Tipo %c, Cant %u) tomada de Bandeja de Entrada %u.\n",
               (unsigned long)muestra.id, muestra.tipo, muestra.cantidad, k);
        fflush(stdout);

        if (muestra.tipo == TIPO_SANGRE) {
            cola_interna_push(&ctx.memoria->cola_interna_sangre, &ctx.sinc_sangre, muestra);
        } else if (muestra.tipo == TIPO_DETRITOS) {
            cola_interna_push(&ctx.memoria->cola_interna_detritos, &ctx.sinc_detritos, muestra);
        } else if (muestra.tipo == TIPO_PIEL) {
            cola_interna_push(&ctx.memoria->cola_interna_piel, &ctx.sinc_piel, muestra);
        } else {
            fprintf(stderr, "[Evaluador] Error: tipo desconocido '%c'\n", muestra.tipo);
        }
    }
    return NULL;
}

/* =========================================================================
 * Hilo del Robot de Reactivos
 * ========================================================================= */
static void *hilo_robot_reactivos(void *arg) {
    (void)arg;
    while (!ctx.terminar) {
        pthread_mutex_lock(&ctx.mutex_robot);
        while (ctx.robot_solicitud_pendiente == '\0' && !ctx.terminar) {
            pthread_cond_wait(&ctx.cond_solicitud_robot, &ctx.mutex_robot);
        }
        if (ctx.terminar) {
            pthread_mutex_unlock(&ctx.mutex_robot);
            break;
        }

        char tipo = ctx.robot_solicitud_pendiente;
        pthread_cond_t *cond_analizador = ctx.cond_notificar_analizador;
        pthread_mutex_unlock(&ctx.mutex_robot);

        printf("[Robot] Solicitud: Analizador '%c' requiere reactivo. Dirigiéndose al almacén...\n", tipo);
        fflush(stdout);

        sem_wait(ctx.semaforos.mutex_estado_global);
        ctx.memoria->estado.robot_ocupado = true;
        ctx.memoria->estado.robot_tipo_reactivo = tipo;

        uint32_t *stock_almacen = NULL;
        uint32_t *tanque_analizador = NULL;
        if (tipo == TIPO_SANGRE) {
            stock_almacen = &ctx.memoria->estado.almacen_reactivo_sangre;
            tanque_analizador = &ctx.memoria->estado.analizador_reactivo_sangre;
        } else if (tipo == TIPO_DETRITOS) {
            stock_almacen = &ctx.memoria->estado.almacen_reactivo_detritos;
            tanque_analizador = &ctx.memoria->estado.analizador_reactivo_detritos;
        } else if (tipo == TIPO_PIEL) {
            stock_almacen = &ctx.memoria->estado.almacen_reactivo_piel;
            tanque_analizador = &ctx.memoria->estado.analizador_reactivo_piel;
        }
        sem_post(ctx.semaforos.mutex_estado_global);

        // Esperar en el almacén si no hay reactivo disponible
        while (*stock_almacen == 0 && !ctx.terminar) {
            printf("[Robot] ALERTA: Almacén sin reactivo para '%c'. Esperando recarga vía 'evaluator ctrl update'...\n", tipo);
            fflush(stdout);
            sem_wait(ctx.semaforos.notificacion_stock);
        }
        if (ctx.terminar) break;

        sem_wait(ctx.semaforos.mutex_estado_global);
        uint32_t carga = ctx.memoria->estado.capacidad_robot;
        if (*stock_almacen < carga) {
            carga = *stock_almacen;
        }
        *stock_almacen -= carga;
        ctx.memoria->estado.robot_cantidad_carga = carga;
        sem_post(ctx.semaforos.mutex_estado_global);

        // Tiempo de transporte
        sleep(1);

        // Recargar tanque del analizador
        sem_wait(ctx.semaforos.mutex_estado_global);
        *tanque_analizador += carga;
        ctx.memoria->estado.robot_ocupado = false;
        ctx.memoria->estado.robot_tipo_reactivo = '\0';
        ctx.memoria->estado.robot_cantidad_carga = 0;
        sem_post(ctx.semaforos.mutex_estado_global);

        printf("[Robot] Analizador '%c' recargado (+%u unidades). Nivel actual: %u.\n",
               tipo, carga, *tanque_analizador);
        fflush(stdout);

        pthread_mutex_lock(&ctx.mutex_robot);
        ctx.robot_solicitud_pendiente = '\0';
        ctx.cond_notificar_analizador = NULL;
        if (cond_analizador) {
            pthread_cond_signal(cond_analizador);
        }
        pthread_mutex_unlock(&ctx.mutex_robot);
    }
    return NULL;
}

/* =========================================================================
 * Hilos Analizadores de Muestras (Blood, Detritus, Skin)
 * ========================================================================= */
typedef struct {
    char tipo;
    int tiempo_min;
    int tiempo_max;
    ColaInternaCompartida *cola;
    SincronizadorColaInterna *sinc;
} ConfigAnalizador;

static void *hilo_analizador_muestra(void *arg) {
    ConfigAnalizador *cfg = (ConfigAnalizador *)arg;
    char tipo = cfg->tipo;
    int t_min = cfg->tiempo_min;
    int t_max = cfg->tiempo_max;
    ColaInternaCompartida *cola = cfg->cola;
    SincronizadorColaInterna *sinc = cfg->sinc;
    free(cfg);

    pthread_cond_t cond_mi_recarga;
    pthread_cond_init(&cond_mi_recarga, NULL);

    while (!ctx.terminar) {
        MuestraClinica muestra;
        if (!cola_interna_pop(cola, sinc, &muestra)) {
            if (ctx.terminar) break;
            continue;
        }

        // Verificar reactivo suficiente
        bool necesita_reactivo = false;
        do {
            sem_wait(ctx.semaforos.mutex_estado_global);
            uint32_t *tanque = NULL;
            if (tipo == TIPO_SANGRE) tanque = &ctx.memoria->estado.analizador_reactivo_sangre;
            else if (tipo == TIPO_DETRITOS) tanque = &ctx.memoria->estado.analizador_reactivo_detritos;
            else if (tipo == TIPO_PIEL) tanque = &ctx.memoria->estado.analizador_reactivo_piel;

            if (*tanque < muestra.cantidad) {
                necesita_reactivo = true;
            } else {
                *tanque -= muestra.cantidad;
                necesita_reactivo = false;
            }
            sem_post(ctx.semaforos.mutex_estado_global);

            if (necesita_reactivo) {
                printf("[Analizador %c] Sin reactivo suficiente para Muestra #%lu. Solicitando robot...\n",
                       tipo, (unsigned long)muestra.id);
                fflush(stdout);

                pthread_mutex_lock(&ctx.mutex_robot);
                while (ctx.robot_solicitud_pendiente != '\0' && !ctx.terminar) {
                    pthread_mutex_unlock(&ctx.mutex_robot);
                    usleep(100000);
                    pthread_mutex_lock(&ctx.mutex_robot);
                }
                if (ctx.terminar) {
                    pthread_mutex_unlock(&ctx.mutex_robot);
                    break;
                }
                ctx.robot_solicitud_pendiente = tipo;
                ctx.cond_notificar_analizador = &cond_mi_recarga;
                pthread_cond_signal(&ctx.cond_solicitud_robot);

                // Sección 3: Mientras se carga el reactivo en un analizador, este no puede procesar
                pthread_cond_wait(&cond_mi_recarga, &ctx.mutex_robot);
                pthread_mutex_unlock(&ctx.mutex_robot);
            }
        } while (necesita_reactivo && !ctx.terminar);

        if (ctx.terminar) break;

        // Asignar duración aleatoria Sección 3.2.7
        int duracion = t_min + (rand() % (t_max - t_min + 1));
        muestra.segundos_proceso = duracion;
        muestra.segundos_restante = duracion;
        muestra.estado = ESTADO_PROCESANDO;

        sem_wait(ctx.semaforos.mutex_estado_global);
        if (tipo == TIPO_SANGRE) ctx.memoria->estado.en_proceso_sangre = muestra;
        else if (tipo == TIPO_DETRITOS) ctx.memoria->estado.en_proceso_detritos = muestra;
        else if (tipo == TIPO_PIEL) ctx.memoria->estado.en_proceso_piel = muestra;
        sem_post(ctx.semaforos.mutex_estado_global);

        printf("[Analizador %c] Procesando Muestra #%lu (Duración: %d seg)...\n",
               tipo, (unsigned long)muestra.id, duracion);
        fflush(stdout);

        for (int s = duracion; s > 0 && !ctx.terminar; s--) {
            sleep(1);
            sem_wait(ctx.semaforos.mutex_estado_global);
            if (tipo == TIPO_SANGRE) ctx.memoria->estado.en_proceso_sangre.segundos_restante = s - 1;
            else if (tipo == TIPO_DETRITOS) ctx.memoria->estado.en_proceso_detritos.segundos_restante = s - 1;
            else if (tipo == TIPO_PIEL) ctx.memoria->estado.en_proceso_piel.segundos_restante = s - 1;
            sem_post(ctx.semaforos.mutex_estado_global);
        }

        if (ctx.terminar) break;

        // Cálculo de resultado según Sección 3.2.8
        int r = rand() % 100;
        if (r < 50) {
            strcpy(muestra.resultado, "positivo");
            muestra.resultado_simbolo = 'P';
        } else if (r < 85) {
            strcpy(muestra.resultado, "negative");
            muestra.resultado_simbolo = 'N';
        } else {
            strcpy(muestra.resultado, "-");
            muestra.resultado_simbolo = '?';
        }
        muestra.estado = ESTADO_REPORTADO;
        muestra.segundos_restante = 0;

        sem_wait(ctx.semaforos.mutex_estado_global);
        if (tipo == TIPO_SANGRE) ctx.memoria->estado.en_proceso_sangre.estado = ESTADO_LIBRE;
        else if (tipo == TIPO_DETRITOS) ctx.memoria->estado.en_proceso_detritos.estado = ESTADO_LIBRE;
        else if (tipo == TIPO_PIEL) ctx.memoria->estado.en_proceso_piel.estado = ESTADO_LIBRE;
        ctx.memoria->estado.total_muestras_atendidas++;
        sem_post(ctx.semaforos.mutex_estado_global);

        // Productor a la cola de salida
        sem_wait(ctx.semaforos.salida.libres);
        sem_wait(ctx.semaforos.salida.mutex);

        ColaSalidaCompartida *salida = &ctx.memoria->bandeja_salida;
        salida->ranuras[salida->cola] = muestra;
        salida->cola = (salida->cola + 1) % salida->capacidad_maxima;
        salida->cantidad_actual++;

        sem_post(ctx.semaforos.salida.mutex);
        sem_post(ctx.semaforos.salida.ocupados);

        printf("[Analizador %c] Muestra #%lu terminada con resultado: '%s' [%c]. Enviada a salida.\n",
               tipo, (unsigned long)muestra.id, muestra.resultado, muestra.resultado_simbolo);
        fflush(stdout);
    }

    pthread_cond_destroy(&cond_mi_recarga);
    return NULL;
}

/* =========================================================================
 * Función Principal de "evaluator init"
 * ========================================================================= */
int ejecutar_comando_inicializar(int argc, char *argv[]) {
    uint32_t num_entradas = ENTRADAS_DEFECTO;
    uint32_t cap_entrada  = POSICIONES_ENTRADA_DEFECTO;
    uint32_t cap_salida   = POSICIONES_SALIDA_DEFECTO;
    char     nombre_shm[LONGITUD_NOMBRE_SHM] = NOMBRE_MEMORIA_DEFECTO;
    uint32_t reactivo_b   = REACTIVO_MAXIMO_DEFECTO;
    uint32_t reactivo_d   = REACTIVO_MAXIMO_DEFECTO;
    uint32_t reactivo_s   = REACTIVO_MAXIMO_DEFECTO;
    uint32_t cap_robot    = CAPACIDAD_ROBOT_DEFECTO;
    uint32_t cap_interna  = COLAS_INTERNAS_DEFECTO;

    for (int idx = 1; idx < argc; idx++) {
        if (strcmp(argv[idx], "-i") == 0 && idx + 1 < argc) {
            num_entradas = (uint32_t)atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-ie") == 0 && idx + 1 < argc) {
            cap_entrada = (uint32_t)atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-oe") == 0 && idx + 1 < argc) {
            cap_salida = (uint32_t)atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-n") == 0 && idx + 1 < argc) {
            strncpy(nombre_shm, argv[++idx], sizeof(nombre_shm) - 1);
        } else if (strcmp(argv[idx], "-b") == 0 && idx + 1 < argc) {
            reactivo_b = (uint32_t)atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-d") == 0 && idx + 1 < argc) {
            reactivo_d = (uint32_t)atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-s") == 0 && idx + 1 < argc) {
            reactivo_s = (uint32_t)atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-ee") == 0 && idx + 1 < argc) {
            cap_robot = (uint32_t)atoi(argv[++idx]);
        } else if (strcmp(argv[idx], "-q") == 0 && idx + 1 < argc) {
            cap_interna = (uint32_t)atoi(argv[++idx]);
        }
    }

    if (num_entradas == 0 || num_entradas > MAX_ENTRADAS_SISTEMA) num_entradas = ENTRADAS_DEFECTO;
    if (cap_entrada == 0 || cap_entrada > MAX_SLOTS_ENTRADA) cap_entrada = POSICIONES_ENTRADA_DEFECTO;
    if (cap_salida == 0 || cap_salida > MAX_SLOTS_SALIDA) cap_salida = POSICIONES_SALIDA_DEFECTO;
    if (cap_interna == 0 || cap_interna > MAX_SLOTS_ENTRADA) cap_interna = COLAS_INTERNAS_DEFECTO;

    printf("==================================================================\n");
    printf("   SISTEMA DE EVALUACIÓN DE EXÁMENES MÉDICOS (EVALUATOR INIT)    \n");
    printf("==================================================================\n");
    printf("Configuración:\n");
    printf("  Segmento SHM:             %s\n", nombre_shm);
    printf("  Bandejas de Entrada (-i): %u (Capacidad c/u: %u)\n", num_entradas, cap_entrada);
    printf("  Bandeja de Salida (-oe):  %u\n", cap_salida);
    printf("  Capacidad Colas Internas: %u\n", cap_interna);
    printf("  Capacidad Robot (-ee):    %u\n", cap_robot);
    printf("  Reactivo Sangre (B):      %u\n", reactivo_b);
    printf("  Reactivo Detritos (D):    %u\n", reactivo_d);
    printf("  Reactivo Piel (S):        %u\n", reactivo_s);
    printf("------------------------------------------------------------------\n");

    if (crear_memoria_compartida(nombre_shm, &ctx.memoria) != 0) {
        fprintf(stderr, "Error fatal al crear la memoria compartida '%s'.\n", nombre_shm);
        return 1;
    }

    ctx.memoria->estado.numero_entradas = num_entradas;
    ctx.memoria->estado.capacidad_por_entrada = cap_entrada;
    ctx.memoria->estado.capacidad_salida = cap_salida;
    ctx.memoria->estado.capacidad_cola_interna = cap_interna;
    ctx.memoria->estado.capacidad_robot = cap_robot;
    strncpy(ctx.memoria->estado.nombre_shm, nombre_shm, LONGITUD_NOMBRE_SHM - 1);

    ctx.memoria->estado.almacen_reactivo_sangre = reactivo_b;
    ctx.memoria->estado.almacen_reactivo_detritos = reactivo_d;
    ctx.memoria->estado.almacen_reactivo_piel = reactivo_s;

    ctx.memoria->estado.analizador_reactivo_sangre = reactivo_b;
    ctx.memoria->estado.analizador_reactivo_detritos = reactivo_d;
    ctx.memoria->estado.analizador_reactivo_piel = reactivo_s;

    ctx.memoria->estado.proximo_id_muestra = 1001;
    ctx.memoria->estado.sistema_activo = true;

    for (uint32_t i = 0; i < num_entradas; i++) {
        ctx.memoria->bandejas_entrada[i].capacidad_maxima = cap_entrada;
    }
    ctx.memoria->bandeja_salida.capacidad_maxima = cap_salida;

    ctx.memoria->cola_interna_sangre.capacidad_maxima = cap_interna;
    ctx.memoria->cola_interna_detritos.capacidad_maxima = cap_interna;
    ctx.memoria->cola_interna_piel.capacidad_maxima = cap_interna;

    if (inicializar_semaforos_sistema(&ctx.semaforos, nombre_shm, num_entradas, cap_entrada, cap_salida) != 0) {
        fprintf(stderr, "Error fatal al inicializar semáforos del sistema.\n");
        destruir_memoria_compartida(nombre_shm);
        return 1;
    }

    sinc_cola_init(&ctx.sinc_sangre);
    sinc_cola_init(&ctx.sinc_detritos);
    sinc_cola_init(&ctx.sinc_piel);

    pthread_mutex_init(&ctx.mutex_robot, NULL);
    pthread_cond_init(&ctx.cond_solicitud_robot, NULL);
    ctx.robot_solicitud_pendiente = '\0';

    signal(SIGINT, manejador_senales);
    signal(SIGTERM, manejador_senales);

    srand((unsigned int)time(NULL));

    pthread_t *hilos_entrada = calloc(num_entradas, sizeof(pthread_t));
    for (uint32_t i = 0; i < num_entradas; i++) {
        ContextoHiloEntrada *info = malloc(sizeof(ContextoHiloEntrada));
        info->id_cola = i;
        pthread_create(&hilos_entrada[i], NULL, hilo_despachador_entrada, info);
    }

    pthread_t hilo_robot;
    pthread_create(&hilo_robot, NULL, hilo_robot_reactivos, NULL);

    pthread_t hilo_b, hilo_d, hilo_s;

    ConfigAnalizador *cfg_b = malloc(sizeof(ConfigAnalizador));
    cfg_b->tipo = TIPO_SANGRE; cfg_b->tiempo_min = 1; cfg_b->tiempo_max = 7;
    cfg_b->cola = &ctx.memoria->cola_interna_sangre; cfg_b->sinc = &ctx.sinc_sangre;
    pthread_create(&hilo_b, NULL, hilo_analizador_muestra, cfg_b);

    ConfigAnalizador *cfg_d = malloc(sizeof(ConfigAnalizador));
    cfg_d->tipo = TIPO_DETRITOS; cfg_d->tiempo_min = 5; cfg_d->tiempo_max = 20;
    cfg_d->cola = &ctx.memoria->cola_interna_detritos; cfg_d->sinc = &ctx.sinc_detritos;
    pthread_create(&hilo_d, NULL, hilo_analizador_muestra, cfg_d);

    ConfigAnalizador *cfg_s = malloc(sizeof(ConfigAnalizador));
    cfg_s->tipo = TIPO_PIEL; cfg_s->tiempo_min = 8; cfg_s->tiempo_max = 25;
    cfg_s->cola = &ctx.memoria->cola_interna_piel; cfg_s->sinc = &ctx.sinc_piel;
    pthread_create(&hilo_s, NULL, hilo_analizador_muestra, cfg_s);

    printf("[Evaluador] Sistema iniciado correctamente. Ocupando consola (Presione Ctrl+C para salir)...\n\n");
    fflush(stdout);

    while (!ctx.terminar) {
        sleep(1);
    }

    printf("\n[Evaluador] Recibida señal de apagado. Finalizando tareas y limpiando recursos...\n");
    fflush(stdout);

    pthread_cond_broadcast(&ctx.sinc_sangre.no_vacia);
    pthread_cond_broadcast(&ctx.sinc_sangre.no_llena);
    pthread_cond_broadcast(&ctx.sinc_detritos.no_vacia);
    pthread_cond_broadcast(&ctx.sinc_detritos.no_llena);
    pthread_cond_broadcast(&ctx.sinc_piel.no_vacia);
    pthread_cond_broadcast(&ctx.sinc_piel.no_llena);
    pthread_cond_broadcast(&ctx.cond_solicitud_robot);

    for (uint32_t i = 0; i < num_entradas; i++) {
        sem_post(ctx.semaforos.entradas[i].ocupados);
        pthread_join(hilos_entrada[i], NULL);
    }
    free(hilos_entrada);

    sem_post(ctx.semaforos.notificacion_stock);
    pthread_join(hilo_robot, NULL);
    pthread_join(hilo_b, NULL);
    pthread_join(hilo_d, NULL);
    pthread_join(hilo_s, NULL);

    sinc_cola_destroy(&ctx.sinc_sangre);
    sinc_cola_destroy(&ctx.sinc_detritos);
    sinc_cola_destroy(&ctx.sinc_piel);
    pthread_mutex_destroy(&ctx.mutex_robot);
    pthread_cond_destroy(&ctx.cond_solicitud_robot);

    cerrar_semaforos_sistema(&ctx.semaforos);
    destruir_semaforos_sistema(nombre_shm, num_entradas);
    desconectar_memoria_compartida(ctx.memoria);
    destruir_memoria_compartida(nombre_shm);

    printf("[Evaluador] Recursos IPC destruidos limpiamente. Ejecución terminada.\n");
    return 0;
}
