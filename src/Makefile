# src/Makefile — Compilacao do servidor HTTP
#
# Uso:
#   make          — compila o servidor (default)
#   make clean    — remove binarios e objetos gerados
#
# Variaveis de ambiente:
#   PORT=8080     — porta do servidor (default 8080)
#   INDEX_PATH=   — caminho do index.bin (default ./index.bin)

CC      = gcc
CFLAGS  = -O3 -std=c11 -Wall -Wextra -I. -Ivendor

# POSIX defines necessarios para clock_gettime (mongoose) no Linux/glibc
# No Mac/Darwin esses defines sao no-op (definidos pelo sistema)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    CFLAGS += -D_POSIX_C_SOURCE=199309L -D_DEFAULT_SOURCE
    # Enable AVX2 on Linux (x86_64) — Mac ARM64 does not support AVX2
    UNAME_M := $(shell uname -m)
    ifeq ($(UNAME_M),x86_64)
        CFLAGS += -mavx2 -march=haswell
    endif
endif
LDFLAGS = -lm

# Definicoes para mongoose (habilita funcionalidades POSIX)
CFLAGS += -DMG_ENABLE_POSIX_FS=1

SRCS = main.c \
       engine.c \
       vectorize.c \
       distance.c \
       quantize.c \
       vptree.c \
       ivf.c \
       vendor/mongoose.c \
       vendor/yyjson.c

OBJS = $(SRCS:.c=.o)

TARGET = server

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Dependencias explicitas para headers customizados
main.o:     main.c engine.h vectorize.h vendor/mongoose.h vendor/yyjson.h
engine.o:   engine.c engine.h file_format.h vptree.h vectorize.h distance.h
vectorize.o: vectorize.c vectorize.h
distance.o: distance.c distance.h vectorize.h
quantize.o: quantize.c quantize.h vectorize.h
vptree.o:   vptree.c vptree.h distance.h vectorize.h
ivf.o:      ivf.c ivf.h distance.h vectorize.h vptree.h

clean:
	rm -f $(TARGET) $(OBJS)
