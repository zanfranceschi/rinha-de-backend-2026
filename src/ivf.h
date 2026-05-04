#ifndef IVF_H
#define IVF_H

#include <stdint.h>
#include "vectorize.h"
#include "vptree.h"  /* for neighbor_t */

#define IVF_NUM_CLUSTERS   4096
#define IVF_NPROBE_DEFAULT  40
#define IVF_KMEANS_ITERS    15

/*
 * IVF index — pointers into mmap'd memory (no ownership).
 *
 * Layout in index.bin (after the VP-Tree section):
 *   centroids      : float32[num_clusters * VECTOR_DIMS]
 *   cluster_offsets: uint32[num_clusters + 1]
 *   sorted_indices : uint32[num_vectors]
 *   quant_vec      : int16[num_vectors][VECTOR_DIMS]  row-major, by cluster
 *   col_data       : int16[VECTOR_DIMS][num_vectors]  col-major, same sort (optional)
 *   bbox_min       : int16[num_clusters][VECTOR_DIMS] per-cluster min (optional)
 *   bbox_max       : int16[num_clusters][VECTOR_DIMS] per-cluster max (optional)
 */
typedef struct {
    int num_vectors;
    int num_clusters;
    int nprobe;

    /* Centroids: float32[num_clusters][VECTOR_DIMS] */
    const float    *centroids;

    /*
     * cluster_offsets: uint32[num_clusters + 1]
     * Cluster i contains vectors at positions
     *   sorted_indices[offsets[i]] .. sorted_indices[offsets[i+1]-1]
     */
    const uint32_t *cluster_offsets;

    /*
     * Original indices sorted by cluster (maps sorted position to original index).
     * uint32[num_vectors]
     */
    const uint32_t *sorted_indices;

    /* int16 quantized vectors sorted by cluster — row-major, cache-friendly scan.
     * int16[num_vectors][VECTOR_DIMS], scale=10000 */
    const int16_t  *quantized_vectors;

    /*
     * Column-major layout: col_data[d] points to all quantized values for
     * dimension d across all vectors (sorted by cluster).
     * int16[VECTOR_DIMS][num_vectors] — enables AVX2 to process 8 vecs/cycle.
     * NULL if not present in index.bin.
     */
    const int16_t  *col_data[VECTOR_DIMS];

    /*
     * Bounding box per cluster: bbox_min[c*VECTOR_DIMS+d], bbox_max[c*VECTOR_DIMS+d]
     * Used to compute a lower bound on the distance from query to any point in
     * cluster c. If lower_bound > tau, skip cluster entirely.
     * NULL if not present in index.bin.
     */
    const int16_t  *bbox_min;
    const int16_t  *bbox_max;

    /* Original vectors (NOT sorted — accessed via sorted_indices[pos]) */
    const float    *vectors;

    /* Original labels (NOT sorted — accessed via sorted_indices[pos]) */
    const uint8_t  *labels;
} ivf_index_t;

/* =========================================================================
 * BUILD-TIME functions (used in tools/build_index.c)
 * ========================================================================= */

/*
 * ivf_kmeans — Run k-means clustering on vectors.
 *
 * Parameters:
 *   vectors          — flat array: vectors[i * VECTOR_DIMS .. (i+1)*VECTOR_DIMS-1]
 *   num_vectors      — number of vectors
 *   num_clusters     — k (256 for production, smaller for tests)
 *   max_iters        — maximum k-means iterations (IVF_KMEANS_ITERS = 20)
 *   centroids_out    — output: float32[num_clusters * VECTOR_DIMS]
 *   assignments_out  — output: uint8[num_vectors], each value in [0, num_clusters-1]
 *
 * Centroid initialization: first num_clusters vectors (simple, avoids rand).
 * Early-stop: if no assignment changed, break before max_iters.
 */
void ivf_kmeans(const float *vectors, int num_vectors,
                int num_clusters, int max_iters,
                float *centroids_out,
                uint16_t *assignments_out);

/*
 * ivf_sort_by_cluster — Scatter vectors into cluster order.
 *
 * Produces four parallel arrays in cluster-order plus an offsets array.
 * After this call:
 *   cluster_offsets_out[c] = start position of cluster c in sorted arrays
 *   cluster_offsets_out[num_clusters] = num_vectors (sentinel)
 */
void ivf_sort_by_cluster(const float *vectors, const uint8_t *labels,
                         const uint16_t *assignments, int num_vectors,
                         int num_clusters,
                         float    *sorted_vectors_out,  /* [num_vectors * VECTOR_DIMS] */
                         uint8_t  *sorted_labels_out,   /* [num_vectors] */
                         uint32_t *sorted_indices_out,  /* [num_vectors] */
                         uint32_t *cluster_offsets_out); /* [num_clusters + 1] */

/* =========================================================================
 * SEARCH-TIME functions (used at runtime)
 * ========================================================================= */

/*
 * ivf_search — Search k nearest neighbors using IVF.
 *
 * Steps:
 *   1. Compute squared distance from query to all num_clusters centroids.
 *   2. Select top-nprobe closest centroids.
 *   3. For each selected cluster, scan all vectors sequentially.
 *   4. Maintain a max-heap of k nearest candidates (array of k elements).
 *
 * Returns number of neighbors found (should equal k when num_vectors >= k).
 * Results are NOT guaranteed to be sorted (sort with qsort if needed).
 *
 * distance stored in results is the full Euclidean distance (not squared),
 * consistent with VP-Tree neighbor_t convention.
 */
int ivf_search(const ivf_index_t *index, const float query[VECTOR_DIMS],
               int k, neighbor_t *results);

#endif /* IVF_H */
