CC ?= gcc
CFLAGS ?= -O3 -flto -pipe -std=c11 -Wall -Wextra -Wshadow -Wconversion -fno-asynchronous-unwind-tables -fno-unwind-tables
ARCH_FLAGS ?= -march=x86-64-v3 -mavx2 -mfma
CPPFLAGS += -Iinclude
LDLIBS += -lz -lm

BUILD_DIR := build
COMMON := src/parser.c src/index.c

.PHONY: all clean

all: $(BUILD_DIR)/rinha-api $(BUILD_DIR)/rinha-lb $(BUILD_DIR)/build-index $(BUILD_DIR)/check-examples $(BUILD_DIR)/query-once $(BUILD_DIR)/bench-query

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/rinha-api: src/api.c $(COMMON) include/rinha.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -o $@ src/api.c $(COMMON) $(LDLIBS)

$(BUILD_DIR)/rinha-lb: src/lb.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O3 -o $@ src/lb.c

$(BUILD_DIR)/build-index: tools/build_index.c src/index.c include/rinha.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -o $@ tools/build_index.c src/index.c $(LDLIBS)

$(BUILD_DIR)/check-examples: tools/check_examples.c src/parser.c include/rinha.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tools/check_examples.c src/parser.c -lm

$(BUILD_DIR)/query-once: tools/query_once.c $(COMMON) include/rinha.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -o $@ tools/query_once.c $(COMMON) $(LDLIBS)

$(BUILD_DIR)/bench-query: tools/bench_query.c $(COMMON) include/rinha.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARCH_FLAGS) -o $@ tools/bench_query.c $(COMMON) $(LDLIBS)

clean:
	rm -rf $(BUILD_DIR)
