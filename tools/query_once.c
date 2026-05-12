#include "rinha.h"

#include <stdio.h>
#include <stdlib.h>

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

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: query-once <index.bin> <payload.json>\n");
        return 1;
    }
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
        return 1;
    }
    rinha_payload payload;
    rinha_vector vector;
    if (!rinha_parse_payload(json, len, &payload)) {
        fprintf(stderr, "failed to parse payload\n");
        free(json);
        rinha_index_free(&index);
        return 1;
    }
    rinha_vectorize(&payload, &vector);
    rinha_search_config config = {
        .fast_nprobe = 1,
        .full_nprobe = 1,
        .bbox_repair = 1,
        .repair_min_frauds = 1,
        .repair_max_frauds = 4,
    };
    uint8_t frauds = rinha_index_fraud_count(&index, &vector, &config);
    printf("frauds=%u approved=%s score=%.1f\n", frauds, frauds < 3 ? "true" : "false", (double)frauds / 5.0);
    free(json);
    rinha_index_free(&index);
    return 0;
}
