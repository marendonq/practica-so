#!/bin/bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$DIR/bin/evaluator"
SHM_NAME="eval_prueba_$$"

echo "====================================================================="
echo "   PRUEBA INTEGRAL DEL SISTEMA EVALUADOR DE EXÁMENES MÉDICOS        "
echo "====================================================================="

# 1. Asegurar compilación
echo "[Paso 1] Verificando compilación..."
make -C "$DIR" build

# 2. Limpieza de memoria compartida previa si existiera
echo "[Paso 2] Limpiando recursos previos..."
rm -f "/dev/shm/${SHM_NAME}" "/dev/shm/sem.${SHM_NAME}"*

# 3. Iniciar evaluator init en segundo plano
echo "[Paso 3] Iniciando 'evaluator init' en segundo plano (SHM: $SHM_NAME)..."
"$BIN" init -n "$SHM_NAME" -i 5 -ie 6 -oe 10 -b 30 -d 30 -s 30 -ee 10 > "$DIR/examples/init.log" 2>&1 &
INIT_PID=$!
echo "  -> Proceso evaluador iniciado con PID $INIT_PID"
sleep 1

# Asegurarse de que el proceso evaluador se cierre al terminar el script
cleanup() {
    echo ""
    echo "[Limpieza] Enviando señal de apagado SIGINT al evaluador (PID $INIT_PID)..."
    kill -2 "$INIT_PID" 2>/dev/null || true
    wait "$INIT_PID" 2>/dev/null || true
    echo "[Limpieza] Verificando eliminación de recursos en /dev/shm..."
    ls -l "/dev/shm/${SHM_NAME}" 2>/dev/null && echo "Advertencia: SHM no se eliminó" || echo "  -> SHM eliminada correctamente."
    echo "[Limpieza] Prueba finalizada."
}
trap cleanup EXIT

# 4. Registrar muestras desde archivo
echo ""
echo "[Paso 4] Registrando muestras desde archivo 'examples/muestras_pacientes.txt'..."
"$BIN" reg -n "$SHM_NAME" "$DIR/examples/muestras_pacientes.txt"
echo "  -> Comprobando archivo de salida '.spl' generado:"
cat "$DIR/examples/muestras_pacientes.spl"

# 5. Registrar muestras en modo interactivo vía stdin (-)
echo ""
echo "[Paso 5] Registrando muestras en modo interactivo vía stdin (-)..."
printf "0 B 3\n2 S 2\n" | "$BIN" reg -n "$SHM_NAME" -

# 6. Consultar estado mediante evaluator ctrl (list all)
echo ""
echo "[Paso 6] Consultando estado del sistema con 'evaluator ctrl'..."
printf "list all\nexit\n" | "$BIN" ctrl -s "$SHM_NAME"

# 7. Actualizar reactivos con evaluator ctrl update
echo ""
echo "[Paso 7] Reabasteciendo reactivos con 'evaluator ctrl update'..."
printf "update B 50\nupdate D 50\nupdate S 50\nlist reactive\nexit\n" | "$BIN" ctrl -s "$SHM_NAME"

# 8. Extraer muestras con evaluator rep (-m 2)
echo ""
echo "[Paso 8] Esperando y extrayendo 2 resultados procesados con 'evaluator rep -m 2'..."
"$BIN" rep -s "$SHM_NAME" -m 2

echo ""
echo "====================================================================="
echo "   ¡TODAS LAS PRUEBAS COMPLETADAS CON ÉXITO!                        "
echo "====================================================================="
