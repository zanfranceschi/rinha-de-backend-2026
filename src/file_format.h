/*
 * file_format.h — Formato binario do index.bin
 *
 * Layout:
 *   [0..63]            Header (64 bytes, packed)
 *   [64..]             Vetores: float32[num_vectors][14]
 *   [vectors_end..]    Labels:  uint8[num_vectors]
 *   [labels_end..]     VP-Tree nodes: vptree_node_t[tree_num_nodes]
 *
 * Todos os offsets sao absolutos em relacao ao inicio do arquivo.
 */

#ifndef FILE_FORMAT_H
#define FILE_FORMAT_H

#include <stdint.h>

/* "RNHA" em little-endian: 0x52, 0x4E, 0x48, 0x41 */
#define INDEX_MAGIC   0x41484E52U
#define INDEX_VERSION 2U

/*
 * Header do index.bin — exatamente 64 bytes (packed para garantir layout).
 *
 * v2 repurposes padding[20] to hold ivf_section_offset (8 bytes) + reserved.
 * v1 readers see ivf_section_offset == 0 and ignore IVF.
 *
 * Campo              Tipo      Bytes  Descricao
 * ──────────────────────────────────────────────────────────────
 * magic              uint32    4      Deve ser INDEX_MAGIC
 * version            uint32    4      INDEX_VERSION (2)
 * num_vectors        uint32    4      Numero de vetores no dataset
 * dims               uint32    4      Numero de dimensoes (14)
 * vectors_offset     uint64    8      Offset absoluto dos vetores float32
 * labels_offset      uint64    8      Offset absoluto dos labels
 * tree_offset        uint64    8      Offset absoluto dos nodes VP-Tree
 * tree_num_nodes     uint32    4      Numero de nodes VP-Tree
 * ivf_section_offset uint64    8      Offset do IVF section header (0=sem IVF)
 * reserved           uint8[12] 12     Zero (uso futuro)
 * ──────────────────────────────────────────────────────────────
 * TOTAL                        64
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t num_vectors;
    uint32_t dims;
    uint64_t vectors_offset;
    uint64_t labels_offset;
    uint64_t tree_offset;
    uint32_t tree_num_nodes;
    uint64_t ivf_section_offset;   /* v2: offset da ivf_section_header_t */
    uint8_t  reserved[12];
} index_header_t;

/* Verificacao em tempo de compilacao */
typedef char assert_header_size[(sizeof(index_header_t) == 64) ? 1 : -1];

/*
 * IVF Section Header — colocado no offset ivf_section_offset do index.bin.
 *
 * Seguido imediatamente por:
 *   centroids:       float32[num_clusters * dims]
 *   cluster_offsets: uint32[num_clusters + 1]
 *   sorted_indices:  uint32[num_vectors]
 *   quant_vec:       int16[num_vectors * dims]    row-major, sorted by cluster
 *   col_data:        int16[dims][num_vectors]     column-major, same cluster sort
 *   bbox_min:        int16[num_clusters * dims]   bbox min per cluster
 *   bbox_max:        int16[num_clusters * dims]   bbox max per cluster
 *
 * v3 adds col_data_offset, bbox_min_offset, bbox_max_offset.
 * Older readers see these as 0 and fall back to row-major scan.
 */
typedef struct __attribute__((packed)) {
    uint32_t num_clusters;       /* 4096 */
    uint32_t nprobe;             /* recomendado */
    uint64_t centroids_offset;   /* absoluto — float32[num_clusters * dims] */
    uint64_t offsets_offset;     /* absoluto — uint32[num_clusters + 1] */
    uint64_t sorted_idx_offset;  /* absoluto — uint32[num_vectors] */
    uint64_t quant_vec_offset;   /* absoluto — int16[num_vectors * dims] row-major */
    uint64_t col_data_offset;    /* absoluto — int16[dims][num_vectors]  col-major (0=absent) */
    uint64_t bbox_min_offset;    /* absoluto — int16[num_clusters * dims] (0=absent) */
    uint64_t bbox_max_offset;    /* absoluto — int16[num_clusters * dims] (0=absent) */
} ivf_section_header_t;

typedef char assert_ivf_header_size[(sizeof(ivf_section_header_t) == 64) ? 1 : -1];

#endif /* FILE_FORMAT_H */
