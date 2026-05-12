#include "rinha.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: build-index <references.json.gz> <index.bin> [clusters=1280] [sample=65536] [iters=6] [max_refs=0]\n");
        return 1;
    }

    rinha_build_options options = {
        .clusters = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 1280,
        .train_sample = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) : 65536,
        .iterations = argc > 5 ? (uint32_t)strtoul(argv[5], NULL, 10) : 6,
        .max_refs = argc > 6 ? (uint32_t)strtoul(argv[6], NULL, 10) : 0,
    };

    rinha_index index;
    rinha_index_init(&index);
    if (!rinha_index_build_from_gzip(argv[1], &options, &index)) {
        fprintf(stderr, "failed to build index from %s\n", argv[1]);
        return 1;
    }
    if (!rinha_index_write(&index, argv[2])) {
        fprintf(stderr, "failed to write %s\n", argv[2]);
        rinha_index_free(&index);
        return 1;
    }

    fprintf(stdout, "index=%s refs=%u clusters=%u blocks=%u memory_mb=%.2f\n",
            argv[2], index.n, index.clusters, index.total_blocks,
            (double)rinha_index_memory_bytes(&index) / (1024.0 * 1024.0));
    rinha_index_free(&index);
    return 0;
}
