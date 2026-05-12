#include "rinha.h"

#include <errno.h>
#include <immintrin.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define RINHA_INDEX_MAGIC 0x31484649u
#define RINHA_QUANT_SCALE 10000.0f

typedef struct {
    float *vectors;
    uint8_t *labels;
    uint32_t len;
    uint32_t cap;
} raw_refs;

typedef struct {
    uint64_t dist[5];
    uint8_t label[5];
    uint32_t id[5];
    uint32_t worst;
} top5;

static void top5_init(top5 *top) {
    for (int i = 0; i < 5; ++i) {
        top->dist[i] = UINT64_MAX;
        top->label[i] = 0;
        top->id[i] = UINT32_MAX;
    }
    top->worst = 0;
}

static void top5_refresh(top5 *top) {
    top->worst = 0;
    for (uint32_t i = 1; i < 5; ++i) {
        if (top->dist[i] > top->dist[top->worst] ||
            (top->dist[i] == top->dist[top->worst] && top->id[i] > top->id[top->worst])) {
            top->worst = i;
        }
    }
}

static void top5_insert(top5 *top, uint64_t dist, uint8_t label, uint32_t id) {
    uint32_t worst = top->worst;
    if (dist > top->dist[worst] || (dist == top->dist[worst] && id >= top->id[worst])) return;
    top->dist[worst] = dist;
    top->label[worst] = label;
    top->id[worst] = id;
    top5_refresh(top);
}

static uint8_t top5_frauds(const top5 *top) {
    uint8_t count = 0;
    for (int i = 0; i < 5; ++i) count = (uint8_t)(count + (top->label[i] != 0 ? 1u : 0u));
    return count;
}

static int16_t quantize(float value) {
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    long rounded = lroundf(value * RINHA_QUANT_SCALE);
    if (rounded < INT16_MIN) return INT16_MIN;
    if (rounded > INT16_MAX) return INT16_MAX;
    return (int16_t)rounded;
}

static uint64_t sqdiff_i16(int16_t a, int16_t b) {
    int64_t d = (int64_t)a - (int64_t)b;
    return (uint64_t)(d * d);
}

void rinha_index_init(rinha_index *index) {
    memset(index, 0, sizeof(*index));
}

void rinha_index_free(rinha_index *index) {
    free(index->centroids);
    free(index->bbox_min);
    free(index->bbox_max);
    free(index->offsets);
    free(index->labels);
    free(index->ids);
    free(index->blocks);
    rinha_index_init(index);
}

static int raw_reserve(raw_refs *refs, uint32_t need) {
    if (need <= refs->cap) return 1;
    uint32_t cap = refs->cap ? refs->cap : 65536;
    while (cap < need) cap *= 2;
    float *vectors = (float *)realloc(refs->vectors, (size_t)cap * RINHA_DIMS * sizeof(float));
    if (!vectors) return 0;
    uint8_t *labels = (uint8_t *)realloc(refs->labels, (size_t)cap * sizeof(uint8_t));
    if (!labels) {
        refs->vectors = vectors;
        return 0;
    }
    refs->vectors = vectors;
    refs->labels = labels;
    refs->cap = cap;
    return 1;
}

static char *read_gzip_all(const char *path, size_t *out_len) {
    gzFile file = gzopen(path, "rb");
    if (!file) return NULL;
    size_t cap = 32 * 1024 * 1024;
    size_t len = 0;
    char *data = (char *)malloc(cap + 1);
    if (!data) {
        gzclose(file);
        return NULL;
    }
    for (;;) {
        if (len + 1024 * 1024 + 1 > cap) {
            cap *= 2;
            char *next = (char *)realloc(data, cap + 1);
            if (!next) {
                free(data);
                gzclose(file);
                return NULL;
            }
            data = next;
        }
        int readn = gzread(file, data + len, 1024 * 1024);
        if (readn > 0) {
            len += (size_t)readn;
            continue;
        }
        if (readn == 0) break;
        free(data);
        gzclose(file);
        return NULL;
    }
    gzclose(file);
    data[len] = '\0';
    *out_len = len;
    return data;
}

static char *find_sub(char *begin, char *end, const char *needle) {
    size_t nlen = strlen(needle);
    for (char *p = begin; p + nlen <= end; ++p) {
        if (*p == *needle && memcmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

static int parse_refs(const char *path, uint32_t max_refs, raw_refs *refs) {
    size_t len = 0;
    char *data = read_gzip_all(path, &len);
    if (!data) return 0;
    char *cursor = data;
    char *end = data + len;
    while (cursor < end && (!max_refs || refs->len < max_refs)) {
        char *vector_key = find_sub(cursor, end, "\"vector\"");
        if (!vector_key) break;
        char *left = memchr(vector_key, '[', (size_t)(end - vector_key));
        if (!left) break;
        char *right = memchr(left, ']', (size_t)(end - left));
        if (!right) break;
        if (!raw_reserve(refs, refs->len + 1)) {
            free(data);
            return 0;
        }
        char *p = left + 1;
        float *dst = refs->vectors + (size_t)refs->len * RINHA_DIMS;
        for (uint32_t dim = 0; dim < RINHA_DIMS; ++dim) {
            while (p < right && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')) ++p;
            char *next = NULL;
            errno = 0;
            dst[dim] = strtof(p, &next);
            if (next == p || errno == ERANGE) {
                free(data);
                return 0;
            }
            p = next;
        }
        char *label_key = find_sub(right, end, "\"label\"");
        if (!label_key) break;
        char *quote = memchr(label_key, '"', (size_t)(end - label_key));
        quote = quote ? memchr(quote + 1, '"', (size_t)(end - quote - 1)) : NULL;
        quote = quote ? memchr(quote + 1, '"', (size_t)(end - quote - 1)) : NULL;
        if (!quote) break;
        ++quote;
        refs->labels[refs->len] = (quote + 5 <= end && memcmp(quote, "fraud", 5) == 0) ? 1 : 0;
        refs->len++;
        cursor = quote;
    }
    free(data);
    return refs->len >= 5;
}

static float centroid_distance_row(const float *vector, const float *centroids, uint32_t cluster) {
    const float *c = centroids + (size_t)cluster * RINHA_DIMS;
    float acc = 0.0f;
    for (uint32_t d = 0; d < RINHA_DIMS; ++d) {
        float delta = vector[d] - c[d];
        acc += delta * delta;
    }
    return acc;
}

static uint32_t nearest_centroid_row(const float *vector, const float *centroids, uint32_t clusters) {
    uint32_t best = 0;
    float best_dist = INFINITY;
    for (uint32_t c = 0; c < clusters; ++c) {
        float dist = centroid_distance_row(vector, centroids, c);
        if (dist < best_dist) {
            best_dist = dist;
            best = c;
        }
    }
    return best;
}

static int train_kmeans(const raw_refs *refs, uint32_t clusters, uint32_t sample, uint32_t iterations, float **out_centroids) {
    sample = sample < clusters ? clusters : sample;
    sample = sample > refs->len ? refs->len : sample;
    float *centroids = (float *)calloc((size_t)clusters * RINHA_DIMS, sizeof(float));
    double *sums = (double *)calloc((size_t)clusters * RINHA_DIMS, sizeof(double));
    uint32_t *counts = (uint32_t *)calloc(clusters, sizeof(uint32_t));
    if (!centroids || !sums || !counts) {
        free(centroids); free(sums); free(counts);
        return 0;
    }
    for (uint32_t c = 0; c < clusters; ++c) {
        uint32_t sample_idx = (uint32_t)(((uint64_t)c * sample) / clusters);
        uint32_t row = (uint32_t)(((uint64_t)sample_idx * refs->len) / sample);
        memcpy(centroids + (size_t)c * RINHA_DIMS, refs->vectors + (size_t)row * RINHA_DIMS, RINHA_DIMS * sizeof(float));
    }
    for (uint32_t it = 0; it < iterations; ++it) {
        memset(sums, 0, (size_t)clusters * RINHA_DIMS * sizeof(double));
        memset(counts, 0, (size_t)clusters * sizeof(uint32_t));
        for (uint32_t i = 0; i < sample; ++i) {
            uint32_t row = (uint32_t)(((uint64_t)i * refs->len) / sample);
            const float *v = refs->vectors + (size_t)row * RINHA_DIMS;
            uint32_t c = nearest_centroid_row(v, centroids, clusters);
            counts[c]++;
            double *sum = sums + (size_t)c * RINHA_DIMS;
            for (uint32_t d = 0; d < RINHA_DIMS; ++d) sum[d] += v[d];
        }
        for (uint32_t c = 0; c < clusters; ++c) {
            if (!counts[c]) continue;
            float *dst = centroids + (size_t)c * RINHA_DIMS;
            double *sum = sums + (size_t)c * RINHA_DIMS;
            double inv = 1.0 / (double)counts[c];
            for (uint32_t d = 0; d < RINHA_DIMS; ++d) dst[d] = (float)(sum[d] * inv);
        }
    }
    free(sums);
    free(counts);
    *out_centroids = centroids;
    return 1;
}

int rinha_index_build_from_gzip(const char *path, const rinha_build_options *options, rinha_index *index) {
    raw_refs refs = {0};
    uint32_t clusters = options->clusters ? options->clusters : 1280;
    uint32_t sample = options->train_sample ? options->train_sample : 65536;
    uint32_t iterations = options->iterations ? options->iterations : 6;
    if (!parse_refs(path, options->max_refs, &refs)) return 0;
    if (clusters > refs.len) clusters = refs.len;

    float *centroids_row = NULL;
    if (!train_kmeans(&refs, clusters, sample, iterations, &centroids_row)) {
        free(refs.vectors); free(refs.labels);
        return 0;
    }

    uint16_t *assign = (uint16_t *)malloc((size_t)refs.len * sizeof(uint16_t));
    uint32_t *counts = (uint32_t *)calloc(clusters, sizeof(uint32_t));
    if (!assign || !counts) {
        free(assign); free(counts); free(centroids_row); free(refs.vectors); free(refs.labels);
        return 0;
    }
    for (uint32_t row = 0; row < refs.len; ++row) {
        uint32_t c = nearest_centroid_row(refs.vectors + (size_t)row * RINHA_DIMS, centroids_row, clusters);
        assign[row] = (uint16_t)c;
        counts[c]++;
    }

    rinha_index built;
    rinha_index_init(&built);
    built.n = refs.len;
    built.clusters = clusters;
    built.offsets = (uint32_t *)calloc((size_t)clusters + 1, sizeof(uint32_t));
    if (!built.offsets) return 0;
    for (uint32_t c = 0; c < clusters; ++c) {
        built.offsets[c + 1] = built.offsets[c] + (counts[c] + RINHA_BLOCK_LANES - 1) / RINHA_BLOCK_LANES;
    }
    built.total_blocks = built.offsets[clusters];
    size_t padded = (size_t)built.total_blocks * RINHA_BLOCK_LANES;
    built.centroids = (float *)malloc((size_t)clusters * RINHA_DIMS * sizeof(float));
    built.bbox_min = (int16_t *)malloc((size_t)clusters * RINHA_DIMS * sizeof(int16_t));
    built.bbox_max = (int16_t *)malloc((size_t)clusters * RINHA_DIMS * sizeof(int16_t));
    built.labels = (uint8_t *)calloc(padded, sizeof(uint8_t));
    built.ids = (uint32_t *)malloc(padded * sizeof(uint32_t));
    built.blocks = (int16_t *)malloc((size_t)built.total_blocks * RINHA_DIMS * RINHA_BLOCK_LANES * sizeof(int16_t));
    if (!built.centroids || !built.bbox_min || !built.bbox_max || !built.labels || !built.ids || !built.blocks) return 0;

    for (uint32_t c = 0; c < clusters; ++c) {
        for (uint32_t d = 0; d < RINHA_DIMS; ++d) {
            built.centroids[(size_t)d * clusters + c] = centroids_row[(size_t)c * RINHA_DIMS + d];
            built.bbox_min[(size_t)c * RINHA_DIMS + d] = INT16_MAX;
            built.bbox_max[(size_t)c * RINHA_DIMS + d] = INT16_MIN;
        }
    }
    for (size_t i = 0; i < padded; ++i) built.ids[i] = UINT32_MAX;
    for (size_t i = 0; i < (size_t)built.total_blocks * RINHA_DIMS * RINHA_BLOCK_LANES; ++i) built.blocks[i] = INT16_MAX;

    uint32_t *pos = (uint32_t *)calloc(clusters, sizeof(uint32_t));
    if (!pos) return 0;
    for (uint32_t row = 0; row < refs.len; ++row) {
        uint32_t c = assign[row];
        uint32_t p = pos[c]++;
        uint32_t block = built.offsets[c] + p / RINHA_BLOCK_LANES;
        uint32_t lane = p % RINHA_BLOCK_LANES;
        size_t label_base = (size_t)block * RINHA_BLOCK_LANES;
        size_t block_base = (size_t)block * RINHA_DIMS * RINHA_BLOCK_LANES;
        built.labels[label_base + lane] = refs.labels[row];
        built.ids[label_base + lane] = row;
        const float *v = refs.vectors + (size_t)row * RINHA_DIMS;
        for (uint32_t d = 0; d < RINHA_DIMS; ++d) {
            int16_t q = quantize(v[d]);
            built.blocks[block_base + (size_t)d * RINHA_BLOCK_LANES + lane] = q;
            size_t bi = (size_t)c * RINHA_DIMS + d;
            if (q < built.bbox_min[bi]) built.bbox_min[bi] = q;
            if (q > built.bbox_max[bi]) built.bbox_max[bi] = q;
        }
    }

    free(pos); free(assign); free(counts); free(centroids_row); free(refs.vectors); free(refs.labels);
    *index = built;
    return 1;
}

static int write_exact(FILE *f, const void *data, size_t len) {
    return fwrite(data, 1, len, f) == len;
}

static int read_exact(FILE *f, void *data, size_t len) {
    return fread(data, 1, len, f) == len;
}

int rinha_index_write(const rinha_index *index, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t magic = RINHA_INDEX_MAGIC;
    uint32_t dims = RINHA_DIMS;
    uint32_t scale = (uint32_t)RINHA_QUANT_SCALE;
    size_t padded = (size_t)index->total_blocks * RINHA_BLOCK_LANES;
    int ok =
        write_exact(f, &magic, sizeof(magic)) &&
        write_exact(f, &index->n, sizeof(index->n)) &&
        write_exact(f, &index->clusters, sizeof(index->clusters)) &&
        write_exact(f, &dims, sizeof(dims)) &&
        write_exact(f, &scale, sizeof(scale)) &&
        write_exact(f, &index->total_blocks, sizeof(index->total_blocks)) &&
        write_exact(f, index->centroids, (size_t)index->clusters * RINHA_DIMS * sizeof(float)) &&
        write_exact(f, index->bbox_min, (size_t)index->clusters * RINHA_DIMS * sizeof(int16_t)) &&
        write_exact(f, index->bbox_max, (size_t)index->clusters * RINHA_DIMS * sizeof(int16_t)) &&
        write_exact(f, index->offsets, ((size_t)index->clusters + 1) * sizeof(uint32_t)) &&
        write_exact(f, index->labels, padded * sizeof(uint8_t)) &&
        write_exact(f, index->ids, padded * sizeof(uint32_t)) &&
        write_exact(f, index->blocks, (size_t)index->total_blocks * RINHA_DIMS * RINHA_BLOCK_LANES * sizeof(int16_t));
    fclose(f);
    return ok;
}

int rinha_index_load(rinha_index *index, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    rinha_index loaded;
    rinha_index_init(&loaded);
    uint32_t magic = 0, dims = 0, scale = 0;
    if (!read_exact(f, &magic, sizeof(magic)) ||
        !read_exact(f, &loaded.n, sizeof(loaded.n)) ||
        !read_exact(f, &loaded.clusters, sizeof(loaded.clusters)) ||
        !read_exact(f, &dims, sizeof(dims)) ||
        !read_exact(f, &scale, sizeof(scale)) ||
        !read_exact(f, &loaded.total_blocks, sizeof(loaded.total_blocks)) ||
        magic != RINHA_INDEX_MAGIC || dims != RINHA_DIMS || scale != (uint32_t)RINHA_QUANT_SCALE) {
        fclose(f);
        return 0;
    }
    size_t padded = (size_t)loaded.total_blocks * RINHA_BLOCK_LANES;
    loaded.centroids = (float *)malloc((size_t)loaded.clusters * RINHA_DIMS * sizeof(float));
    loaded.bbox_min = (int16_t *)malloc((size_t)loaded.clusters * RINHA_DIMS * sizeof(int16_t));
    loaded.bbox_max = (int16_t *)malloc((size_t)loaded.clusters * RINHA_DIMS * sizeof(int16_t));
    loaded.offsets = (uint32_t *)malloc(((size_t)loaded.clusters + 1) * sizeof(uint32_t));
    loaded.labels = (uint8_t *)malloc(padded * sizeof(uint8_t));
    loaded.ids = (uint32_t *)malloc(padded * sizeof(uint32_t));
    loaded.blocks = (int16_t *)malloc((size_t)loaded.total_blocks * RINHA_DIMS * RINHA_BLOCK_LANES * sizeof(int16_t));
    if (!loaded.centroids || !loaded.bbox_min || !loaded.bbox_max || !loaded.offsets || !loaded.labels || !loaded.ids || !loaded.blocks) {
        rinha_index_free(&loaded);
        fclose(f);
        return 0;
    }
    int ok =
        read_exact(f, loaded.centroids, (size_t)loaded.clusters * RINHA_DIMS * sizeof(float)) &&
        read_exact(f, loaded.bbox_min, (size_t)loaded.clusters * RINHA_DIMS * sizeof(int16_t)) &&
        read_exact(f, loaded.bbox_max, (size_t)loaded.clusters * RINHA_DIMS * sizeof(int16_t)) &&
        read_exact(f, loaded.offsets, ((size_t)loaded.clusters + 1) * sizeof(uint32_t)) &&
        read_exact(f, loaded.labels, padded * sizeof(uint8_t)) &&
        read_exact(f, loaded.ids, padded * sizeof(uint32_t)) &&
        read_exact(f, loaded.blocks, (size_t)loaded.total_blocks * RINHA_DIMS * RINHA_BLOCK_LANES * sizeof(int16_t));
    fclose(f);
    if (!ok) {
        rinha_index_free(&loaded);
        return 0;
    }
    *index = loaded;
    return 1;
}

static void insert_probe(uint32_t cluster, float distance, uint32_t *clusters, float *distances, uint32_t nprobe) {
    if (distance >= distances[nprobe - 1]) return;
    uint32_t pos = nprobe - 1;
    while (pos > 0 && distance < distances[pos - 1]) {
        distances[pos] = distances[pos - 1];
        clusters[pos] = clusters[pos - 1];
        --pos;
    }
    distances[pos] = distance;
    clusters[pos] = cluster;
}

static void best_clusters(const rinha_index *index, const rinha_vector *query, uint32_t nprobe, uint32_t *out) {
    float distances[64];
    if (nprobe > 64) nprobe = 64;
    for (uint32_t i = 0; i < nprobe; ++i) distances[i] = INFINITY;
    for (uint32_t c = 0; c < index->clusters; ++c) {
        float acc = 0.0f;
        for (uint32_t d = 0; d < RINHA_DIMS; ++d) {
            float delta = query->v[d] - index->centroids[(size_t)d * index->clusters + c];
            acc += delta * delta;
        }
        insert_probe(c, acc, out, distances, nprobe);
    }
}

static uint64_t bbox_lower(const rinha_index *index, uint32_t cluster, const int16_t *query, uint64_t stop_after) {
    uint64_t acc = 0;
    size_t base = (size_t)cluster * RINHA_DIMS;
    for (uint32_t d = 0; d < RINHA_DIMS; ++d) {
        int16_t q = query[d];
        if (q < index->bbox_min[base + d]) acc += sqdiff_i16(q, index->bbox_min[base + d]);
        else if (q > index->bbox_max[base + d]) acc += sqdiff_i16(q, index->bbox_max[base + d]);
        if (acc > stop_after) return acc;
    }
    return acc;
}

static void scan_blocks_scalar(const rinha_index *index, uint32_t start, uint32_t end, const int16_t *query, top5 *top) {
    for (uint32_t block = start; block < end; ++block) {
        size_t block_base = (size_t)block * RINHA_DIMS * RINHA_BLOCK_LANES;
        size_t label_base = (size_t)block * RINHA_BLOCK_LANES;
        for (uint32_t lane = 0; lane < RINHA_BLOCK_LANES; ++lane) {
            uint32_t id = index->ids[label_base + lane];
            if (id == UINT32_MAX) continue;
            uint64_t dist = 0;
            for (uint32_t d = 0; d < RINHA_DIMS; ++d) {
                dist += sqdiff_i16(query[d], index->blocks[block_base + (size_t)d * RINHA_BLOCK_LANES + lane]);
                if (dist > top->dist[top->worst]) break;
            }
            top5_insert(top, dist, index->labels[label_base + lane], id);
        }
    }
}

__attribute__((target("avx2")))
static void scan_blocks_avx2(const rinha_index *index, uint32_t start, uint32_t end, const int16_t *query, top5 *top) {
    __m256i q[RINHA_DIMS];
    for (uint32_t d = 0; d < RINHA_DIMS; ++d) q[d] = _mm256_set1_epi32((int)query[d]);
    uint32_t values[RINHA_BLOCK_LANES] __attribute__((aligned(32)));
    for (uint32_t block = start; block < end; ++block) {
        size_t block_base = (size_t)block * RINHA_DIMS * RINHA_BLOCK_LANES;
        __m256i acc = _mm256_setzero_si256();
        for (uint32_t d = 0; d < RINHA_DIMS; ++d) {
            const int16_t *ptr = index->blocks + block_base + (size_t)d * RINHA_BLOCK_LANES;
            __m128i raw = _mm_loadu_si128((const __m128i *)ptr);
            __m256i v = _mm256_cvtepi16_epi32(raw);
            __m256i diff = _mm256_sub_epi32(v, q[d]);
            acc = _mm256_add_epi32(acc, _mm256_mullo_epi32(diff, diff));
        }
        _mm256_store_si256((__m256i *)values, acc);
        size_t label_base = (size_t)block * RINHA_BLOCK_LANES;
        for (uint32_t lane = 0; lane < RINHA_BLOCK_LANES; ++lane) {
            uint32_t id = index->ids[label_base + lane];
            if (id != UINT32_MAX) top5_insert(top, values[lane], index->labels[label_base + lane], id);
        }
    }
}

static void scan_blocks(const rinha_index *index, uint32_t start, uint32_t end, const int16_t *query, top5 *top) {
#if defined(__x86_64__)
    if (__builtin_cpu_supports("avx2")) {
        scan_blocks_avx2(index, start, end, query, top);
        return;
    }
#endif
    scan_blocks_scalar(index, start, end, query, top);
}

uint8_t rinha_index_fraud_count(const rinha_index *index, const rinha_vector *query, const rinha_search_config *config) {
    if (!index->n || index->clusters == 0) return 0;
    int16_t q[RINHA_DIMS];
    for (uint32_t d = 0; d < RINHA_DIMS; ++d) q[d] = quantize(query->v[d]);
    uint32_t nprobe = config->fast_nprobe ? config->fast_nprobe : 1;
    if (nprobe > 64) nprobe = 64;
    if (nprobe > index->clusters) nprobe = index->clusters;
    uint32_t probes[64] = {0};
    best_clusters(index, query, nprobe, probes);
    top5 top;
    top5_init(&top);
    for (uint32_t i = 0; i < nprobe; ++i) {
        uint32_t c = probes[i];
        scan_blocks(index, index->offsets[c], index->offsets[c + 1], q, &top);
    }
    if (config->bbox_repair) {
        for (uint32_t c = 0; c < index->clusters; ++c) {
            int seen = 0;
            for (uint32_t i = 0; i < nprobe; ++i) seen |= probes[i] == c;
            if (seen || index->offsets[c] == index->offsets[c + 1]) continue;
            uint64_t worst = top.dist[top.worst];
            if (bbox_lower(index, c, q, worst) <= worst) {
                scan_blocks(index, index->offsets[c], index->offsets[c + 1], q, &top);
            }
        }
    }
    return top5_frauds(&top);
}

size_t rinha_index_memory_bytes(const rinha_index *index) {
    size_t padded = (size_t)index->total_blocks * RINHA_BLOCK_LANES;
    return (size_t)index->clusters * RINHA_DIMS * sizeof(float) +
           (size_t)index->clusters * RINHA_DIMS * sizeof(int16_t) * 2 +
           ((size_t)index->clusters + 1) * sizeof(uint32_t) +
           padded * sizeof(uint8_t) +
           padded * sizeof(uint32_t) +
           (size_t)index->total_blocks * RINHA_DIMS * RINHA_BLOCK_LANES * sizeof(int16_t);
}
