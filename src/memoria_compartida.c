#include "memoria_compartida.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

static void formatear_nombre_shm(char *destino, size_t max_len, const char *nombre_base) {
    if (nombre_base[0] == '/') {
        snprintf(destino, max_len, "%s", nombre_base);
    } else {
        snprintf(destino, max_len, "/%s", nombre_base);
    }
}

int crear_memoria_compartida(const char *nombre_base, MemoriaCompartidaSistema **memoria_ret) {
    char nombre_shm[128];
    formatear_nombre_shm(nombre_shm, sizeof(nombre_shm), nombre_base);

    // Destruir por si quedó huérfana de una ejecución previa
    shm_unlink(nombre_shm);

    int fd = shm_open(nombre_shm, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (fd == -1) {
        perror("Error shm_open al crear memoria compartida");
        return -1;
    }

    size_t tamano = sizeof(MemoriaCompartidaSistema);
    if (ftruncate(fd, tamano) == -1) {
        perror("Error ftruncate memoria compartida");
        close(fd);
        shm_unlink(nombre_shm);
        return -1;
    }

    void *ptr = mmap(NULL, tamano, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) {
        perror("Error mmap al proyectar memoria compartida");
        shm_unlink(nombre_shm);
        return -1;
    }

    memset(ptr, 0, tamano);
    *memoria_ret = (MemoriaCompartidaSistema *)ptr;
    return 0;
}

int conectar_memoria_compartida(const char *nombre_base, MemoriaCompartidaSistema **memoria_ret) {
    char nombre_shm[128];
    formatear_nombre_shm(nombre_shm, sizeof(nombre_shm), nombre_base);

    int fd = shm_open(nombre_shm, O_RDWR, 0666);
    if (fd == -1) {
        return -1;
    }

    size_t tamano = sizeof(MemoriaCompartidaSistema);
    void *ptr = mmap(NULL, tamano, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) {
        perror("Error mmap al conectar a memoria compartida");
        return -1;
    }

    *memoria_ret = (MemoriaCompartidaSistema *)ptr;
    return 0;
}

void desconectar_memoria_compartida(MemoriaCompartidaSistema *memoria) {
    if (memoria) {
        munmap((void *)memoria, sizeof(MemoriaCompartidaSistema));
    }
}

void destruir_memoria_compartida(const char *nombre_base) {
    char nombre_shm[128];
    formatear_nombre_shm(nombre_shm, sizeof(nombre_shm), nombre_base);
    shm_unlink(nombre_shm);
}
