# tests/Makefile — Compilacao e execucao da suite TDD
#
# Uso:
#   make test    — compila e executa todos os testes
#   make clean   — remove binarios gerados

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -pedantic -g -I../src
LDFLAGS = -lm

# Fontes de teste
TEST_SRCS = test_main.c \
            test_vectorize.c \
            test_distance.c \
            test_scoring.c \
            test_vptree.c \
            test_quantize.c \
            test_ivf.c

# Fontes de producao (stubs ou implementacoes reais)
SRC_SRCS = ../src/vectorize.c \
           ../src/distance.c \
           ../src/vptree.c \
           ../src/quantize.c \
           ../src/ivf.c

TEST_BIN = test_runner

.PHONY: test clean

test: $(TEST_BIN)
	./$(TEST_BIN); \
	EXIT_CODE=$$?; \
	if [ $$EXIT_CODE -ne 0 ]; then \
	    echo ""; \
	    echo "*** TESTES FALHARAM (RED — esperado antes da implementacao) ***"; \
	    exit $$EXIT_CODE; \
	fi

$(TEST_BIN): $(TEST_SRCS) $(SRC_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TEST_BIN)
