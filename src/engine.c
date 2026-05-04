/*
 * engine.c — Motor de deteccao de fraude
 *
 * Fluxo:
 *   engine_init  → mmap(index.bin) → valida header → configura ponteiros
 *   engine_score → vectorize(tx) → vptree_search(k=5) → fraud_score = n_fraud/5
 *   engine_shutdown → munmap
 *
 * Regras criticas:
 *   - mmap com MAP_SHARED + PROT_READ (compartilhado entre containers)
 *   - VP-Tree NAO e reconstruida: nodes lidos diretamente do mmap (zero copy)
 *   - Fallback {approved=1, fraud_score=0.0f} em qualquer erro interno
 *   - Thread-safe apos init (estruturas somente leitura)
 *   - Zero alocacao por request (buffers em stack: query[14] + neighbors[5])
 */

#include "engine.h"
#include "file_format.h"
#include "vptree.h"
#include "ivf.h"
#include "vectorize.h"
#include "distance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* =========================================================================
 * Estado global do engine (inicializado uma vez, somente leitura depois)
 * ========================================================================= */

typedef enum { BACKEND_VPTREE, BACKEND_IVF } search_backend_t;

static struct {
    void    *mmap_base;   /* ponteiro base do mmap */
    size_t   mmap_size;   /* tamanho total do arquivo mapeado */

    uint32_t num_vectors;  /* total de vetores no dataset */
    float   *vectors;     /* aponta para secao de vetores no mmap */
    uint8_t *labels;      /* aponta para secao de labels no mmap */

    vptree_t tree;        /* tree.nodes aponta para secao VP-Tree no mmap */

    ivf_index_t ivf;      /* IVF index (ponteiros no mmap) */
    int         has_ivf;  /* 1 se index.bin contem secao IVF */

    search_backend_t backend; /* qual algoritmo usar */
    int      initialized; /* 1 se engine_init teve sucesso */
} g_engine = {
    .mmap_base   = NULL,
    .mmap_size   = 0,
    .vectors     = NULL,
    .labels      = NULL,
    .has_ivf     = 0,
    .backend     = BACKEND_VPTREE,
    .initialized = 0,
};

/* =========================================================================
 * Fallback — retornado em qualquer erro interno
 * ========================================================================= */
static const score_result_t FALLBACK = { .approved = 1, .fraud_score = 0.0f };

/* =========================================================================
 * engine_init
 * ========================================================================= */
int engine_init(const char *index_path)
{
    if (!index_path) {
        fprintf(stderr, "engine_init: index_path nulo\n");
        return -1;
    }

    int fd = open(index_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "engine_init: nao foi possivel abrir '%s'\n",
                index_path);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "engine_init: fstat falhou\n");
        close(fd);
        return -1;
    }

    size_t file_size = (size_t)st.st_size;
    if (file_size < sizeof(index_header_t)) {
        fprintf(stderr, "engine_init: arquivo muito pequeno (%zu bytes)\n",
                file_size);
        close(fd);
        return -1;
    }

    void *base = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd); /* fd pode ser fechado apos mmap */

    if (base == MAP_FAILED) {
        fprintf(stderr, "engine_init: mmap falhou\n");
        return -1;
    }

    /* Valida header */
    const index_header_t *hdr = (const index_header_t *)base;

    if (hdr->magic != INDEX_MAGIC) {
        fprintf(stderr, "engine_init: magic invalido (0x%08X esperado 0x%08X)\n",
                hdr->magic, INDEX_MAGIC);
        munmap(base, file_size);
        return -1;
    }

    if (hdr->version != 1U && hdr->version != 2U) {
        fprintf(stderr, "engine_init: versao incompativel (%u, suporta 1-2)\n",
                hdr->version);
        munmap(base, file_size);
        return -1;
    }

    if (hdr->dims != VECTOR_DIMS) {
        fprintf(stderr, "engine_init: dims incompativel (%u esperado %d)\n",
                hdr->dims, VECTOR_DIMS);
        munmap(base, file_size);
        return -1;
    }

    uint32_t nv   = hdr->num_vectors;
    uint32_t nn   = hdr->tree_num_nodes;
    uint64_t voff = hdr->vectors_offset;
    uint64_t loff = hdr->labels_offset;
    uint64_t toff = hdr->tree_offset;

    /* Verificacoes de bounds */
    size_t vectors_bytes = (size_t)nv * VECTOR_DIMS * sizeof(float);
    size_t labels_bytes  = (size_t)nv * sizeof(uint8_t);
    size_t nodes_bytes   = (size_t)nn * sizeof(vptree_node_t);

    if (voff + vectors_bytes > file_size ||
        loff + labels_bytes  > file_size ||
        toff + nodes_bytes   > file_size) {
        fprintf(stderr, "engine_init: offsets fora dos limites do arquivo\n");
        munmap(base, file_size);
        return -1;
    }

    /* Configura ponteiros diretamente no mmap (zero copy) */
    g_engine.mmap_base   = base;
    g_engine.mmap_size   = file_size;
    g_engine.num_vectors = nv;
    g_engine.vectors     = (float *)  ((uint8_t *)base + voff);
    g_engine.labels      = (uint8_t *)((uint8_t *)base + loff);

    /* VP-Tree aponta para nodes no mmap — NAO reconstroi */
    g_engine.tree.vectors     = g_engine.vectors;
    g_engine.tree.labels      = g_engine.labels;
    g_engine.tree.num_vectors = (int)nv;
    g_engine.tree.nodes       = (vptree_node_t *)((uint8_t *)base + toff);
    g_engine.tree.num_nodes   = (int)nn;

    /* Carrega secao IVF se presente (v2+) */
    g_engine.has_ivf = 0;
    if (hdr->version >= 2 && hdr->ivf_section_offset != 0) {
        uint64_t ioff = hdr->ivf_section_offset;
        if (ioff + sizeof(ivf_section_header_t) <= file_size) {
            const ivf_section_header_t *ihdr =
                (const ivf_section_header_t *)((uint8_t *)base + ioff);

            g_engine.ivf.num_vectors  = (int)nv;
            g_engine.ivf.num_clusters = (int)ihdr->num_clusters;
            g_engine.ivf.nprobe       = (int)ihdr->nprobe;
            g_engine.ivf.centroids        = (const float *)   ((uint8_t *)base + ihdr->centroids_offset);
            g_engine.ivf.cluster_offsets  = (const uint32_t *)((uint8_t *)base + ihdr->offsets_offset);
            g_engine.ivf.sorted_indices   = (const uint32_t *)((uint8_t *)base + ihdr->sorted_idx_offset);
            g_engine.ivf.quantized_vectors = (ihdr->quant_vec_offset != 0)
                ? (const int16_t *)((uint8_t *)base + ihdr->quant_vec_offset)
                : NULL;

            /* Column-major layout for AVX2 (optional — v3+ index) */
            if (ihdr->col_data_offset != 0) {
                const int16_t *col_base =
                    (const int16_t *)((uint8_t *)base + ihdr->col_data_offset);
                for (int d = 0; d < VECTOR_DIMS; d++) {
                    g_engine.ivf.col_data[d] = col_base + (size_t)d * (size_t)nv;
                }
                fprintf(stderr, "engine_init: col_data (column-major) carregado\n");
            } else {
                for (int d = 0; d < VECTOR_DIMS; d++)
                    g_engine.ivf.col_data[d] = NULL;
            }

            /* Bounding boxes per cluster (optional — v3+ index) */
            g_engine.ivf.bbox_min = (ihdr->bbox_min_offset != 0)
                ? (const int16_t *)((uint8_t *)base + ihdr->bbox_min_offset)
                : NULL;
            g_engine.ivf.bbox_max = (ihdr->bbox_max_offset != 0)
                ? (const int16_t *)((uint8_t *)base + ihdr->bbox_max_offset)
                : NULL;
            if (g_engine.ivf.bbox_min)
                fprintf(stderr, "engine_init: bbox pruning carregado\n");

            g_engine.ivf.vectors         = g_engine.vectors;
            g_engine.ivf.labels          = g_engine.labels;

            g_engine.has_ivf = 1;
            fprintf(stderr, "engine_init: IVF carregado — %u clusters, nprobe=%u\n",
                    ihdr->num_clusters, ihdr->nprobe);
        }
    }

    /* Seleciona backend: IVF se disponivel, senao VP-Tree */
    const char *env_backend = getenv("SEARCH_BACKEND");
    if (env_backend && strcmp(env_backend, "vptree") == 0) {
        g_engine.backend = BACKEND_VPTREE;
    } else if (g_engine.has_ivf) {
        g_engine.backend = BACKEND_IVF;
    } else {
        g_engine.backend = BACKEND_VPTREE;
    }

    g_engine.initialized = 1;

    fprintf(stderr, "engine_init: OK — %u vetores, backend=%s, %.1f MB\n",
            nv, (g_engine.backend == BACKEND_IVF) ? "IVF" : "VP-Tree",
            (double)file_size / (1024.0 * 1024.0));
    return 0;
}

/* =========================================================================
 * engine_score
 * ========================================================================= */
score_result_t engine_score(const transaction_t *tx)
{
    if (!g_engine.initialized || !tx)
        return FALLBACK;

    /* Vetorizacao — buffer no stack (14 floats = 56 bytes) */
    float query[VECTOR_DIMS];
    vectorize(tx, query);

    /* KNN — buffer no stack (5 vizinhos) */
    neighbor_t neighbors[KNN_K];
    int found;

    if (g_engine.backend == BACKEND_IVF && g_engine.has_ivf) {
        found = ivf_search(&g_engine.ivf, query, KNN_K, neighbors);
    } else {
        found = vptree_search(&g_engine.tree, query, KNN_K, neighbors);
    }

    if (found <= 0)
        return FALLBACK;

    /* Conta fraudes nos vizinhos (ambos backends retornam indice original) */
    int fraud_count = 0;
    for (int i = 0; i < found; i++) {
        uint32_t idx = (uint32_t)neighbors[i].index;
        if (idx < g_engine.num_vectors) {
            if (g_engine.labels[idx] == 1)
                fraud_count++;
        }
    }

    float fraud_score = (float)fraud_count / (float)KNN_K;
    int   approved    = (fraud_score < FRAUD_THRESHOLD) ? 1 : 0;

    return (score_result_t){ .approved = approved, .fraud_score = fraud_score };
}

/* =========================================================================
 * engine_shutdown
 * ========================================================================= */
void engine_shutdown(void)
{
    if (g_engine.mmap_base && g_engine.mmap_base != MAP_FAILED) {
        munmap(g_engine.mmap_base, g_engine.mmap_size);
        g_engine.mmap_base = NULL;
    }
    g_engine.initialized = 0;
}

/* =========================================================================
 * engine_ready
 * ========================================================================= */
int engine_ready(void)
{
    return g_engine.initialized;
}
