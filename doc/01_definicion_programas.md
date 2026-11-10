# 1. Definición de Programas, Sintaxis de Comandos y Variables de Ambiente

## 1.1. Arquitectura General de Procesos

El sistema se compone de una herramienta de interfaz única de línea de comandos llamada `evaluator` (wrapper), la cual actúa como punto de entrada para todos los módulos del sistema.

La arquitectura se basa en un **modelo de procesos desacoplados comunicados mediante Memoria Compartida (Shared Memory)** y **Semáforos POSIX**.

```text
                  +-----------------------------------+
                  |      evaluator init (Daemon)      |
                  |  - Crea Memoria Compartida (SHM)  |
                  |  - Inicializa Semáforos           |
                  |  - Ejecuta hilos analizadores/rec |
                  +-----------------+-----------------+
                                    |
            ================== MEMORIA COMPARTIDA ==================
            |  [Bandejas Entrada]  [Colas Salida]  [Reactivos]    |
            ========================================================
                 ^                         ^              ^
                 |                         |              |
      +----------+---------+     +---------+---+    +-----+-------------+
      |   evaluator reg    |     |evaluator rep|    |  evaluator ctrl   |
      | (Procesos cliente) |     |  (Consumo)  |    |  (Monitor/Admin)  |
      +--------------------+     +-------------+    +-------------------+
```

---

## 1.2. Especificación de Comandos y Argumentos

### 1.2.1. `evaluator init`
Inicializa el entorno global de ejecución, crea la memoria compartida y los mecanismos de sincronización, y mantiene la consola ocupada como proceso servidor/backend.

**Sintaxis:**
```bash
evaluator init [-i <integer>] [-ie <integer>] [-oe <integer>] [-n <string>] [-b <integer>] [-d <integer>] [-s <integer>] [-ee <integer>] [-q <integer>]
```

**Parámetros:**
| Opción | Tipo | Valor por defecto | Descripción |
| :--- | :--- | :--- | :--- |
| `-i` | Entero positivo | `5` | Número de bandejas/colas de entrada independientes en el sistema. |
| `-ie` | Entero positivo | `6` | Capacidad máxima (número de slots) por cada cola de entrada. |
| `-oe` | Entero positivo | `10` | Capacidad máxima de la cola de salida para reportes. |
| `-n` | Cadena | `"evaluator"` | Nombre identificador del segmento de memoria compartida y prefijo de semáforos. |
| `-b` | Entero (0-100) | `100` | Nivel inicial de reactivo para muestras de Sangre (*Blood*). |
| `-d` | Entero (0-100) | `100` | Nivel inicial de reactivo para muestras de Detritos (*Detritus*). |
| `-s` | Entero (0-100) | `100` | Nivel inicial de reactivo para muestras de Piel (*Skin*). |
| `-ee` | Entero positivo | `10` | Capacidad de transporte del robot de reactivos. |
| `-q` | Entero positivo | `6` | Tamaño máximo de las colas internas de procesamiento. |

**Comportamiento de Terminación:**
Captura señales de interrupción (`SIGINT`, `SIGTERM`). Antes de salir, espera a que los analizadores terminen la muestra en curso, destruye los semáforos y desenlaza (`shm_unlink`) el segmento de memoria compartida.

---

### 1.2.2. `evaluator reg`
Registra lotes de muestras o muestras individuales para su procesamiento. Puede invocarse simultáneamente desde múltiples terminales.

**Sintaxis:**
```bash
evaluator reg [-n <string>] {{<filename>} ... | - }
```

**Parámetros:**
| Opción | Tipo | Valor por defecto | Descripción |
| :--- | :--- | :--- | :--- |
| `-n` | Cadena | `"evaluator"` | Nombre de la memoria compartida a la que se conectará. |
| `<filename>` | Rutas | N/A | Uno o más archivos de texto con listas de muestras a registrar. |
| `-` | Literal | N/A | Modo interactivo; lee muestras de la entrada estándar (`stdin`) línea por línea hasta `EOF`. |

**Formato de Entrada por Muestra:**
```text
<entrada> <tipo> <cantidad>
```
- `<entrada>`: Entero en el rango `[0, i - 1]`.
- `<tipo>`: Carácter `B` (Blood), `D` (Detritus) o `S` (Skin).
- `<cantidad>`: Entero en el rango `[1, 5]`.

**Salida generada:**
- **Modo archivo:** Para un archivo `pacientes.txt`, genera `pacientes.spl` conteniendo los identificadores asignados y las muestras registradas:
  ```text
  ID: 1001 -> 0 B 3
  ID: 1002 -> 2 S 1
  ```
- **Modo interactivo (`-`):** Tras cada línea válida ingresada, imprime en `stdout` el entero identificador único asignado:
  ```text
  1001
  ```

---

### 1.2.3. `evaluator ctrl`
Consola interactiva de monitoreo, diagnóstico y reabastecimiento en caliente.

**Sintaxis:**
```bash
evaluator ctrl [-s <string>]
```

**Subcomandos disponibles en modo interactivo:**
- `list [ processing | waiting | reported | reactive | all ]`
  - `processing`: Muestra exámenes actualmente en ejecución dentro de los analizadores `[id i k q p]`.
  - `waiting`: Muestra exámenes en cola de espera `[id i k q]`.
  - `reported`: Muestra exámenes en bandeja de salida listos para informe `[id i k r]`.
  - `reactive`: Muestra el nivel actual de reactivos `[B: cant, D: cant, S: cant]`.
  - `all`: Despliega el resumen consolidado de las 4 secciones anteriores.
- `update { B | D | S } <integer>`:
  - Incrementa el almacén central de reactivo en la cantidad entera especificada.

---

### 1.2.4. `evaluator rep`
Extrae y consume las muestras ya analizadas desde la cola de salida, liberando slots para nuevos resultados.

**Sintaxis:**
```bash
evaluator rep [-s <string>] { -i <integer> | -m <integer> }
```

**Parámetros:**
| Opción | Tipo | Descripción |
| :--- | :--- | :--- |
| `-s` | Cadena (default `"evaluator"`) | Nombre de la región de memoria compartida. |
| `-i <segundos>` | Entero positivo | Modo continuo: sondea y extrae resultados periódicamente cada `<segundos>`. |
| `-m <cantidad>` | Entero positivo | Modo por lote: se bloquea hasta recolectar exactamente `<cantidad>` de exámenes terminados. |

**Formato de Salida:**
```text
[id i k r]
```
Ejemplo:
```text
[1001 0 B negative]
[1002 2 S positive]
[1003 1 D -]
```

---

## 1.3. Variables de Ambiente

El sistema soporta configuración por variables de entorno para facilitar despliegues y pruebas automatizadas:

| Variable | Tipo | Default | Descripción |
| :--- | :--- | :--- | :--- |
| `EVALUATOR_SHM_NAME` | String | `"evaluator"` | Nombre base para la memoria compartida y semáforos POSIX. |
| `EVALUATOR_MAX_INPUTS`| Entero | `5` | Número de bandejas de entrada si no se pasa `-i`. |
| `EVALUATOR_DEBUG` | Booleano (`0`/`1`) | `0` | Activa trazas detalladas de depuración en `stderr`. |
| `EVALUATOR_AGING_SEC` | Entero | `30` | Límite máximo de espera antes de aplicar envejecimiento forzado. |

---

## 1.4. Códigos de Salida del Sistema (Exit Codes)

| Código | Significado | Causa típica |
| :--- | :--- | :--- |
| `0` | `EXIT_SUCCESS` | Operación completada con éxito. |
| `1` | `ERR_INVALID_ARGS` | Argumentos de línea de comandos mal formados o incompletos. |
| `2` | `ERR_SHM_NOT_FOUND` | Se intentó ejecutar `reg`, `ctrl` o `rep` sin haber ejecutado `init`. |
| `3` | `ERR_SHM_EXISTS` | `evaluator init` falló porque el segmento ya existe activo. |
| `4` | `ERR_INPUT_FORMAT` | Formato de muestra erróneo en registro. |
| `5` | `ERR_SEM_FAILURE` | Error al inicializar o acceder a primitivas de sincronización. |
