CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c99 -pthread -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -g
LDFLAGS ?= -pthread -lrt

SRC_DIR = src
BIN_DIR = bin
TARGET = $(BIN_DIR)/evaluator
SYMLINK = $(BIN_DIR)/evaluador
PREFIX ?= /usr/local/bin

SRCS = $(SRC_DIR)/evaluador_principal.c \
       $(SRC_DIR)/memoria_compartida.c \
       $(SRC_DIR)/semaforos_sistema.c \
       $(SRC_DIR)/comando_inicializar.c \
       $(SRC_DIR)/comando_registro.c \
       $(SRC_DIR)/comando_control.c \
       $(SRC_DIR)/comando_reporte.c

OBJS = $(SRCS:.c=.o)

.PHONY: all build clean install ipc-clean test

all: build

build: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)
	@ln -sf evaluator $(SYMLINK)
	@echo "Compilación exitosa: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -f $(SRC_DIR)/*.o
	@rm -rf $(BIN_DIR)/*
	@echo "Limpieza completada."

ipc-clean:
	@echo "Limpiando segmentos POSIX shm y semáforos huérfanos..."
	@rm -f /dev/shm/*evaluator* /dev/shm/sem.*evaluator*
	@echo "Recursos IPC limpiados."

install: build
	@mkdir -p $(PREFIX)
	@cp -f $(TARGET) $(PREFIX)/evaluator
	@cp -f $(SYMLINK) $(PREFIX)/evaluador
	@echo "Instalado en $(PREFIX)/evaluator y $(PREFIX)/evaluador"
