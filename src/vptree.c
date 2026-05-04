/*
 * vptree.c — Implementacao GREEN da VP-Tree para KNN exato
 *
 * Algoritmo: Vantage Point Tree (VP-Tree)
 *   - Busca exata (100% recall) — equivalente a brute force euclidiano
 *   - O(log N) medio por query
 *   - Nodes armazenados em array flat pre-alocado (zero reallocacao)
 *
 * Regras criticas:
 *   - Usa distance_euclidean (com sqrt) — VP-Tree requer metrica triangular
 *   - Sentinel -1.0f nas dims 5 e 6 participa normalmente
 *   - Float32 em tudo
 *
 * Estrutura de build:
 *   1. Ordena o subset de pontos por distancia ao vantage point
 *   2. left  = primeira metade (dist <= mediana)
 *   3. right = segunda metade  (dist > mediana)
 *   4. radius = distancia maxima do left subset (nao a mediana pura)
 *      Isso garante que TODOS os pontos em left tenham dist(p,vp) <= radius
 *
 * Pruning de busca:
 *   Dado query q com d = dist(q, vp) e tau = pior dist no heap:
 *   - left:  precisa buscar se d - tau <= radius
 *   - right: precisa buscar se d + tau >= radius
 *   (ambos com >= para tratar fronteira de forma conservadora)
 */

#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include "vptree.h"
#include "distance.h"

/* =========================================================================
 * Max-heap de k vizinhos — mantem os k menores (pior = raiz do max-heap)
 * ========================================================================= */

static void heap_sift_down(neighbor_t *heap, int size, int i)
{
    while (1) {
        int largest = i;
        int left    = 2 * i + 1;
        int right   = 2 * i + 2;

        if (left < size && heap[left].distance > heap[largest].distance)
            largest = left;
        if (right < size && heap[right].distance > heap[largest].distance)
            largest = right;

        if (largest == i) break;

        neighbor_t tmp  = heap[i];
        heap[i]         = heap[largest];
        heap[largest]   = tmp;
        i               = largest;
    }
}

static void heap_sift_up(neighbor_t *heap, int i)
{
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap[i].distance <= heap[parent].distance) break;
        neighbor_t tmp   = heap[i];
        heap[i]          = heap[parent];
        heap[parent]     = tmp;
        i                = parent;
    }
}

static void heap_try_insert(neighbor_t *heap, int *heap_size, int k,
                             int idx, float dist)
{
    if (*heap_size < k) {
        heap[*heap_size].index    = idx;
        heap[*heap_size].distance = dist;
        (*heap_size)++;
        heap_sift_up(heap, *heap_size - 1);
    } else if (dist < heap[0].distance) {
        heap[0].index    = idx;
        heap[0].distance = dist;
        heap_sift_down(heap, k, 0);
    }
}

/* Retorna FLT_MAX se heap nao esta cheio, senao a pior distancia (raiz) */
static float heap_worst(const neighbor_t *heap, int heap_size, int k)
{
    if (heap_size < k) return FLT_MAX;
    return heap[0].distance;
}

/* =========================================================================
 * Ordenacao simples por distancia (insertion sort — rapido para N pequeno)
 * Para N grande (3M vetores), o build usa qsort da stdlib.
 * ========================================================================= */

typedef struct {
    int   global_idx;
    float dist;
} dist_pair_t;

static int cmp_dist_pair(const void *a, const void *b)
{
    float da = ((const dist_pair_t *)a)->dist;
    float db = ((const dist_pair_t *)b)->dist;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

/* =========================================================================
 * Contexto de build
 * ========================================================================= */
typedef struct {
    float        *vectors;
    vptree_node_t *nodes;
    int           num_nodes;
} build_ctx_t;

/* =========================================================================
 * Escolha do vantage point: melhor de 3 candidatos por variancia maxima
 * ========================================================================= */
static float compute_variance_for_vp(const float *vectors,
                                      const int *indices, int n,
                                      int vp_global)
{
    if (n <= 1) return 0.0f;
    const float *vp = vectors + (size_t)vp_global * VECTOR_DIMS;
    int samples = n < 8 ? n : 8;
    int step    = n / samples;
    if (step < 1) step = 1;

    float sum = 0.0f, sum2 = 0.0f;
    int count = 0;
    for (int i = 0; i < n && count < samples; i += step) {
        if (indices[i] == vp_global) continue;
        float d = distance_euclidean(vp,
                      vectors + (size_t)indices[i] * VECTOR_DIMS);
        sum  += d;
        sum2 += d * d;
        count++;
    }
    if (count < 2) return 0.0f;
    float mean = sum / (float)count;
    float var  = (sum2 / (float)count) - (mean * mean);
    return var < 0.0f ? 0.0f : var;
}

static int choose_vantage_point(const float *vectors,
                                 const int *indices, int n)
{
    if (n == 1) return 0;
    int nc = n < 3 ? n : 3;
    int cands[3] = {0, n / 2, n - 1};
    int best  = cands[0];
    float bv  = compute_variance_for_vp(vectors, indices, n,
                                         indices[cands[0]]);
    for (int c = 1; c < nc; c++) {
        float v = compute_variance_for_vp(vectors, indices, n,
                                           indices[cands[c]]);
        if (v > bv) { bv = v; best = cands[c]; }
    }
    return best;
}

/* =========================================================================
 * build_recursive
 *
 * Cada node representa exatamente 1 ponto (o vantage point).
 * O radius e o maximo das distancias do vp aos pontos do left subset,
 * garantindo que TODOS os pontos em left satisfazem dist(p,vp) <= radius.
 * ========================================================================= */
static int build_recursive(build_ctx_t *ctx, int *indices, int n)
{
    if (n <= 0) return -1;

    int node_idx = ctx->num_nodes++;
    vptree_node_t *node = &ctx->nodes[node_idx];

    /* Caso base: unico ponto */
    if (n == 1) {
        node->vantage_idx = indices[0];
        node->radius      = 0.0f;
        node->left_child  = -1;
        node->right_child = -1;
        return node_idx;
    }

    /* Escolhe e posiciona vantage point em indices[0] */
    int vp_local  = choose_vantage_point(ctx->vectors, indices, n);
    int vp_global = indices[vp_local];
    indices[vp_local] = indices[0];
    indices[0]        = vp_global;

    node->vantage_idx = vp_global;

    int  rest_n = n - 1;
    int *rest   = indices + 1;

    if (rest_n == 0) {
        node->radius      = 0.0f;
        node->left_child  = -1;
        node->right_child = -1;
        return node_idx;
    }

    /* Calcula distancias de todos os pontos restantes ao vantage point */
    dist_pair_t *pairs = (dist_pair_t *)malloc(
        (size_t)rest_n * sizeof(dist_pair_t));
    if (!pairs) {
        /* Fallback: folha simples */
        node->radius      = 0.0f;
        node->left_child  = -1;
        node->right_child = -1;
        return node_idx;
    }

    const float *vp = ctx->vectors + (size_t)vp_global * VECTOR_DIMS;
    for (int i = 0; i < rest_n; i++) {
        pairs[i].global_idx = rest[i];
        pairs[i].dist = distance_euclidean(vp,
                            ctx->vectors + (size_t)rest[i] * VECTOR_DIMS);
    }

    /* Ordena completamente por distancia — garante particao correta */
    qsort(pairs, (size_t)rest_n, sizeof(dist_pair_t), cmp_dist_pair);

    /* Reordena rest[] na ordem ordenada */
    for (int i = 0; i < rest_n; i++) rest[i] = pairs[i].global_idx;

    /*
     * Particao: left = primeira metade, right = segunda metade.
     * radius = distancia maxima do left subset = pairs[mid].dist
     * (pois array esta ordenado, pairs[mid].dist e o maior do left)
     */
    int mid = rest_n / 2;       /* left: [0..mid], right: [mid+1..rest_n-1] */
    /* Quando rest_n e impar, left tem 1 elemento a mais que right */

    float radius = pairs[mid].dist;
    node->radius = radius;

    free(pairs);

    int left_count  = mid + 1;
    int right_count = rest_n - left_count;

    node->left_child  = -1;
    node->right_child = -1;

    if (left_count > 0)
        node->left_child = build_recursive(ctx, rest, left_count);
    if (right_count > 0)
        node->right_child = build_recursive(ctx, rest + left_count,
                                             right_count);

    return node_idx;
}

/* =========================================================================
 * vptree_build
 * ========================================================================= */
int vptree_build(vptree_t *tree, float *vectors, uint8_t *labels,
                 int num_vectors)
{
    if (!tree) return -1;

    tree->vectors     = vectors;
    tree->labels      = labels;
    tree->num_vectors = num_vectors;
    tree->nodes       = NULL;
    tree->num_nodes   = 0;

    if (num_vectors <= 0) return 0;

    /* Cada ponto gera exatamente 1 node */
    tree->nodes = (vptree_node_t *)malloc(
        (size_t)num_vectors * sizeof(vptree_node_t));
    if (!tree->nodes) return -1;

    int *indices = (int *)malloc((size_t)num_vectors * sizeof(int));
    if (!indices) {
        free(tree->nodes);
        tree->nodes = NULL;
        return -1;
    }

    for (int i = 0; i < num_vectors; i++) indices[i] = i;

    build_ctx_t ctx;
    ctx.vectors   = vectors;
    ctx.nodes     = tree->nodes;
    ctx.num_nodes = 0;

    build_recursive(&ctx, indices, num_vectors);

    tree->num_nodes = ctx.num_nodes;
    free(indices);
    return 0;
}

/* =========================================================================
 * search_recursive
 *
 * Invariante de busca (com radius = max dist do left subset):
 *   left  contem pontos p com dist(p, vp) <= radius
 *   right contem pontos p com dist(p, vp) >  radius
 *
 * Para query q com d = dist(q, vp) e tau = heap_worst:
 *   Entrar no left:  d - tau <= radius   (bola de busca intersecta left)
 *   Entrar no right: d + tau >  radius   (bola de busca intersecta right)
 *
 * Usamos >= nos dois lados para seguranca na fronteira.
 * ========================================================================= */
static void search_recursive(const vptree_t *tree, const float *query,
                              int k, neighbor_t *heap, int *heap_size,
                              int node_idx)
{
    if (node_idx < 0 || node_idx >= tree->num_nodes) return;

    const vptree_node_t *node = &tree->nodes[node_idx];

    /* Distancia do query ao vantage point */
    const float *vp = tree->vectors +
                      (size_t)node->vantage_idx * VECTOR_DIMS;
    float d = distance_euclidean(query, vp);

    /* Tenta inserir o vantage point */
    heap_try_insert(heap, heap_size, k, node->vantage_idx, d);

    /* Folha pura: sem filhos */
    if (node->left_child < 0 && node->right_child < 0) return;

    float tau    = heap_worst(heap, *heap_size, k);
    float radius = node->radius;

    /*
     * Visita primeiro o lado mais provavel (onde o query esta),
     * depois o outro lado se necessario.
     *
     * Condicao LEFT:  a bola [d-tau, d+tau] intersecta [0, radius]
     *                 <=> d - tau <= radius  (sempre verdade se tau=FLT_MAX)
     *
     * Condicao RIGHT: a bola intersecta (radius, +inf)
     *                 <=> d + tau > radius   (sempre verdade se tau=FLT_MAX)
     *                 usamos >= para tratar fronteira
     */
    int visit_left_first = (d <= radius);

    if (visit_left_first) {
        if (node->left_child >= 0) {
            /* LEFT: d - tau <= radius, i.e., tau >= d - radius */
            if (*heap_size < k || (d - tau) <= radius) {
                search_recursive(tree, query, k, heap, heap_size,
                                 node->left_child);
                tau = heap_worst(heap, *heap_size, k);
            }
        }
        if (node->right_child >= 0) {
            /* RIGHT: d + tau >= radius */
            if (*heap_size < k || (d + tau) >= radius) {
                search_recursive(tree, query, k, heap, heap_size,
                                 node->right_child);
            }
        }
    } else {
        if (node->right_child >= 0) {
            if (*heap_size < k || (d + tau) >= radius) {
                search_recursive(tree, query, k, heap, heap_size,
                                 node->right_child);
                tau = heap_worst(heap, *heap_size, k);
            }
        }
        if (node->left_child >= 0) {
            if (*heap_size < k || (d - tau) <= radius) {
                search_recursive(tree, query, k, heap, heap_size,
                                 node->left_child);
            }
        }
    }
}

/* =========================================================================
 * vptree_search
 * ========================================================================= */
int vptree_search(const vptree_t *tree, const float query[VECTOR_DIMS],
                  int k, neighbor_t *results)
{
    if (!tree || !results || k <= 0 || tree->num_nodes == 0) return 0;

    int heap_size = 0;
    search_recursive(tree, query, k, results, &heap_size, 0);
    return heap_size;
}

/* =========================================================================
 * vptree_free
 * ========================================================================= */
void vptree_free(vptree_t *tree)
{
    if (!tree) return;
    free(tree->nodes);
    tree->nodes     = NULL;
    tree->num_nodes = 0;
}
