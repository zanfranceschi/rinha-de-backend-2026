#include "rinha.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const int64_t SECONDS_PER_DAY = 86400;
static const int64_t SECONDS_PER_MINUTE = 60;

static float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static uint32_t fnv1a(const char *data, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (unsigned char)data[i];
        hash *= 16777619u;
    }
    return hash;
}

static const char *find_bytes(const char *begin, const char *end, const char *needle) {
    const size_t nlen = strlen(needle);
    if ((size_t)(end - begin) < nlen) return NULL;
    for (const char *p = begin; p + nlen <= end; ++p) {
        if (*p == *needle && memcmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

static const char *skip_ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    return p;
}

static const char *value_after_key(const char *p, const char *end, const char *key) {
    p = find_bytes(p, end, key);
    if (!p) return NULL;
    p += strlen(key);
    while (p < end && *p != ':') ++p;
    if (p == end) return NULL;
    return skip_ws(p + 1, end);
}

static int parse_float_value(const char **cursor, const char *end, float *out) {
    const char *p = skip_ws(*cursor, end);
    errno = 0;
    char *next = NULL;
    float value = strtof(p, &next);
    if (next == p || next > end || errno == ERANGE) return 0;
    *out = value;
    *cursor = next;
    return 1;
}

static int parse_u32_value(const char **cursor, const char *end, uint32_t *out) {
    const char *p = skip_ws(*cursor, end);
    uint32_t value = 0;
    int seen = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        value = (value * 10u) + (uint32_t)(*p - '0');
        ++p;
        seen = 1;
    }
    if (!seen) return 0;
    *out = value;
    *cursor = p;
    return 1;
}

static int parse_bool_value(const char **cursor, const char *end, uint8_t *out) {
    const char *p = skip_ws(*cursor, end);
    if (p + 4 <= end && memcmp(p, "true", 4) == 0) {
        *out = 1;
        *cursor = p + 4;
        return 1;
    }
    if (p + 5 <= end && memcmp(p, "false", 5) == 0) {
        *out = 0;
        *cursor = p + 5;
        return 1;
    }
    return 0;
}

static int parse_string_hash(const char **cursor, const char *end, uint32_t *hash) {
    const char *p = skip_ws(*cursor, end);
    if (p >= end || *p != '"') return 0;
    ++p;
    const char *start = p;
    while (p < end && *p != '"') ++p;
    if (p == end) return 0;
    *hash = fnv1a(start, (size_t)(p - start));
    *cursor = p + 1;
    return 1;
}

static int parse_mcc_value(const char **cursor, const char *end, uint32_t *mcc) {
    const char *p = skip_ws(*cursor, end);
    if (p < end && *p == '"') ++p;
    uint32_t value = 0;
    int seen = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        value = (value * 10u) + (uint32_t)(*p - '0');
        ++p;
        seen = 1;
    }
    if (!seen) return 0;
    if (p < end && *p == '"') ++p;
    *mcc = value;
    *cursor = p;
    return 1;
}

static int64_t days_from_civil(int32_t year, uint8_t month, uint8_t day) {
    year -= month <= 2;
    const int32_t era = year >= 0 ? year / 400 : (year - 399) / 400;
    const uint32_t yoe = (uint32_t)(year - era * 400);
    const uint32_t mp = (uint32_t)month + (month > 2 ? 4294967293u : 9u);
    const uint32_t doy = (153u * mp + 2u) / 5u + (uint32_t)day - 1u;
    const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static uint8_t two_digits(const char *s) {
    return (uint8_t)((s[0] - '0') * 10 + (s[1] - '0'));
}

static int parse_timestamp_value(const char **cursor, const char *end, rinha_timestamp *out) {
    const char *p = skip_ws(*cursor, end);
    if (p < end && *p == '"') ++p;
    if (p + 20 > end) return 0;
    if (p[4] != '-' || p[7] != '-' || p[10] != 'T' || p[13] != ':' || p[16] != ':' || p[19] != 'Z') return 0;
    int32_t year = (p[0] - '0') * 1000 + (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
    uint8_t month = two_digits(p + 5);
    uint8_t day = two_digits(p + 8);
    uint8_t hour = two_digits(p + 11);
    uint8_t minute = two_digits(p + 14);
    uint8_t second = two_digits(p + 17);
    int64_t days = days_from_civil(year, month, day);
    out->total_seconds = days * SECONDS_PER_DAY + (int64_t)hour * 3600 + (int64_t)minute * SECONDS_PER_MINUTE + second;
    out->hour = hour;
    out->weekday_monday0 = (uint8_t)((days + 3) % 7);
    p += 20;
    if (p < end && *p == '"') ++p;
    *cursor = p;
    return 1;
}

static int parse_known_merchants(const char **cursor, const char *end, rinha_payload *payload) {
    const char *p = skip_ws(*cursor, end);
    if (p >= end || *p != '[') return 0;
    ++p;
    payload->known_count = 0;
    while (p < end && *p != ']') {
        p = skip_ws(p, end);
        if (p < end && *p == '"') {
            uint32_t hash = 0;
            if (!parse_string_hash(&p, end, &hash)) return 0;
            if (payload->known_count < RINHA_MAX_KNOWN) {
                payload->known_hashes[payload->known_count++] = hash;
            }
        } else {
            ++p;
        }
    }
    if (p == end) return 0;
    *cursor = p + 1;
    return 1;
}

int rinha_parse_payload(const char *body, size_t len, rinha_payload *payload) {
    const char *end = body + len;
    const char *p = body;
    memset(payload, 0, sizeof(*payload));

    p = value_after_key(p, end, "\"amount\"");
    if (!p || !parse_float_value(&p, end, &payload->amount)) return 0;
    p = value_after_key(p, end, "\"installments\"");
    if (!p || !parse_u32_value(&p, end, &payload->installments)) return 0;
    p = value_after_key(p, end, "\"requested_at\"");
    if (!p || !parse_timestamp_value(&p, end, &payload->requested_at)) return 0;
    p = value_after_key(p, end, "\"avg_amount\"");
    if (!p || !parse_float_value(&p, end, &payload->customer_avg_amount)) return 0;
    p = value_after_key(p, end, "\"tx_count_24h\"");
    if (!p || !parse_u32_value(&p, end, &payload->tx_count_24h)) return 0;
    p = value_after_key(p, end, "\"known_merchants\"");
    if (!p || !parse_known_merchants(&p, end, payload)) return 0;
    p = value_after_key(p, end, "\"id\"");
    if (!p || !parse_string_hash(&p, end, &payload->merchant_hash)) return 0;
    p = value_after_key(p, end, "\"mcc\"");
    if (!p || !parse_mcc_value(&p, end, &payload->mcc)) return 0;
    p = value_after_key(p, end, "\"avg_amount\"");
    if (!p || !parse_float_value(&p, end, &payload->merchant_avg_amount)) return 0;
    p = value_after_key(p, end, "\"is_online\"");
    if (!p || !parse_bool_value(&p, end, &payload->is_online)) return 0;
    p = value_after_key(p, end, "\"card_present\"");
    if (!p || !parse_bool_value(&p, end, &payload->card_present)) return 0;
    p = value_after_key(p, end, "\"km_from_home\"");
    if (!p || !parse_float_value(&p, end, &payload->km_from_home)) return 0;
    p = value_after_key(p, end, "\"last_transaction\"");
    if (!p) return 0;
    p = skip_ws(p, end);
    if (p + 4 <= end && memcmp(p, "null", 4) == 0) {
        payload->has_last_transaction = 0;
        return 1;
    }
    payload->has_last_transaction = 1;
    p = value_after_key(p, end, "\"timestamp\"");
    if (!p || !parse_timestamp_value(&p, end, &payload->last_timestamp)) return 0;
    p = value_after_key(p, end, "\"km_from_current\"");
    if (!p || !parse_float_value(&p, end, &payload->km_from_current)) return 0;
    return 1;
}

static float mcc_risk(uint32_t mcc) {
    switch (mcc) {
        case 5411: return 0.15f;
        case 5812: return 0.30f;
        case 5912: return 0.20f;
        case 5944: return 0.45f;
        case 7801: return 0.80f;
        case 7802: return 0.75f;
        case 7995: return 0.85f;
        case 4511: return 0.35f;
        case 5311: return 0.25f;
        case 5999: return 0.50f;
        default: return 0.50f;
    }
}

void rinha_vectorize(const rinha_payload *payload, rinha_vector *out) {
    uint8_t known_merchant = 0;
    for (uint32_t i = 0; i < payload->known_count; ++i) {
        known_merchant |= payload->known_hashes[i] == payload->merchant_hash;
    }

    float minutes_since_last = -1.0f;
    float km_from_last = -1.0f;
    if (payload->has_last_transaction) {
        int64_t elapsed = payload->requested_at.total_seconds - payload->last_timestamp.total_seconds;
        if (elapsed < 0) elapsed = 0;
        minutes_since_last = clamp01((float)(elapsed / SECONDS_PER_MINUTE) / 1440.0f);
        km_from_last = clamp01(payload->km_from_current / 1000.0f);
    }

    float amount_vs_avg = payload->customer_avg_amount <= 0.0f
        ? (payload->amount <= 0.0f ? 0.0f : 1.0f)
        : (payload->amount / payload->customer_avg_amount) / 10.0f;

    out->v[0] = clamp01(payload->amount / 10000.0f);
    out->v[1] = clamp01((float)payload->installments / 12.0f);
    out->v[2] = clamp01(amount_vs_avg);
    out->v[3] = (float)payload->requested_at.hour / 23.0f;
    out->v[4] = (float)payload->requested_at.weekday_monday0 / 6.0f;
    out->v[5] = minutes_since_last;
    out->v[6] = km_from_last;
    out->v[7] = clamp01(payload->km_from_home / 1000.0f);
    out->v[8] = clamp01((float)payload->tx_count_24h / 20.0f);
    out->v[9] = payload->is_online ? 1.0f : 0.0f;
    out->v[10] = payload->card_present ? 1.0f : 0.0f;
    out->v[11] = known_merchant ? 0.0f : 1.0f;
    out->v[12] = mcc_risk(payload->mcc);
    out->v[13] = clamp01(payload->merchant_avg_amount / 10000.0f);
}

uint32_t rinha_env_u32(const char *key, uint32_t fallback) {
    const char *value = getenv(key);
    if (!value || !*value) return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    return end == value ? fallback : (uint32_t)parsed;
}

uint8_t rinha_env_bool(const char *key, uint8_t fallback) {
    const char *value = getenv(key);
    if (!value || !*value) return fallback;
    return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0;
}
