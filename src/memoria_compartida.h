#ifndef MEMORIA_COMPARTIDA_H
#define MEMORIA_COMPARTIDA_H

#include "definiciones_globales.h"

/* Funciones para gestión del segmento de memoria compartida POSIX */
int crear_memoria_compartida(const char *nombre_base, MemoriaCompartidaSistema **memoria_ret);
int conectar_memoria_compartida(const char *nombre_base, MemoriaCompartidaSistema **memoria_ret);
void desconectar_memoria_compartida(MemoriaCompartidaSistema *memoria);
void destruir_memoria_compartida(const char *nombre_base);

#endif // MEMORIA_COMPARTIDA_H
