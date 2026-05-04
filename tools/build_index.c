/*
 * build_index.c — Gera index.bin a partir de references.json
 *
 * Uso:
 *   ./build_index <references.json> <output.bin>
 *
 * Formato de entrada (references.json):
 * [
 *   { "vector": [f0, f1, ..., f13], "label": "legit" | "fraud" },
 *   ...
 * ]
 *
 * Formato de saida (index.bin):
 *   Header 64 bytes (index_header_t)
 *   Vetores: float32[num_vectors][14]
 *   Labels:  uint8[num_vectors]
 *   VP-Tree nodes: vptree_node_t[num_nodes]
 *
 * Memoria durante build (~500MB para 3M vetores):
 *   - Buffer JSON: ate 284MB
 *   - Vetores float32: 3M * 14 * 4 = 168MB
 *   - Labels uint8:    3M * 1     =   3MB
 *   - VP-Tree nodes:   3M * 16    =  48MB
 *   - Indices temp:    3M * 4     =  12MB
 * Isso e aceitavel no Dockerfile build stage (nao afeta runtime).
 *
 * Compilacao:
 *   gcc -O3 -std=c11 -Wall -Wextra -o build_index build_index.c \
 *       ../src/vptree.c ../src/distance.c ../src/vectorize.c \
 *       ../src/vendor/yyjson.c -lm -I../src -I../src/vendor
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>

#include "yyjson.h"
#include "vectorize.h"
#include "vptree.h"
#include "ivf.h"
#include "quantize.h"
#include "file_format.h"

/* Capacidade inicial de vetores (cresce se necessario) */
#define INITIAL_CAPACITY 4096

/* =========================================================================
 * Leitura do arquivo completo em memoria
 * ========================================================================= */
static char *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) {
        fprintf(stderr, "Erro: arquivo vazio '%s'\n", path);
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fprintf(stderr, "Erro: malloc falhou para %ld bytes\n", sz);
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (read_bytes != (size_t)sz) {
        fprintf(stderr, "Erro: leitura incompleta (%zu de %ld bytes)\n",
                read_bytes, sz);
        free(buf);
        return NULL;
    }

    buf[sz] = '\0';
    *out_size = (size_t)sz;
    return buf;
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <references.json> <output.bin>\n", argv[0]);
        return 1;
    }

    const char *json_path   = argv[1];
    const char *output_path = argv[2];

    fprintf(stderr, "Lendo '%s'...\n", json_path);

    /* Lê o arquivo JSON completo */
    size_t json_size = 0;
    char  *json_buf  = read_file(json_path, &json_size);
    if (!json_buf) return 1;

    fprintf(stderr, "Parse JSON (%.1f MB)...\n",
            (double)json_size / (1024.0 * 1024.0));

    clock_t t0 = clock();

    yyjson_doc *doc = yyjson_read(json_buf, json_size, 0);
    if (!doc) {
        fprintf(stderr, "Erro: yyjson_read falhou\n");
        free(json_buf);
        return 1;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root)) {
        fprintf(stderr, "Erro: root nao e array\n");
        yyjson_doc_free(doc);
        free(json_buf);
        return 1;
    }

    size_t num_elements = yyjson_arr_size(root);
    fprintf(stderr, "Encontrados %zu elementos\n", num_elements);

    if (num_elements == 0) {
        fprintf(stderr, "Erro: array vazio\n");
        yyjson_doc_free(doc);
        free(json_buf);
        return 1;
    }

    /* Aloca arrays de vetores e labels */
    float   *vectors = (float *)  malloc(num_elements * VECTOR_DIMS * sizeof(float));
    uint8_t *labels  = (uint8_t *)malloc(num_elements * sizeof(uint8_t));

    if (!vectors || !labels) {
        fprintf(stderr, "Erro: malloc falhou (%.1f MB necessarios)\n",
                (double)(num_elements * VECTOR_DIMS * sizeof(float) +
                         num_elements) / (1024.0 * 1024.0));
        free(vectors);
        free(labels);
        yyjson_doc_free(doc);
        free(json_buf);
        return 1;
    }

    /* Itera sobre os elementos */
    size_t num_vectors  = 0;
    size_t num_fraud    = 0;
    size_t num_legit    = 0;
    size_t num_skipped  = 0;

    size_t idx, max_idx;
    yyjson_val *elem;

    yyjson_arr_foreach(root, idx, max_idx, elem) {
        if (!yyjson_is_obj(elem)) {
            num_skipped++;
            continue;
        }

        /* Extrai vetor */
        yyjson_val *vec_arr = yyjson_obj_get(elem, "vector");
        if (!yyjson_is_arr(vec_arr) || yyjson_arr_size(vec_arr) != VECTOR_DIMS) {
            num_skipped++;
            continue;
        }

        float *dst = vectors + num_vectors * VECTOR_DIMS;
        size_t vi, vmax;
        yyjson_val *vval;
        int valid = 1;

        yyjson_arr_foreach(vec_arr, vi, vmax, vval) {
            if (!yyjson_is_num(vval)) {
                valid = 0;
                break;
            }
            dst[vi] = (float)yyjson_get_num(vval);
        }

        if (!valid) {
            num_skipped++;
            continue;
        }

        /* Extrai label */
        yyjson_val *label_val = yyjson_obj_get(elem, "label");
        if (!yyjson_is_str(label_val)) {
            num_skipped++;
            continue;
        }

        const char *label_str = yyjson_get_str(label_val);
        uint8_t label;

        if (strcmp(label_str, "fraud") == 0) {
            label = 1;
            num_fraud++;
        } else if (strcmp(label_str, "legit") == 0) {
            label = 0;
            num_legit++;
        } else {
            num_skipped++;
            continue;
        }

        labels[num_vectors] = label;
        num_vectors++;

        /* Progresso a cada 500K vetores */
        if (num_vectors % 500000 == 0) {
            fprintf(stderr, "  Processado: %zu vetores...\n", num_vectors);
        }
    }

    clock_t t1 = clock();
    fprintf(stderr, "Parse concluido em %.1f segundos\n",
            (double)(t1 - t0) / CLOCKS_PER_SEC);
    fprintf(stderr, "  Total: %zu | Legit: %zu | Fraud: %zu | Skipped: %zu\n",
            num_vectors, num_legit, num_fraud, num_skipped);

    /* Libera JSON (nao precisamos mais) */
    yyjson_doc_free(doc);
    free(json_buf);

    if (num_vectors == 0) {
        fprintf(stderr, "Erro: nenhum vetor valido encontrado\n");
        free(vectors);
        free(labels);
        return 1;
    }

    /* Constroi VP-Tree */
    fprintf(stderr, "Construindo VP-Tree (%zu vetores)...\n", num_vectors);
    clock_t t2 = clock();

    vptree_t tree;
    int ret = vptree_build(&tree, vectors, labels, (int)num_vectors);

    clock_t t3 = clock();
    if (ret != 0) {
        fprintf(stderr, "Erro: vptree_build falhou\n");
        free(vectors);
        free(labels);
        return 1;
    }

    fprintf(stderr, "VP-Tree construida em %.1f segundos (%d nodes)\n",
            (double)(t3 - t2) / CLOCKS_PER_SEC, tree.num_nodes);

    /* Constroi IVF index */
    fprintf(stderr, "Construindo IVF (%zu vetores, %d clusters)...\n",
            num_vectors, IVF_NUM_CLUSTERS);
    clock_t t_ivf0 = clock();

    float    *ivf_centroids   = (float *)   malloc(IVF_NUM_CLUSTERS * VECTOR_DIMS * sizeof(float));
    uint16_t *ivf_assignments = (uint16_t *)malloc(num_vectors * sizeof(uint16_t));
    float    *ivf_sorted_vec  = (float *)   malloc(num_vectors * VECTOR_DIMS * sizeof(float));
    uint8_t  *ivf_sorted_lbl  = (uint8_t *) malloc(num_vectors * sizeof(uint8_t));
    uint32_t *ivf_sorted_idx  = (uint32_t *)malloc(num_vectors * sizeof(uint32_t));
    uint32_t *ivf_offsets     = (uint32_t *)malloc((IVF_NUM_CLUSTERS + 1) * sizeof(uint32_t));

    if (!ivf_centroids || !ivf_assignments || !ivf_sorted_vec ||
        !ivf_sorted_lbl || !ivf_sorted_idx || !ivf_offsets) {
        fprintf(stderr, "Erro: malloc falhou para IVF\n");
        free(ivf_centroids); free(ivf_assignments); free(ivf_sorted_vec);
        free(ivf_sorted_lbl); free(ivf_sorted_idx); free(ivf_offsets);
        vptree_free(&tree); free(vectors); free(labels);
        return 1;
    }

    ivf_kmeans(vectors, (int)num_vectors, IVF_NUM_CLUSTERS, IVF_KMEANS_ITERS,
               ivf_centroids, ivf_assignments);

    ivf_sort_by_cluster(vectors, labels, ivf_assignments, (int)num_vectors,
                        IVF_NUM_CLUSTERS, ivf_sorted_vec, ivf_sorted_lbl,
                        ivf_sorted_idx, ivf_offsets);

    free(ivf_assignments);
    /* Quantiza sorted_vectors para int16 (contiguos por cluster para cache locality) */
    fprintf(stderr, "Quantizando vetores para int16 row-major (scale=%d)...\n", QUANT_SCALE);
    int16_t *ivf_quant_vec = (int16_t *)malloc(num_vectors * VECTOR_DIMS * sizeof(int16_t));
    if (!ivf_quant_vec) {
        fprintf(stderr, "Erro: malloc falhou para int16 vetores\n");
        free(ivf_sorted_vec); free(ivf_sorted_lbl);
        free(ivf_sorted_idx); free(ivf_offsets); free(ivf_centroids);
        vptree_free(&tree); free(vectors); free(labels);
        return 1;
    }
    for (size_t i = 0; i < num_vectors; i++) {
        quantize_f32_to_i16(ivf_sorted_vec + i * VECTOR_DIMS,
                            ivf_quant_vec + i * VECTOR_DIMS);
    }

    free(ivf_sorted_vec);
    free(ivf_sorted_lbl);

    /* Transpoe para column-major: col_data[d][vec] = quant_vec[vec][d]
     * This enables AVX2 to process 8 vectors simultaneously per dimension.
     * Size: VECTOR_DIMS * num_vectors * 2B = 14 * 3M * 2 = 84MB (same as row-major)
     */
    fprintf(stderr, "Transpondo para column-major (AVX2 layout)...\n");
    int16_t *ivf_col_data = (int16_t *)malloc((size_t)VECTOR_DIMS * num_vectors * sizeof(int16_t));
    if (!ivf_col_data) {
        fprintf(stderr, "Aviso: malloc falhou para col_data, pulando column-major\n");
        ivf_col_data = NULL;
    } else {
        for (size_t i = 0; i < num_vectors; i++) {
            for (int d = 0; d < VECTOR_DIMS; d++) {
                /* col_data[d * num_vectors + i] = quant_vec[i * VECTOR_DIMS + d] */
                ivf_col_data[(size_t)d * num_vectors + i] = ivf_quant_vec[i * VECTOR_DIMS + d];
            }
        }
    }

    /* Computa bounding boxes por cluster (int16 min/max em cada dimensao)
     * bbox_min[c * VECTOR_DIMS + d] = min over all vectors in cluster c of dim d
     * bbox_max[c * VECTOR_DIMS + d] = max over all vectors in cluster c of dim d
     * Used to compute exact lower bound before scanning a cluster.
     */
    fprintf(stderr, "Computando bounding boxes por cluster...\n");
    size_t bbox_bytes = (size_t)IVF_NUM_CLUSTERS * VECTOR_DIMS * sizeof(int16_t);
    int16_t *ivf_bbox_min = (int16_t *)malloc(bbox_bytes);
    int16_t *ivf_bbox_max = (int16_t *)malloc(bbox_bytes);

    if (!ivf_bbox_min || !ivf_bbox_max) {
        fprintf(stderr, "Aviso: malloc falhou para bbox, pulando pruning\n");
        free(ivf_bbox_min); free(ivf_bbox_max);
        ivf_bbox_min = NULL; ivf_bbox_max = NULL;
    } else {
        /* Initialize: INT16_MAX for min, INT16_MIN for max */
        for (int c = 0; c < IVF_NUM_CLUSTERS; c++) {
            for (int d = 0; d < VECTOR_DIMS; d++) {
                ivf_bbox_min[c * VECTOR_DIMS + d] = INT16_MAX;
                ivf_bbox_max[c * VECTOR_DIMS + d] = INT16_MIN;
            }
        }
        /* Scan quantized vectors sorted by cluster */
        for (int c = 0; c < IVF_NUM_CLUSTERS; c++) {
            uint32_t cstart = ivf_offsets[c];
            uint32_t cend   = ivf_offsets[c + 1];
            for (uint32_t pos = cstart; pos < cend; pos++) {
                const int16_t *qv = ivf_quant_vec + (size_t)pos * VECTOR_DIMS;
                for (int d = 0; d < VECTOR_DIMS; d++) {
                    int16_t val = qv[d];
                    if (val < ivf_bbox_min[c * VECTOR_DIMS + d])
                        ivf_bbox_min[c * VECTOR_DIMS + d] = val;
                    if (val > ivf_bbox_max[c * VECTOR_DIMS + d])
                        ivf_bbox_max[c * VECTOR_DIMS + d] = val;
                }
            }
        }
    }

    clock_t t_ivf1 = clock();
    fprintf(stderr, "IVF construido em %.1f segundos\n",
            (double)(t_ivf1 - t_ivf0) / CLOCKS_PER_SEC);

    /* Calcula offsets */
    uint64_t vectors_offset = sizeof(index_header_t);
    uint64_t labels_offset  = vectors_offset +
                              (uint64_t)num_vectors * VECTOR_DIMS * sizeof(float);
    uint64_t tree_offset    = labels_offset +
                              (uint64_t)num_vectors * sizeof(uint8_t);
    uint64_t ivf_hdr_offset = tree_offset +
                              (uint64_t)tree.num_nodes * sizeof(vptree_node_t);

    /* IVF data offsets (after ivf section header) */
    uint64_t ivf_data_start    = ivf_hdr_offset + sizeof(ivf_section_header_t);
    uint64_t ivf_cent_offset   = ivf_data_start;
    uint64_t ivf_off_offset    = ivf_cent_offset + (uint64_t)IVF_NUM_CLUSTERS * VECTOR_DIMS * sizeof(float);
    uint64_t ivf_sidx_offset   = ivf_off_offset + (uint64_t)(IVF_NUM_CLUSTERS + 1) * sizeof(uint32_t);
    uint64_t ivf_qvec_offset   = 0;  /* row-major omitted — col-major is canonical */
    uint64_t ivf_col_offset    = ivf_sidx_offset + (uint64_t)num_vectors * sizeof(uint32_t);
    uint64_t ivf_bbox_min_off  = (ivf_col_data != NULL)
        ? ivf_col_offset + (uint64_t)VECTOR_DIMS * num_vectors * sizeof(int16_t)
        : ivf_col_offset;
    uint64_t ivf_bbox_max_off  = (ivf_bbox_min != NULL)
        ? ivf_bbox_min_off + (uint64_t)IVF_NUM_CLUSTERS * VECTOR_DIMS * sizeof(int16_t)
        : ivf_bbox_min_off;

    /* Total size includes col_data and bbox sections if present */
    size_t total_size = (size_t)ivf_bbox_max_off;
    if (ivf_bbox_max != NULL)
        total_size += (size_t)IVF_NUM_CLUSTERS * VECTOR_DIMS * sizeof(int16_t);

    fprintf(stderr, "Escrevendo '%s' (%.1f MB)...\n", output_path,
            (double)total_size / (1024.0 * 1024.0));

    /* Escreve arquivo */
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "Erro: nao foi possivel criar '%s'\n", output_path);
        vptree_free(&tree);
        free(vectors);
        free(labels);
        return 1;
    }

    /* Header */
    index_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic              = INDEX_MAGIC;
    hdr.version            = INDEX_VERSION;
    hdr.num_vectors        = (uint32_t)num_vectors;
    hdr.dims               = (uint32_t)VECTOR_DIMS;
    hdr.vectors_offset     = vectors_offset;
    hdr.labels_offset      = labels_offset;
    hdr.tree_offset        = tree_offset;
    hdr.tree_num_nodes     = (uint32_t)tree.num_nodes;
    hdr.ivf_section_offset = ivf_hdr_offset;

    if (fwrite(&hdr, sizeof(hdr), 1, out) != 1) {
        fprintf(stderr, "Erro: falha ao escrever header\n");
        fclose(out);
        vptree_free(&tree);
        free(vectors);
        free(labels);
        return 1;
    }

    /* Vetores */
    size_t vbytes = num_vectors * VECTOR_DIMS * sizeof(float);
    if (fwrite(vectors, 1, vbytes, out) != vbytes) {
        fprintf(stderr, "Erro: falha ao escrever vetores\n");
        fclose(out);
        vptree_free(&tree);
        free(vectors);
        free(labels);
        return 1;
    }

    /* Labels */
    size_t lbytes = num_vectors * sizeof(uint8_t);
    if (fwrite(labels, 1, lbytes, out) != lbytes) {
        fprintf(stderr, "Erro: falha ao escrever labels\n");
        fclose(out);
        vptree_free(&tree);
        free(vectors);
        free(labels);
        return 1;
    }

    /* VP-Tree nodes */
    size_t nbytes = (size_t)tree.num_nodes * sizeof(vptree_node_t);
    if (fwrite(tree.nodes, 1, nbytes, out) != nbytes) {
        fprintf(stderr, "Erro: falha ao escrever nodes VP-Tree\n");
        fclose(out);
        vptree_free(&tree);
        free(vectors);
        free(labels);
        return 1;
    }

    /* IVF Section Header */
    ivf_section_header_t ivf_hdr_s;
    memset(&ivf_hdr_s, 0, sizeof(ivf_hdr_s));
    ivf_hdr_s.num_clusters      = IVF_NUM_CLUSTERS;
    ivf_hdr_s.nprobe            = IVF_NPROBE_DEFAULT;
    ivf_hdr_s.centroids_offset  = ivf_cent_offset;
    ivf_hdr_s.offsets_offset    = ivf_off_offset;
    ivf_hdr_s.sorted_idx_offset = ivf_sidx_offset;
    ivf_hdr_s.quant_vec_offset  = ivf_qvec_offset;
    ivf_hdr_s.col_data_offset   = (ivf_col_data  != NULL) ? ivf_col_offset   : 0;
    ivf_hdr_s.bbox_min_offset   = (ivf_bbox_min  != NULL) ? ivf_bbox_min_off : 0;
    ivf_hdr_s.bbox_max_offset   = (ivf_bbox_max  != NULL) ? ivf_bbox_max_off : 0;

    fwrite(&ivf_hdr_s, sizeof(ivf_hdr_s), 1, out);

    /* Centroids */
    fwrite(ivf_centroids, sizeof(float), IVF_NUM_CLUSTERS * VECTOR_DIMS, out);

    /* Cluster offsets */
    fwrite(ivf_offsets, sizeof(uint32_t), IVF_NUM_CLUSTERS + 1, out);

    /* Sorted indices (original dataset indices sorted by cluster) */
    fwrite(ivf_sorted_idx, sizeof(uint32_t), num_vectors, out);

    /* Row-major quant_vec OMITTED — col-major is canonical, saves 84MB */

    /* Column-major layout (canonical format for scan) */
    if (ivf_col_data != NULL) {
        fprintf(stderr, "Escrevendo col_data (%.1f MB)...\n",
                (double)((size_t)VECTOR_DIMS * num_vectors * sizeof(int16_t)) / (1024.0 * 1024.0));
        fwrite(ivf_col_data, sizeof(int16_t), (size_t)VECTOR_DIMS * num_vectors, out);
        free(ivf_col_data);
    }

    /* Bounding boxes per cluster (optional) */
    if (ivf_bbox_min != NULL) {
        fprintf(stderr, "Escrevendo bbox_min/bbox_max (%.1f KB cada)...\n",
                (double)bbox_bytes / 1024.0);
        fwrite(ivf_bbox_min, sizeof(int16_t), (size_t)IVF_NUM_CLUSTERS * VECTOR_DIMS, out);
        free(ivf_bbox_min);
    }
    if (ivf_bbox_max != NULL) {
        fwrite(ivf_bbox_max, sizeof(int16_t), (size_t)IVF_NUM_CLUSTERS * VECTOR_DIMS, out);
        free(ivf_bbox_max);
    }

    fclose(out);

    free(ivf_centroids);
    free(ivf_sorted_idx);
    free(ivf_offsets);
    free(ivf_quant_vec);

    clock_t t4 = clock();
    fprintf(stderr, "index.bin gerado em %.1f segundos total\n",
            (double)(t4 - t0) / CLOCKS_PER_SEC);
    fprintf(stderr, "\nEstatisticas finais:\n");
    fprintf(stderr, "  Vetores:    %zu\n", num_vectors);
    fprintf(stderr, "  Fraudes:    %zu (%.1f%%)\n",
            num_fraud, 100.0 * (double)num_fraud / (double)num_vectors);
    fprintf(stderr, "  Legitimas:  %zu (%.1f%%)\n",
            num_legit, 100.0 * (double)num_legit / (double)num_vectors);
    fprintf(stderr, "  Nodes VP-T: %d\n", tree.num_nodes);
    fprintf(stderr, "  Tamanho:    %.1f MB\n",
            (double)total_size / (1024.0 * 1024.0));

    vptree_free(&tree);
    free(vectors);
    free(labels);
    return 0;
}
