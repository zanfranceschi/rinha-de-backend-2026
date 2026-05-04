/*
 * ivf.c — Inverted File Index (IVF) for approximate KNN search
 *
 * Optimizations (in order of impact):
 *   1. Column-major layout: col_data[d][vec_idx] — AVX2 processes 8 vecs/cycle
 *   2. Bounding box pruning: exact lower bound per cluster, skip if > tau
 *   3. AVX2 SIMD: 8x int16 vectors per cycle using _mm256 intrinsics
 *      (guarded by #ifdef __AVX2__ — falls back to scalar on non-AVX2)
 *
 * Regras criticas:
 *   - distance_sq() para comparacoes internas (sem sqrt, ordem preservada)
 *   - distance_euclidean() no resultado final (consistente com neighbor_t)
 *   - Float32 SEMPRE
 *   - Sentinel -1.0f participa normalmente no calculo de distancia
 *   - num_clusters <= 65535 para caber em uint16_t assignments
 *     (IVF_NUM_CLUSTERS=4096 usa valores 0..4095 — OK para uint16_t)
 */

#include "ivf.h"
#include "distance.h"
#include "quantize.h"

#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <stdint.h>

/* AVX2 support: only on x86_64 with AVX2 capability */
#if defined(__AVX2__) && defined(__x86_64__)
#include <immintrin.h>
#define HAVE_AVX2 1
#else
#define HAVE_AVX2 0
#endif

/* =========================================================================
 * ivf_kmeans
 *
 * Simple Lloyd's algorithm:
 *   Init: centroids[c] = vectors[c * VECTOR_DIMS]  (first N vectors)
 *   Repeat up to max_iters:
 *     Assign: each vector → nearest centroid (by squared distance)
 *     Update: centroid = mean of assigned vectors
 *     If no assignment changed, stop early.
 *
 * Note: num_clusters must be <= 65535 (uint16_t assignment range).
 * ========================================================================= */
void ivf_kmeans(const float *vectors, int num_vectors,
                int num_clusters, int max_iters,
                float *centroids_out,
                uint16_t *assignments_out)
{
    if (!vectors || num_vectors <= 0 || num_clusters <= 0
        || !centroids_out || !assignments_out) return;

    /* Clamp num_clusters to available vectors */
    if (num_clusters > num_vectors) num_clusters = num_vectors;

    /* ------------------------------------------------------------------
     * Initialization: use the first num_clusters vectors as centroids.
     * ------------------------------------------------------------------ */
    memcpy(centroids_out,
           vectors,
           (size_t)num_clusters * VECTOR_DIMS * sizeof(float));

    /* Initialize all assignments to 0 */
    memset(assignments_out, 0, (size_t)num_vectors * sizeof(uint16_t));

    /* Accumulators for centroid update */
    float    *sums  = (float *)calloc((size_t)num_clusters * VECTOR_DIMS,
                                       sizeof(float));
    uint32_t *counts = (uint32_t *)calloc((size_t)num_clusters,
                                           sizeof(uint32_t));
    if (!sums || !counts) {
        free(sums);
        free(counts);
        return;
    }

    for (int iter = 0; iter < max_iters; iter++) {
        int changed = 0;

        /* ----------------------------------------------------------------
         * Assignment step: assign each vector to its nearest centroid.
         * Use distance_sq (no sqrt needed — order preserved).
         * ---------------------------------------------------------------- */
        for (int i = 0; i < num_vectors; i++) {
            const float *v = vectors + (size_t)i * VECTOR_DIMS;

            float    best_dist = FLT_MAX;
            uint16_t best_c    = 0;

            for (int c = 0; c < num_clusters; c++) {
                const float *cen = centroids_out + (size_t)c * VECTOR_DIMS;
                float d = distance_sq(v, cen);
                if (d < best_dist) {
                    best_dist = d;
                    best_c    = (uint16_t)c;
                }
            }

            if (assignments_out[i] != best_c) {
                assignments_out[i] = best_c;
                changed++;
            }
        }

        /* Early stop: no changes */
        if (changed == 0) break;

        /* ----------------------------------------------------------------
         * Update step: recompute each centroid as mean of assigned vectors.
         * Empty clusters keep their previous centroid.
         * ---------------------------------------------------------------- */
        memset(sums,   0, (size_t)num_clusters * VECTOR_DIMS * sizeof(float));
        memset(counts, 0, (size_t)num_clusters * sizeof(uint32_t));

        for (int i = 0; i < num_vectors; i++) {
            int c = (int)assignments_out[i];
            const float *v = vectors + (size_t)i * VECTOR_DIMS;
            float *s = sums + (size_t)c * VECTOR_DIMS;
            for (int d = 0; d < VECTOR_DIMS; d++) s[d] += v[d];
            counts[c]++;
        }

        for (int c = 0; c < num_clusters; c++) {
            if (counts[c] == 0) continue;  /* keep previous centroid */
            float  *cen = centroids_out + (size_t)c * VECTOR_DIMS;
            float  *s   = sums          + (size_t)c * VECTOR_DIMS;
            float   inv = 1.0f / (float)counts[c];
            for (int d = 0; d < VECTOR_DIMS; d++) cen[d] = s[d] * inv;
        }
    }

    free(sums);
    free(counts);
}

/* =========================================================================
 * ivf_sort_by_cluster
 *
 * Scatter vectors, labels, and original indices into cluster-order.
 * Builds cluster_offsets[] using a two-pass counting-sort approach.
 * ========================================================================= */
void ivf_sort_by_cluster(const float *vectors, const uint8_t *labels,
                         const uint16_t *assignments, int num_vectors,
                         int num_clusters,
                         float    *sorted_vectors_out,
                         uint8_t  *sorted_labels_out,
                         uint32_t *sorted_indices_out,
                         uint32_t *cluster_offsets_out)
{
    if (!vectors || !labels || !assignments || num_vectors <= 0
        || num_clusters <= 0 || !sorted_vectors_out || !sorted_labels_out
        || !sorted_indices_out || !cluster_offsets_out) return;

    /* Pass 1: count vectors per cluster */
    memset(cluster_offsets_out, 0, (size_t)(num_clusters + 1) * sizeof(uint32_t));

    for (int i = 0; i < num_vectors; i++) {
        int c = (int)assignments[i];
        cluster_offsets_out[c]++;
    }

    /* Compute prefix sums → cluster_offsets_out[c] = start of cluster c */
    uint32_t running = 0;
    for (int c = 0; c < num_clusters; c++) {
        uint32_t count = cluster_offsets_out[c];
        cluster_offsets_out[c] = running;
        running += count;
    }
    cluster_offsets_out[num_clusters] = (uint32_t)num_vectors;

    /* Pass 2: scatter each vector into its cluster slot.
     * Use a temporary write-cursor array to track the next write position
     * within each cluster. */
    uint32_t *cursors = (uint32_t *)malloc((size_t)num_clusters
                                            * sizeof(uint32_t));
    if (!cursors) return;

    memcpy(cursors, cluster_offsets_out,
           (size_t)num_clusters * sizeof(uint32_t));

    for (int i = 0; i < num_vectors; i++) {
        int      c   = (int)assignments[i];
        uint32_t pos = cursors[c]++;

        /* Copy vector */
        memcpy(sorted_vectors_out + (size_t)pos * VECTOR_DIMS,
               vectors             + (size_t)i   * VECTOR_DIMS,
               VECTOR_DIMS * sizeof(float));

        sorted_labels_out[pos]  = labels[i];
        sorted_indices_out[pos] = (uint32_t)i;
    }

    free(cursors);
}

/* =========================================================================
 * Tiny max-heap helpers for ivf_search.
 *
 * We maintain an array of k neighbors where heap[0] is the worst (max dist).
 * With k=5 a linear scan of 5 elements is fast enough — no complex heap.
 * ========================================================================= */

/* Find index of the element with maximum distance in heap[0..size-1] */
static int find_max_idx(const neighbor_t *heap, int size)
{
    int max_i = 0;
    for (int i = 1; i < size; i++) {
        if (heap[i].distance > heap[max_i].distance) max_i = i;
    }
    return max_i;
}

/* =========================================================================
 * bbox_lower_bound — exact lower bound on distance from query to cluster c.
 *
 * For each dimension d:
 *   if q[d] < bbox_min[c][d]: contribution = (bbox_min - q)^2
 *   if q[d] > bbox_max[c][d]: contribution = (q - bbox_max)^2
 *   else: contribution = 0  (query is within bbox on this dim)
 *
 * This is mathematically exact: any vector in cluster c must have distance
 * >= sqrt(bbox_lower_bound) from the query.
 *
 * Returns int64_t in int16-squared units (same as distance_sq_i16).
 * ========================================================================= */
static inline int64_t bbox_lower_bound(const int16_t q[VECTOR_DIMS],
                                        const int16_t *bbox_min,
                                        const int16_t *bbox_max,
                                        int cluster)
{
    const int16_t *bmin = bbox_min + (size_t)cluster * VECTOR_DIMS;
    const int16_t *bmax = bbox_max + (size_t)cluster * VECTOR_DIMS;
    int64_t s = 0;
    for (int d = 0; d < VECTOR_DIMS; d++) {
        int32_t diff = 0;
        if (q[d] < bmin[d])      diff = (int32_t)bmin[d] - (int32_t)q[d];
        else if (q[d] > bmax[d]) diff = (int32_t)q[d]    - (int32_t)bmax[d];
        s += (int64_t)diff * (int64_t)diff;
    }
    return s;
}

/* =========================================================================
 * Candidate heap helpers (inline for hot path)
 * ========================================================================= */
#define REFINE_K 50   /* candidates to pass from phase 1 to phase 2 */

typedef struct { uint32_t pos; int64_t dist_i16; } candidate_t;

static inline void cand_push(candidate_t *cands, int *cand_size,
                              int *worst_cand_idx, int64_t *worst_i16,
                              uint32_t pos, int64_t d)
{
    if (*cand_size < REFINE_K) {
        cands[*cand_size].pos      = pos;
        cands[*cand_size].dist_i16 = d;
        (*cand_size)++;
        if (*cand_size == REFINE_K) {
            *worst_cand_idx = 0;
            for (int j = 1; j < REFINE_K; j++)
                if (cands[j].dist_i16 > cands[*worst_cand_idx].dist_i16)
                    *worst_cand_idx = j;
            *worst_i16 = cands[*worst_cand_idx].dist_i16;
        }
    } else if (d < *worst_i16) {
        cands[*worst_cand_idx].pos      = pos;
        cands[*worst_cand_idx].dist_i16 = d;
        *worst_cand_idx = 0;
        for (int j = 1; j < REFINE_K; j++)
            if (cands[j].dist_i16 > cands[*worst_cand_idx].dist_i16)
                *worst_cand_idx = j;
        *worst_i16 = cands[*worst_cand_idx].dist_i16;
    }
}

/* =========================================================================
 * scan_cluster_avx2 — AVX2 column-major scan of one cluster.
 *
 * Processes 8 vectors simultaneously per AVX2 register.
 * For each dimension d:
 *   1. Broadcast query_i16[d] to all 8 lanes
 *   2. Load 8 int16 values from col_data[d][pos..pos+7]
 *   3. Extend to int32, subtract, square, accumulate
 *
 * After all 14 dimensions, store 8 int64 accumulators and push candidates.
 *
 * Falls back to scalar for the tail (num_in_cluster % 8 remainder).
 * ========================================================================= */
#if HAVE_AVX2

/* AVX2 column-major scan: processes 8 vectors at a time */
static void scan_cluster_avx2(const ivf_index_t *index,
                                const int16_t query_i16[VECTOR_DIMS],
                                uint32_t start, uint32_t end,
                                int64_t tau,
                                candidate_t *cands, int *cand_size,
                                int *worst_cand_idx, int64_t *worst_i16)
{
    uint32_t count = end - start;
    uint32_t vec8_end = start + (count / 8) * 8;  /* aligned to 8 */

    /* Process 8 vectors per iteration */
    for (uint32_t pos = start; pos < vec8_end; pos += 8) {
        /* Accumulate squared distances for 8 vectors across all 14 dims.
         * We need int64 to avoid overflow: 14 * 20000^2 = 5.6e9 > INT32_MAX.
         * Use two int64 accumulators: lo for lower 4, hi for upper 4 of the 8 lanes. */
        __m256i acc_lo = _mm256_setzero_si256();  /* lower 4 x int64 */
        __m256i acc_hi = _mm256_setzero_si256();  /* upper 4 x int64 */

        int can_prune = 0;

        for (int d = 0; d < VECTOR_DIMS; d++) {
            /* Broadcast query dimension to all 8 lanes (int32) */
            __m256i q_broadcast = _mm256_set1_epi32((int)query_i16[d]);

            /* Load 8 int16 values from column d at position pos */
            __m128i refs16 = _mm_loadu_si128(
                (const __m128i *)(index->col_data[d] + pos));

            /* Sign-extend int16 to int32 */
            __m256i refs32 = _mm256_cvtepi16_epi32(refs16);

            /* diff = refs - q */
            __m256i diff = _mm256_sub_epi32(refs32, q_broadcast);

            /* diff^2 (int32 x int32 -> int32, low 32 bits) */
            __m256i sq = _mm256_mullo_epi32(diff, diff);

            /* Extend sq to int64 and accumulate in lo/hi */
            /* Lower 4 elements */
            __m128i sq_lo128 = _mm256_castsi256_si128(sq);
            __m256i sq_lo64  = _mm256_cvtepi32_epi64(sq_lo128);
            acc_lo = _mm256_add_epi64(acc_lo, sq_lo64);

            /* Upper 4 elements */
            __m128i sq_hi128 = _mm256_extracti128_si256(sq, 1);
            __m256i sq_hi64  = _mm256_cvtepi32_epi64(sq_hi128);
            acc_hi = _mm256_add_epi64(acc_hi, sq_hi64);

            /* Early dimension pruning: after each dim, check if all 8 accumulators
             * already exceed tau. Extract and check. */
            if (d >= 3 && (d % 4) == 3 && *cand_size == REFINE_K) {
                /* Extract lo 4 accumulators */
                int64_t lo[4], hi[4];
                _mm256_storeu_si256((__m256i *)lo, acc_lo);
                _mm256_storeu_si256((__m256i *)hi, acc_hi);
                /* If all 8 exceed tau, prune entire group */
                if (lo[0] > tau && lo[1] > tau && lo[2] > tau && lo[3] > tau &&
                    hi[0] > tau && hi[1] > tau && hi[2] > tau && hi[3] > tau) {
                    can_prune = 1;
                    break;
                }
            }
        }

        if (can_prune) continue;

        /* Extract final accumulated distances for all 8 vectors */
        int64_t lo[4], hi[4];
        _mm256_storeu_si256((__m256i *)lo, acc_lo);
        _mm256_storeu_si256((__m256i *)hi, acc_hi);

        /* Push candidates for each of the 8 vectors */
        int64_t cur_tau = (*cand_size < REFINE_K) ? INT64_MAX : *worst_i16;
        if (lo[0] <= cur_tau) cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos + 0, lo[0]);
        if (lo[1] <= cur_tau) cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos + 1, lo[1]);
        if (lo[2] <= cur_tau) cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos + 2, lo[2]);
        if (lo[3] <= cur_tau) cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos + 3, lo[3]);
        if (hi[0] <= cur_tau) cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos + 4, hi[0]);
        if (hi[1] <= cur_tau) cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos + 5, hi[1]);
        if (hi[2] <= cur_tau) cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos + 6, hi[2]);
        if (hi[3] <= cur_tau) cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos + 7, hi[3]);
    }

    /* Scalar tail: remaining < 8 vectors — reconstruct from col_data */
    for (uint32_t pos = vec8_end; pos < end; pos++) {
        int64_t cur_tau = (*cand_size < REFINE_K) ? INT64_MAX : *worst_i16;
        int64_t d = 0;
        for (int dd = 0; dd < VECTOR_DIMS; dd++) {
            int32_t diff = (int32_t)query_i16[dd] - (int32_t)index->col_data[dd][pos];
            d += (int64_t)diff * (int64_t)diff;
            if (d > cur_tau) goto avx2_tail_skip;
        }
        cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos, d);
        avx2_tail_skip:;
    }
}

#endif /* HAVE_AVX2 */

/* =========================================================================
 * scan_cluster_scalar — Scalar int16 scan with early exit (row-major).
 *
 * Same algorithm as before: iterate over vectors in row-major order,
 * use distance_sq_i16 with tau for per-dimension early exit.
 * ========================================================================= */
static void scan_cluster_scalar(const ivf_index_t *index,
                                 const int16_t query_i16[VECTOR_DIMS],
                                 uint32_t start, uint32_t end,
                                 candidate_t *cands, int *cand_size,
                                 int *worst_cand_idx, int64_t *worst_i16)
{
    int use_col = (index->col_data[0] != NULL);
    for (uint32_t pos = start; pos < end; pos++) {
        int64_t tau = (*cand_size < REFINE_K) ? INT64_MAX : *worst_i16;
        int64_t d;
        if (use_col) {
            /* Column-major: reconstruct per-dim with early exit */
            d = 0;
            for (int dd = 0; dd < VECTOR_DIMS; dd++) {
                int32_t diff = (int32_t)query_i16[dd] - (int32_t)index->col_data[dd][pos];
                d += (int64_t)diff * (int64_t)diff;
                if (d > tau) goto scalar_skip;
            }
        } else {
            /* Row-major fallback */
            const int16_t *qv = index->quantized_vectors + (size_t)pos * VECTOR_DIMS;
            d = distance_sq_i16(query_i16, qv, tau);
        }
        if (d <= tau)
            cand_push(cands, cand_size, worst_cand_idx, worst_i16, pos, d);
        scalar_skip:;
    }
}

/* =========================================================================
 * ivf_search
 * ========================================================================= */
int ivf_search(const ivf_index_t *index, const float query[VECTOR_DIMS],
               int k, neighbor_t *results)
{
    if (!index || !query || k <= 0 || !results
        || index->num_vectors <= 0 || index->num_clusters <= 0) return 0;

    int num_clusters = index->num_clusters;
    int nprobe       = index->nprobe;
    if (nprobe > num_clusters) nprobe = num_clusters;
    if (nprobe <= 0)           nprobe = IVF_NPROBE_DEFAULT;

    /* ------------------------------------------------------------------
     * Step 1: compute squared distance from query to all centroids.
     * Stack-allocated buffers — no malloc per request.
     * 4096 clusters × 4 bytes = 16KB each — safe on stack.
     * ------------------------------------------------------------------ */
    float centroid_dists[IVF_NUM_CLUSTERS];
    int   centroid_order[IVF_NUM_CLUSTERS];
    if (num_clusters > IVF_NUM_CLUSTERS) num_clusters = IVF_NUM_CLUSTERS;

    for (int c = 0; c < num_clusters; c++) {
        const float *cen = index->centroids + (size_t)c * VECTOR_DIMS;
        centroid_dists[c] = distance_sq(query, cen);
        centroid_order[c] = c;
    }

    /* ------------------------------------------------------------------
     * Step 2: partial selection sort to find top-nprobe closest centroids.
     * O(num_clusters * nprobe) — with 4096 clusters and nprobe=40: 163K ops.
     * ------------------------------------------------------------------ */
    for (int p = 0; p < nprobe; p++) {
        int min_i = p;
        for (int c = p + 1; c < num_clusters; c++) {
            if (centroid_dists[centroid_order[c]]
                < centroid_dists[centroid_order[min_i]]) {
                min_i = c;
            }
        }
        int tmp               = centroid_order[p];
        centroid_order[p]     = centroid_order[min_i];
        centroid_order[min_i] = tmp;
    }

    /* ------------------------------------------------------------------
     * Step 3: TWO-PHASE SEARCH
     *
     * Phase 1: int16 quantized scan.
     *   - Bbox pruning: skip cluster entirely if lower bound > tau.
     *   - Column-major + AVX2 scan (if available) OR scalar row-major scan.
     *   - Collect REFINE_K candidates.
     *
     * Phase 2: float32 refinement.
     *   - Compute exact float32 distance for top candidates.
     *   - Return top-k.
     * ------------------------------------------------------------------ */

    /* Quantize query to int16 (once, reused across all clusters) */
    int16_t query_i16[VECTOR_DIMS];
    quantize_f32_to_i16(query, query_i16);

    /* Determine which scan path to use */
    int use_quant  = (index->quantized_vectors != NULL);
#if HAVE_AVX2
    int use_colmaj = (index->col_data[0] != NULL);
#endif
    int use_bbox   = (index->bbox_min != NULL && index->bbox_max != NULL);

    /* Phase 1 candidates heap */
    candidate_t candidates[REFINE_K]; /* 50 × 12 bytes = 600 bytes on stack */
    int   cand_size      = 0;
    int64_t worst_i16    = INT64_MAX;
    int   worst_cand_idx = 0;

    for (int p = 0; p < nprobe; p++) {
        int      c     = centroid_order[p];
        uint32_t start = index->cluster_offsets[c];
        uint32_t end   = index->cluster_offsets[c + 1];

        if (start == end) continue;  /* empty cluster */

        /* ---- Bounding box pruning ---- */
        if (use_bbox && cand_size == REFINE_K) {
            int64_t lb = bbox_lower_bound(query_i16, index->bbox_min,
                                           index->bbox_max, c);
            if (lb > worst_i16) continue;  /* exact skip — no false negatives */
        }

        if (use_quant) {
#if HAVE_AVX2
            if (use_colmaj && (end - start) >= 8) {
                /* AVX2 column-major scan */
                scan_cluster_avx2(index, query_i16, start, end,
                                   worst_i16,
                                   candidates, &cand_size,
                                   &worst_cand_idx, &worst_i16);
            } else {
                /* Scalar row-major scan with early exit */
                scan_cluster_scalar(index, query_i16, start, end,
                                     candidates, &cand_size,
                                     &worst_cand_idx, &worst_i16);
            }
#else
            /* No AVX2: scalar row-major scan */
            scan_cluster_scalar(index, query_i16, start, end,
                                 candidates, &cand_size,
                                 &worst_cand_idx, &worst_i16);
#endif
        } else {
            /* ---- float32 fallback (no quantized vectors) ---- */
            for (uint32_t pos = start; pos < end; pos++) {
                uint32_t orig_idx = index->sorted_indices[pos];
                const float *v = index->vectors + (size_t)orig_idx * VECTOR_DIMS;
                float d_sq = distance_sq(query, v);
                int64_t d_i16 = (int64_t)(d_sq * (float)(QUANT_SCALE) * (float)(QUANT_SCALE));

                int64_t cur_tau = (cand_size < REFINE_K) ? INT64_MAX : worst_i16;
                if (d_i16 <= cur_tau)
                    cand_push(candidates, &cand_size, &worst_cand_idx, &worst_i16, pos, d_i16);
            }
        }
    }

    /* ------------------------------------------------------------------
     * Phase 2: float32 refinement of top candidates → final k results.
     * ------------------------------------------------------------------ */
    int   heap_size  = 0;
    float worst_dist = FLT_MAX;
    int   worst_idx  = 0;

    for (int i = 0; i < cand_size; i++) {
        uint32_t pos      = candidates[i].pos;
        uint32_t orig_idx = index->sorted_indices[pos];
        const float *v    = index->vectors + (size_t)orig_idx * VECTOR_DIMS;
        float d_sq = distance_sq(query, v);

        if (heap_size < k) {
            float d = sqrtf(d_sq);
            results[heap_size].index    = (int)orig_idx;
            results[heap_size].distance = d;
            heap_size++;
            if (heap_size == k) {
                worst_idx  = find_max_idx(results, k);
                worst_dist = results[worst_idx].distance;
            }
        } else {
            float wd_sq = worst_dist * worst_dist;
            if (d_sq < wd_sq) {
                float d = sqrtf(d_sq);
                results[worst_idx].index    = (int)orig_idx;
                results[worst_idx].distance = d;
                worst_idx  = find_max_idx(results, k);
                worst_dist = results[worst_idx].distance;
            }
        }
    }

    return heap_size;

#undef REFINE_K
}
