# 2. Estructura de la Bandeja de Entrada, Bandeja de Salida y Memoria Compartida

Este documento describe la representación en bajo nivel de las estructuras de datos que residen en el segmento de Memoria Compartida (`shm`), asegurando compatibilidad con llamadas al sistema POSIX (`shm_open`, `mmap`) y tipos de longitud fija.

---

## 2.1. Estructura de Muestra / Examen (`Sample`)

Cada examen dentro del sistema se representa mediante un registro de tamaño fijo:

```c
typedef struct {
    uint64_t id;             // Identificador único correlativo
    uint32_t input_queue;    // Índice de la bandeja de entrada origen [0 .. i-1]
    char     sample_type;    // 'B' (Blood), 'D' (Detritus), 'S' (Skin)
    uint8_t  quantity;       // Cantidad de muestra requerida [1 .. 5]
    uint8_t  status;         // 0: Esperando, 1: En proceso, 2: Reportado
    char     result;         // Resultado: 'N' (negativo), 'P' (positivo), '?' (no concluyente), '-' (pendiente)
    int64_t  timestamp_reg;  // Timestamp Unix (ms) de entrada (para control de espera acotada/aging)
    int64_t  process_time;   // Tiempo de procesamiento asignado (en segundos)
} Sample;
```

**Representación en Go:**
```go
type Sample struct {
    ID           uint64
    InputQueue   uint32
    SampleType   byte
    Quantity     uint8
    Status       uint8
    Result       byte
    TimestampReg int64
    ProcessTime  int64
}
```

---

## 2.2. Bandeja de Entrada: Modelo de Búfer Circular Acotado

La bandeja de entrada no es una cola única sino un **arreglo de $i$ colas independientes**. Cada cola funciona como un **Búfer Circular Acotado (Bounded Buffer)**:

```text
Cola de Entrada [k]:
Capacidad: ie elementos
                      head (lectura/consumidor)
                       |
     +---+---+---+---+---+---+
     | S1| S2| S3|   |   |   |
     +---+---+---+---+---+---+
                   |
                  tail (escritura/productor)
```

### Estructura de una Cola de Entrada Individual:
```c
typedef struct {
    uint32_t head;                     // Puntero de extracción (consumidor: dispatcher del evaluador)
    uint32_t tail;                     // Puntero de inserción (productores: procesos reg)
    uint32_t count;                    // Elementos actualmente en cola
    uint32_t capacity;                 // Capacidad máxima (ie, por defecto 6)
    Sample   slots[MAX_QUEUE_CAPACITY];// Arreglo continuo de muestras
} InputQueue;
```

---

## 2.3. Bandeja de Salida: Búfer de Resultados

La bandeja de salida es una cola única compartida de tamaño $oe$ (por omisión 10) donde los analizadores colocan las muestras procesadas y el comando `evaluator rep` las extrae.

### Estructura de la Cola de Salida:
```c
typedef struct {
    uint32_t head;                     // Puntero de extracción (consumidor: proceso rep)
    uint32_t tail;                     // Puntero de inserción (productores: hilos analizadores)
    uint32_t count;                    // Número actual de resultados listos
    uint32_t capacity;                 // Capacidad máxima (oe, por defecto 10)
    Sample   slots[MAX_OUTPUT_CAPACITY];
} OutputQueue;
```

---

## 2.4. Almacén Central de Reactivos y Estado del Sistema

Para el robot de recarga y el monitoreo por `evaluator ctrl`, se incluye un bloque de control de recursos:

```c
typedef struct {
    // Almacén central de reactivos
    uint32_t reagent_stock_b;    // Stock disponible de reactivo Sangre
    uint32_t reagent_stock_d;    // Stock disponible de reactivo Detritos
    uint32_t reagent_stock_s;    // Stock disponible de reactivo Piel
    
    // Reactivo actual en los analizadores
    uint32_t analyzer_b_reagent;
    uint32_t analyzer_d_reagent;
    uint32_t analyzer_s_reagent;

    // Estado del robot
    uint8_t  robot_busy;         // 1 si el robot está transportando/recargando
    char     robot_carrying_type;// 'B', 'D', 'S' o '\0'
    uint32_t robot_carrying_qty; // Cantidad transportada (hasta -ee)

    // Métricas del sistema
    uint64_t next_sample_id;     // Generador de ID correlativo
    uint32_t total_processed;    // Muestras finalizadas
    uint8_t  system_active;      // 1: activo, 0: en proceso de shutdown
} SystemState;
```

---

## 2.5. Layout Global de la Memoria Compartida (`SharedMemoryLayout`)

Toda la memoria compartida se mapea como una única estructura contigua para evitar punteros desreferenciados entre espacios de memoria virtual independientes:

```text
+-------------------------------------------------------------+
| SystemState (Metadatos globales, Reactivos, Contadores)     |
+-------------------------------------------------------------+
| InputQueue[0]  (Buffer circular bandeja 0)                  |
| InputQueue[1]  (Buffer circular bandeja 1)                  |
| ...                                                         |
| InputQueue[i-1](Buffer circular bandeja i-1)                |
+-------------------------------------------------------------+
| OutputQueue    (Buffer circular de salida para rep)         |
+-------------------------------------------------------------+
```

### Cálculo del Tamaño Total de Memoria Compartida
$$\text{Tamaño Total} = \text{sizeof}(\text{SystemState}) + i \times \text{sizeof}(\text{InputQueue}) + \text{sizeof}(\text{OutputQueue})$$

Por ejemplo, para valores por omisión ($i=5, ie=6, oe=10$):
- $\text{sizeof}(\text{Sample}) \approx 40 \text{ bytes}$
- $\text{InputQueue} \approx 16 + 6 \times 40 = 256 \text{ bytes}$
- $\text{OutputQueue} \approx 16 + 10 \times 40 = 416 \text{ bytes}$
- $\text{SystemState} \approx 128 \text{ bytes}$
- **Total:** $\approx 128 + (5 \times 256) + 416 \approx 1824 \text{ bytes}$ (redondeado a 1 página de memoria = 4096 bytes con `sysconf(_SC_PAGE_SIZE)`).
