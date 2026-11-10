# Matriz Exhaustiva de Requisitos vs. Implementación

Este documento contiene el desglose minucioso de **todos los requisitos** extraídos textualmente y analíticamente del documento oficial de la práctica (`ST0257-2026-2-031-Practica.pdf`, profesor Juan Francisco Cardona Mc’Cormick), seguido de una **comparación crítica y exhaustiva** contra la implementación en el código.

---

## 1. Desglose Exhaustivo de Requisitos del Documento PDF

### 1.1. Arquitectura y Envoltorio (Sección 1, 2, 3)
- **REQ-ARQ-01 (Modelo General):** Simulación de un sistema paralelo de evaluación de exámenes médicos compuesto por 5 componentes: Registro, Entrada, Evaluador, Salida y Reporte.
- **REQ-ARQ-02 (Envoltorio / Wrapper):** Todos los programas deben ser invocados directamente a través de un envoltorio llamado `evaluador` (o `evaluator`). El wrapper se encarga de invocar los programas reales.
- **REQ-ARQ-03 (Comandos del Sistema):** La interfaz con el usuario final constará de cuatro comandos: `init`, `reg`, `ctrl` y `rep`.
- **REQ-ARQ-04 (Desacoplamiento):** Los diferentes comandos son procesos independientes que se comunican mediante memoria compartida y mecanismos de sincronización (semáforos).

---

### 1.2. Comando `evaluator init` (Secciones 3.1.1, 3.2.1, 3.2.4)
- **REQ-INI-01 (Sinopsis exacta):**
  `evaluator init [-i <integer>] [-ie <integer>] [-oe <integer>] [-n <string>] [-b <integer>] [-d <integer>] [-s <integer>] [-ee <integer>] [-q <integer>]`
- **REQ-INI-02 (Parámetros y valores por defecto):**
  - `-i`: Número de bandejas de entrada (valor positivo, por omisión `5`).
  - `-ie`: Número de posiciones en cada cola de entrada (por omisión `6`).
  - `-oe`: Número de posiciones en la cola de salida (por omisión `10`).
  - `-n`: Nombre del segmento de memoria compartida (por omisión `evaluator`).
  - `-b`: Nivel de reactivo de sangre (Blood), valor máximo por omisión `100`.
  - `-d`: Nivel de reactivo de detritos (Detritus), valor máximo por omisión `100`.
  - `-s`: Nivel de reactivo de piel (Skin), valor máximo por omisión `100`.
  - `-ee`: Capacidad de carga del robot de reactivos (transporte limitado a un único tipo).
  - `-q`: Tamaño de las colas internas (por omisión `6`).
- **REQ-INI-03 (Creación de Recursos IPC):** Debe crear la región de memoria compartida con el tamaño suficiente para almacenar colas y variables de control, e inicializar los valores de configuración.
- **REQ-INI-04 (Ocupación de Consola):** El proceso evaluador iniciado por `init` debe quedarse ocupando la consola donde fue lanzado.
- **REQ-INI-05 (Concurrencia de Hilos de Entrada):** Debe construir un conjunto de $i$ hilos (uno por cada cola de entrada). Cada hilo toma muestras de su cola de entrada compartida y las dirige a la cola interna correspondiente según el tipo de muestra.
- **REQ-INI-06 (Concurrencia de Hilos de Analizadores):** Existe un hilo analizador por cada tipo de muestra (3 hilos: Sangre, Detritos, Piel). Toman las muestras de una cola interna de tamaño `-q`.
- **REQ-INI-07 (Tiempos de Procesamiento - Sección 3.2.7):**
  - Sangre (`B`): valor aleatorio uniforme en el rango $1 \dots 7$ segundos.
  - Detritos (`D`): valor aleatorio uniforme en el rango $5 \dots 20$ segundos.
  - Piel (`S`): valor aleatorio uniforme en el rango $8 \dots 25$ segundos.
- **REQ-INI-08 (Obtención de Resultados - Sección 3.2.8):**
  Distribución probabilística de resultados:
  - Positivo (`p` / `positivo` / `P`): 50%
  - Negativo (`n` / `negative` / `N`): 35%
  - No concluyente (`-` / `?`): 15%
- **REQ-INI-09 (Dinámica de Reactivos y Robot de Recarga):**
  - Cada analizador tiene cantidad limitada de reactivos.
  - Cada muestra requiere una cantidad entera ($1 \dots 5$). Si se agota el reactivo, no se pueden procesar más muestras de ese tipo.
  - Existe un **único robot** que se dirige al almacén de reactivos por el tipo necesitado.
  - El robot solo puede transportar hasta `-ee` unidades de un solo tipo de reactivo a la vez.
  - **Exclusión mutua durante recarga:** Mientras se carga el reactivo en un analizador, este no puede procesar ninguna otra muestra.
- **REQ-INI-10 (Espera Acotada / No Inanición):** Una muestra no puede permanecer en un estado de espera por siempre.

---

### 1.3. Comando `evaluator reg` (Secciones 3.1.2, 3.2.2, 3.2.5)
- **REQ-REG-01 (Sinopsis exacta):**
  `evaluator reg [-n <string>] {{<filename>} ... | - }`
- **REQ-REG-02 (Conexión a Memoria):** Parámetro `-n` indica el nombre de la memoria compartida.
- **REQ-REG-03 (Manejo de Ficheros):**
  - Acepta uno o varios ficheros como argumento.
  - Si un fichero no existe, **se ignora y no se envía ningún mensaje**.
- **REQ-REG-04 (Formato de Muestra):**
  Cada registro tiene el formato estricto: `<integer> {B|D|S} <integer>`
  - Primer entero: bandeja de entrada ($0$ a $i - 1$).
  - Segundo: letra indicando Blood (`B`), Detritus (`D`) o Skin (`S`).
  - Tercer entero: cantidad de muestra ($1 \dots 5$).
- **REQ-REG-05 (Retorno en Modo Archivo):**
  Si las muestras provienen de un fichero, genera un archivo con el mismo nombre y extensión `.spl` (*Samples*) con los identificadores pendientes.
- **REQ-REG-06 (Retorno en Modo Interactivo `-`):**
  Lee de la entrada estándar y, tras ingresar una muestra válida, retorna inmediatamente en `stdout` el número identificador único de la muestra.
- **REQ-REG-07 (Control de Flujo / Búfer Lleno):**
  Si no hay espacio en la cola de entrada especificada, el proceso de registro debe esperar (bloquearse) a que se libere un espacio.
- **REQ-REG-08 (Concurrencia de Clientes):**
  Pueden existir múltiples instancias simultáneas de `reg` registrando muestras al mismo tiempo.

---

### 1.4. Comando `evaluator ctrl` (Secciones 3.1.3, 3.2.5)
- **REQ-CTR-01 (Sinopsis exacta):**
  `evaluator ctrl [-s <string>]`
  (Nota: el flag en el documento es `-s`).
- **REQ-CTR-02 (Modo Interactivo REPL):**
  Inicia una consola interactiva que acepta subcomandos `list` y `update`. Termina al recibir final de fichero (`EOF` / Ctrl+D).
- **REQ-CTR-03 (Subcomando `list` y variantes):**
  `list [ processing | waiting | reported | reactive | all ]`
  - `list processing`:
    Encabezado `Processing:` seguido de líneas con formato:
    `[id i k q p] ...`
    Donde `id`: identificador, `i`: cola de entrada, `k`: tipo de muestra, `q`: cantidad, `p`: tiempo de procesamiento en segundos.
  - `list waiting`:
    Encabezado `Waiting:` seguido de líneas con formato:
    `[id i k q] ...`
  - `list reported`:
    Encabezado `Reported:` seguido de líneas con formato:
    `[id i k r] ...`
    Donde `r` es el informe final (`negative`, `positivo`, `-`).
  - `list reactive`:
    Lista el nivel de reactivo disponible.
  - `list all`:
    Muestra la combinación de todas las opciones anteriores.
- **REQ-CTR-04 (Subcomando `update`):**
  `update { B | D | S } <integer>`
  Actualiza los niveles de reactivo en el almacén central sumando el valor positivo indicado.

---

### 1.5. Comando `evaluator rep` (Secciones 3.1.4, 3.2.5, 3.2.6)
- **REQ-REP-01 (Sinopsis exacta):**
  `evaluator rep [-s <string>] { -i <integer> | -m <integer> }`
- **REQ-REP-02 (Modos de Operación):**
  - `-i <integer>`: Modo interactivo continuo que espera y reporta periódicamente cada $X$ segundos.
  - `-m <integer>`: Modo por lote que extrae hasta un número determinado de exámenes; si no hay suficientes, espera bloqueado hasta completar la cifra.
- **REQ-REP-03 (Formato de Salida de Reporte):**
  Líneas con formato:
  `[id i k r]`
  Donde `id`: identificador, `i`: cola de entrada, `k`: tipo de muestra, `r`: resultado (`N` para negativos, `P` para positivos, `?` para no concluyente según sección 3.2.6).

---

### 1.6. Jerarquía de Directorios y Otros Requisitos (Secciones 3.3, 4, 5)
- **REQ-EST-01 (Estructura de Directorios):**
  ```text
  evaluador/
  |-- Makefile
  |-- Readme.md
  |-- Install.md
  +-- src/
  +-- bin/
  +-- doc/
  +-- examples/
  ```
- **REQ-EST-02 (Reglas de Contenido):**
  - `doc/`: Toda la documentación de diseño en Markdown.
  - `bin/`: Binarios generados (no deben commitearse en Git).
  - `Makefile`: Compilación, instalación y limpieza.
- **REQ-EST-03 (Individual / SO):** Al ser individual, se implementa sobre Linux.

---

## 2. Matriz de Cumplimiento Post-Corrección

| ID Requisito | Estado | Resultado de la Verificación |
| :--- | :---: | :--- |
| **REQ-ARQ-01** | ✅ CUMPLE | 5 componentes funcionales y desacoplados. |
| **REQ-ARQ-02** | ✅ CUMPLE | Binario `evaluator` con alias/symlink `evaluador`, soporta invocación de todos los subcomandos. |
| **REQ-ARQ-03** | ✅ CUMPLE | `init`, `reg`, `ctrl`, `rep` implementados. |
| **REQ-ARQ-04** | ✅ CUMPLE | Comunicación estricta mediante POSIX SHM y Semáforos con nombre. |
| **REQ-INI-01** | ✅ CUMPLE | Parseo exacto de los 9 flags (`-i`, `-ie`, `-oe`, `-n`, `-b`, `-d`, `-s`, `-ee`, `-q`). |
| **REQ-INI-02** | ✅ CUMPLE | Valores por defecto idénticos a los del PDF. |
| **REQ-INI-03** | ✅ CUMPLE | Segmento SHM dimensionado para soportar colas de entrada, salida, colas internas y estado. |
| **REQ-INI-04** | ✅ CUMPLE | Ocupa la consola de lanzamiento hasta recibir SIGINT/SIGTERM. |
| **REQ-INI-05** | ✅ CUMPLE | $i$ hilos consumidores de colas de entrada compartidas. |
| **REQ-INI-06** | ✅ CUMPLE | **Colas internas ubicadas en SHM:** 3 hilos analizadores toman de las colas internas compartidas, haciéndolas 100% visibles para `ctrl list waiting`. |
| **REQ-INI-07** | ✅ CUMPLE | Tiempos exactos: Sangre (1-7s), Detritos (5-20s), Piel (8-25s). |
| **REQ-INI-08** | ✅ CUMPLE | Probabilidades exactas: 50% Positivo, 35% Negativo, 15% No concluyente. |
| **REQ-INI-09** | ✅ CUMPLE | Robot único, capacidad `-ee`, transporte de un solo tipo a la vez, exclusión mutua durante recarga y espera por reabastecimiento en almacén central. |
| **REQ-INI-10** | ✅ CUMPLE | **Política de envejecimiento (*Aging*) implementada:** Muestras que superan el umbral de espera son promovidas para evitar inanición (*Bounded Waiting*). |
| **REQ-REG-01** | ✅ CUMPLE | `evaluator reg [-n <string>] {{<filename>} ... | - }`. |
| **REQ-REG-02** | ✅ CUMPLE | Parámetro `-n` soportado. |
| **REQ-REG-03** | ✅ CUMPLE | Archivos inexistentes ignorados silenciosamente sin mensajes de error. |
| **REQ-REG-04** | ✅ CUMPLE | Validación estricta de `<entrada> {B|D|S} <cantidad>`. |
| **REQ-REG-05** | ✅ CUMPLE | Generación de archivos `.spl` con los IDs asignados. |
| **REQ-REG-06** | ✅ CUMPLE | En modo interactivo (`-`), imprime el ID en stdout tras cada muestra. |
| **REQ-REG-07** | ✅ CUMPLE | `sem_wait(libres)` bloquea si la cola de entrada está llena. |
| **REQ-REG-08** | ✅ CUMPLE | Concurrencia segura entre múltiples clientes con semáforos mutex y contadores. |
| **REQ-CTR-01** | ✅ CUMPLE | Soporta `-s <string>` y `-n <string>`. |
| **REQ-CTR-02** | ✅ CUMPLE | Shell REPL interactivo finaliza con EOF (Ctrl+D) o `exit`. |
| **REQ-CTR-03** | ✅ CUMPLE | `list processing`, `list waiting` (visibilidad total de entradas y colas internas), `list reported`, `list reactive`, `list all` con los formatos exactos del PDF. |
| **REQ-CTR-04** | ✅ CUMPLE | `update { B | D | S } <cant>` incrementa el stock central y notifica al robot. |
| **REQ-REP-01** | ✅ CUMPLE | `evaluator rep [-s <string>] { -i <int> | -m <int> }`. |
| **REQ-REP-02** | ✅ CUMPLE | Modo `-m` bloqueante por cantidad y modo `-i` periódico por tiempo. |
| **REQ-REP-03** | ✅ CUMPLE | Formato de salida estricto `[id i k r]` con `N`, `P`, `?` según sección 3.2.6. |
| **REQ-EST-01** | ✅ CUMPLE | Jerarquía de directorios completa (`src/`, `bin/`, `doc/`, `examples/`, `Makefile`, `Readme.md`, `Install.md`). |
| **REQ-EST-02** | ✅ CUMPLE | Binarios no versionados, documentación en Markdown, Makefile funcional. |
| **REQ-EST-03** | ✅ CUMPLE | Desarrollado para Linux nativo (POSIX). |
