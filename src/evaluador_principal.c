#include <stdio.h>
#include <string.h>

#include "comando_inicializar.h"
#include "comando_registro.h"
#include "comando_control.h"
#include "comando_reporte.h"

static void imprimir_uso(const char *prog) {
    printf("==================================================================\n");
    printf("   SISTEMA DE EVALUACIÓN PARALELO DE EXÁMENES MÉDICOS            \n");
    printf("==================================================================\n");
    printf("Uso: %s <comando> [opciones...]\n\n", prog);
    printf("Comandos disponibles:\n");
    printf("  init   Inicializa el sistema, memoria compartida y servidor evaluador.\n");
    printf("         evaluator init [-i <int>] [-ie <int>] [-oe <int>] [-n <str>]\n");
    printf("                        [-b <int>] [-d <int>] [-s <int>] [-ee <int>] [-q <int>]\n\n");
    printf("  reg    Registra exámenes por lote (archivos) o en modo interactivo (-).\n");
    printf("         evaluator reg [-n <str>] {{<filename>} ... | - }\n\n");
    printf("  ctrl   Abre consola interactiva de monitoreo (list) y recarga (update).\n");
    printf("         evaluator ctrl [-s <str>]\n\n");
    printf("  rep    Extrae y reporta resultados procesados de la cola de salida.\n");
    printf("         evaluator rep [-s <str>] { -i <int> | -m <int> }\n\n");
    printf("Consulte la documentación en la carpeta 'doc/' para más detalles.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        imprimir_uso(argv[0]);
        return 1;
    }

    const char *comando = argv[1];

    if (strcmp(comando, "init") == 0) {
        return ejecutar_comando_inicializar(argc - 1, &argv[1]);
    } else if (strcmp(comando, "reg") == 0) {
        return ejecutar_comando_registro(argc - 1, &argv[1]);
    } else if (strcmp(comando, "ctrl") == 0) {
        return ejecutar_comando_control(argc - 1, &argv[1]);
    } else if (strcmp(comando, "rep") == 0) {
        return ejecutar_comando_reporte(argc - 1, &argv[1]);
    } else if (strcmp(comando, "--help") == 0 || strcmp(comando, "-h") == 0) {
        imprimir_uso(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Error: Comando desconocido '%s'.\n\n", comando);
        imprimir_uso(argv[0]);
        return 1;
    }
}
