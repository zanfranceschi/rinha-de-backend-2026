# tools/Makefile — Ferramentas de build offline
#
# Uso:
#   make build-index         — compila o gerador de index.bin
#   make clean               — remove binarios gerados

CC      = gcc
CFLAGS  = -O3 -std=c11 -Wall -Wextra -I../src -I../src/vendor
LDFLAGS = -lm

# Enable AVX2 on Linux x86_64 (Mac ARM64 does not support AVX2)
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_S),Linux)
    ifeq ($(UNAME_M),x86_64)
        CFLAGS += -mavx2 -march=haswell
    endif
endif

SRC_CORE = ../src/vptree.c \
           ../src/ivf.c \
           ../src/quantize.c \
           ../src/distance.c \
           ../src/vectorize.c \
           ../src/vendor/yyjson.c

.PHONY: build-index clean

build-index: build_index
	@echo "build_index pronto"

build_index: build_index.c $(SRC_CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f build_index
