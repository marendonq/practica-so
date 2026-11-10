# 4. Mecanismos para Implementar Cada Comando

Este documento describe el flujo procedimental detallado y las llamadas al sistema requeridas para la implementación de cada comando del ejecutable `evaluator`.

---

## 4.1. Mecanismo de `evaluator init`

```text
[Inicio]
   │
   ├─► 1. Parsear argumentos (getopt / flag package en Go)
   ├─► 2. Validar que no exista ya un segmento shm activo (shm_open con O_CREAT | O_EXCL)
   ├─► 3. Asignar tamaño total con ftruncate(fd, total_size)
   ├─► 4. Mapear en memoria con mmap(PROT_READ | PROT_WRITE, MAP_SHARED)
   ├─► 5. Inicializar estructuras (SystemState, InputQueues, OutputQueue)
   ├─► 6. Crear semáforos POSIX con sem_open(O_CREAT | O_EXCL)
   │        - sem_in_mutex[0..i-1] (val: 1)
   │        - sem_in_empty[0..i-1] (val: ie)
   │        - sem_in_full[0..i-1]  (val: 0)
   │        - sem_out_mutex (val: 1), sem_out_empty (val: oe), sem_out_full (val: 0)
   │        - sem_robot_req (val: 0), sem_robot_mutex (val: 1)
   ├─► 7. Configurar manejador de señales (SIGINT, SIGTERM)
   ├─► 8. Lanzar hilos concurrentes (goroutines):
   │        - i hilos de recepción de entrada
   │        - 3 hilos analizadores (Blood, Detritus, Skin)
   │        - 1 hilo para el Robot de reactivos
   └─► 9. Quedar a la espera de señal de terminación (select{} o signal.Notify)
```

**Mecanismo de Salida Limpia (*Graceful Shutdown*):**
Al recibir `SIGINT` (Ctrl+C):
1. Marcar `system_active = 0` en el estado global.
2. Esperar a que las muestras en proceso en los analizadores culminen.
3. Notificar a los semáforos para desbloquear hilos en espera.
4. Cerrar y desenlazar semáforos (`sem_close`, `sem_unlink`).
5. Desmapear (`munmap`) y desenlazar memoria compartida (`shm_unlink`).

---

## 4.2. Mecanismo de `evaluator reg`

```text
[Inicio reg]
   │
   ├─► 1. Conectarse a memoria compartida existente con shm_open(O_RDWR) + mmap()
   ├─► 2. Abrir semáforos existentes con sem_open()
   ├─► 3. Determinar modo: ¿Lista de archivos o modo interactivo '-'?
   │
   ▼
[Procesamiento de cada muestra]
   │
   ├─► Formato esperado: <entrada> {B|D|S} <cantidad>
   ├─► Validar:
   │     entrada in [0 .. i-1]
   │     tipo in {'B','D','S'}
   │     cantidad in [1 .. 5]
   │
   ├─► Generar ID único atómico (atomic.AddUint64(&shm.SystemState.next_sample_id, 1))
   │
   ├─► Inserción en la bandeja de entrada:
   │     sem_wait(sem_in_empty[entrada])   // Se bloquea si la bandeja está llena
   │     sem_wait(sem_in_mutex[entrada])   // Exclusión mutua
   │       cola[entrada].slots[tail] = nueva_muestra
   │       cola[entrada].tail = (tail + 1) % ie
   │       cola[entrada].count++
   │     sem_post(sem_in_mutex[entrada])
   │     sem_post(sem_in_full[entrada])    // Despierta al despachador
   │
   └─► Reporte del ID:
         - Modo archivo: Escribir en <archivo>.spl
         - Modo '-': Imprimir ID en stdout inmediatamente
```

---

## 4.3. Mecanismo de `evaluator ctrl`

Inicia una sesión interactiva (REPL) leyendo comandos desde `stdin`:

### Subcomando `list [ processing | waiting | reported | reactive | all ]`:
1. Obtiene acceso de solo lectura a la memoria compartida.
2. Adquiere el mutex de la estructura a consultar para garantizar una lectura consistente (evitando lecturas sucias durante modificaciones concurrentes).
3. Formatea la salida conforme a la especificación:
   - `processing`: Muestra exámenes activos en los 3 analizadores con tiempo restante.
   - `waiting`: Itera sobre todas las bandejas de entrada y colas internas listando muestras con estado *esperando*.
   - `reported`: Itera sobre la cola de salida mostrando las muestras terminadas.
   - `reactive`: Muestra el stock del almacén central y los tanques de cada analizador.
4. Libera el mutex.

### Subcomando `update { B | D | S } <integer>`:
1. Valida que el entero sea positivo ($> 0$).
2. Adquiere `sem_robot_mutex`.
3. Incrementa el stock correspondiente en `SystemState`:
   `reagent_stock_x += integer`.
4. Si el robot estaba esperando por reactivo en el almacén, se le envía una señal (`signal/post`) para que reanude las recargas pendientes.
5. Libera `sem_robot_mutex`.

---

## 4.4. Mecanismo de `evaluator rep`

```text
[Inicio rep]
   │
   ├─► 1. Conectarse a la memoria compartida existente y semáforos de salida
   │
   ├─► 2. Si se especificó '-m <cantidad>':
   │     Bucle for j := 0; j < cantidad; j++:
   │       sem_wait(sem_out_full)          // Espera bloqueante hasta que haya un resultado
   │       sem_wait(sem_out_mutex)         // Exclusión mutua de extracción
   │         muestra = output_queue.slots[head]
   │         output_queue.head = (head + 1) % oe
   │         output_queue.count--
   │       sem_post(sem_out_mutex)
   │       sem_post(sem_out_empty)         // Notifica slot libre en salida
   │       Imprimir: [id i k r]
   │
   └─► 3. Si se especificó '-i <segundos>':
         Bucle infinito cada <segundos>:
           Intentar extraer resultados no-bloqueante (sem_trywait o chequeo de count con mutex)
           Para cada resultado disponible: extraer, imprimir y liberar slot.
           Dormir el hilo durante <segundos>.
```

---

## 4.5. Algoritmo de Generación de Resultados y Tiempos de Análisis

Conforme a las secciones 3.2.7 y 3.2.8 del PDF:

### Tiempo de Análisis:
Al iniciar el examen en el analizador correspondiente, se selecciona un tiempo aleatorio uniforme $T$:
- **Sangre (`B`):** $T \in [1, 7]$ segundos.
- **Detritos (`D`):** $T \in [5, 20]$ segundos.
- **Piel (`S`):** $T \in [8, 25]$ segundos.

### Determinación del Resultado:
Se genera un valor aleatorio $R \in [1, 100]$:
- Si $1 \le R \le 50$ (50% de probabilidad): Resultado = `'P'` (positivo).
- Si $51 \le R \le 85$ (35% de probabilidad): Resultado = `'N'` (negativo).
- Si $86 \le R \le 100$ (15% de probabilidad): Resultado = `'?'` (no concluyente / volver a hacer examen).
