#define _POSIX_C_SOURCE 200809L
#include "rinha.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    char *data = (char *)malloc((size_t)size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    *len = fread(data, 1, (size_t)size, f);
    data[*len] = '\0';
    fclose(f);
    return data;
}

static uint64_t nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: bench-query <index.bin> <payload.json> <iterations> [nprobe=1]\n");
        return 1;
    }
    uint32_t iterations = (uint32_t)strtoul(argv[3], NULL, 10);
    uint32_t nprobe = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) : 1;

    rinha_index index;
    rinha_index_init(&index);
    if (!rinha_index_load(&index, argv[1])) {
        fprintf(stderr, "failed to load index\n");
        return 1;
    }
    size_t len = 0;
    char *json = read_all(argv[2], &len);
    if (!json) {
        fprintf(stderr, "failed to read payload\n");
        rinha_index_free(&index);
        return 1;
    }

    rinha_search_config config = {
        .fast_nprobe = nprobe,
        .full_nprobe = nprobe,
        .bbox_repair = 1,
        .repair_min_frauds = 1,
        .repair_max_frauds = 4,
    };
    volatile uint32_t checksum = 0;
    uint64_t start = nanos();
    for (uint32_t i = 0; i < iterations; ++i) {
        rinha_payload payload;
        rinha_vector vector;
        if (!rinha_parse_payload(json, len, &payload)) return 1;
        rinha_vectorize(&payload, &vector);
        checksum += rinha_index_fraud_count(&index, &vector, &config);
    }
    uint64_t elapsed = nanos() - start;
    printf("iterations=%u nprobe=%u total_ms=%.3f avg_us=%.3f checksum=%u\n",
           iterations, nprobe, (double)elapsed / 1000000.0, (double)elapsed / (double)iterations / 1000.0, checksum);
    free(json);
    rinha_index_free(&index);
    return 0;
}
