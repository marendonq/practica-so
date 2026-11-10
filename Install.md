# Guía de Instalación y Requisitos

## Requisitos del Sistema
- **Sistema Operativo:** Linux (Ubuntu 20.04+, Debian 11+, Arch, Fedora u homologado).
- **Lenguaje:** Go (Golang versión 1.20 o superior).
- **Herramientas de construcción:** `make` y `gcc` (para soporte de llamadas POSIX CGO si se requiere).

## Compilación del Proyecto
Para compilar la aplicación y generar el binario en la carpeta `bin/`:

```bash
cd evaluador
make build
```

## Instalación en un Directorio Específico
Para instalar el ejecutable `evaluator` en una ruta personalizada del sistema (por ejemplo `/usr/local/bin` o `$HOME/.local/bin`):

```bash
make install PREFIX=/usr/local/bin
```

## Limpieza de Binarios y Recursos IPC
Para limpiar los binarios generados:
```bash
make clean
```

Para forzar la eliminación de segmentos de memoria compartida o semáforos huérfanos que hayan quedado tras una salida abrupta:
```bash
make ipc-clean
```
