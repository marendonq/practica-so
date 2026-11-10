# Simulación de un Sistema de Evaluación de Exámenes Médicos

**Materia:** Sistemas Operativos (ST0257)  
**Profesor:** Juan Francisco Cardona Mc’Cormick  
**Universidad:** Universidad EAFIT  

## Integrantes
- Trabajo Individual: marendonq

---

## Descripción del Proyecto
Este proyecto simula un sistema paralelo de evaluación de exámenes médicos que modela problemas fundamentales de sistemas operativos:
- **Comunicación Entre Procesos (IPC):** Memoria compartida (`shm_open`, `mmap`).
- **Sincronización:** Primitivas de semáforos POSIX (`sem_open`, `sem_wait`, `sem_post`) bajo el esquema Productor-Consumidor sobre Búfer Acotado (*Bounded Buffer*).
- **Concurrencia Multihilo:** Despacho paralelo, hilos de análisis independientes y control del recurso crítico de recarga (Robot de Reactivos).
- **Prevención de Bloqueos Mutuos (*Deadlocks*) y Espera Acotada (*Bounded Waiting*).**

---

## Documentación de Diseño (Primera Entrega)
Toda la documentación técnica exigida para la Primera Entrega se encuentra en la carpeta `doc/`:

1. [01_definicion_programas.md](doc/01_definicion_programas.md): Programas, comandos, argumentos y variables de ambiente.
2. [02_bandeja_entrada_y_salida.md](doc/02_bandeja_entrada_y_salida.md): Estructuras de datos y layout de memoria compartida.
3. [03_concurrencia_y_sincronizacion.md](doc/03_concurrencia_y_sincronizacion.md): Semáforos, productor-consumidor y robot de reactivos.
4. [04_mecanismos_comandos.md](doc/04_mecanismos_comandos.md): Flujos de ejecución paso a paso de cada comando.

---

## Estructura del Repositorio
```text
evaluador/
|-- Makefile           # Automatización de compilación y limpieza
|-- Readme.md          # Información general del proyecto
|-- Install.md         # Instrucciones de instalación y dependencias
+-- src/               # Código fuente del sistema (Go)
+-- bin/               # Directorio donde se compilan los binarios
+-- doc/               # Documentación de diseño en Markdown
+-- examples/          # Ficheros de prueba para registro de muestras
```
