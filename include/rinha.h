#ifndef RINHA_H
#define RINHA_H

#include <stddef.h>
#include <stdint.h>

#define RINHA_DIMS 14
#define RINHA_BLOCK_LANES 8
#define RINHA_MAX_KNOWN 32

typedef struct {
    int64_t total_seconds;
    uint8_t hour;
    uint8_t weekday_monday0;
} rinha_timestamp;

typedef struct {
    float amount;
    uint32_t installments;
    rinha_timestamp requested_at;
    float customer_avg_amount;
    uint32_t tx_count_24h;
    uint32_t known_hashes[RINHA_MAX_KNOWN];
    uint32_t known_count;
    uint32_t merchant_hash;
    uint32_t mcc;
    float merchant_avg_amount;
    uint8_t is_online;
    uint8_t card_present;
    float km_from_home;
    uint8_t has_last_transaction;
    rinha_timestamp last_timestamp;
    float km_from_current;
} rinha_payload;

typedef struct {
    float v[RINHA_DIMS];
} rinha_vector;

typedef struct {
    uint32_t fast_nprobe;
    uint32_t full_nprobe;
    uint8_t boundary_full;
    uint8_t bbox_repair;
    uint8_t repair_min_frauds;
    uint8_t repair_max_frauds;
} rinha_search_config;

typedef struct {
    uint32_t n;
    uint32_t clusters;
    uint32_t total_blocks;
    float *centroids;
    int16_t *bbox_min;
    int16_t *bbox_max;
    uint32_t *offsets;
    uint8_t *labels;
    uint32_t *ids;
    int16_t *blocks;
} rinha_index;

typedef struct {
    uint32_t clusters;
    uint32_t train_sample;
    uint32_t iterations;
    uint32_t max_refs;
} rinha_build_options;

int rinha_parse_payload(const char *body, size_t len, rinha_payload *payload);
void rinha_vectorize(const rinha_payload *payload, rinha_vector *out);

void rinha_index_init(rinha_index *index);
void rinha_index_free(rinha_index *index);
int rinha_index_load(rinha_index *index, const char *path);
int rinha_index_build_from_gzip(const char *path, const rinha_build_options *options, rinha_index *index);
int rinha_index_write(const rinha_index *index, const char *path);
uint8_t rinha_index_fraud_count(const rinha_index *index, const rinha_vector *query, const rinha_search_config *config);
size_t rinha_index_memory_bytes(const rinha_index *index);

uint32_t rinha_env_u32(const char *key, uint32_t fallback);
uint8_t rinha_env_bool(const char *key, uint8_t fallback);

#endif
