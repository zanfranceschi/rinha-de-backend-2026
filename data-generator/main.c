#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include "cjson/cJSON.h"

/* ═══════════════════════════════════════════════════════════════════════════
   SHA-256 (compatível com sha256sum)
   ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[64];
    size_t   buflen;
} Sha256;

#define SHA_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static const uint32_t SHA_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(Sha256 *s, const uint8_t b[64]) {
    uint32_t m[64], a,b_,c,d,e,f,g,h,t1,t2;
    for (int i = 0, j = 0; i < 16; i++, j += 4)
        m[i] = ((uint32_t)b[j] << 24) | ((uint32_t)b[j+1] << 16) | ((uint32_t)b[j+2] << 8) | (uint32_t)b[j+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = SHA_ROTR(m[i-15],7) ^ SHA_ROTR(m[i-15],18) ^ (m[i-15] >> 3);
        uint32_t s1 = SHA_ROTR(m[i-2],17) ^ SHA_ROTR(m[i-2],19)  ^ (m[i-2] >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }
    a = s->state[0]; b_= s->state[1]; c = s->state[2]; d = s->state[3];
    e = s->state[4]; f = s->state[5]; g = s->state[6]; h = s->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = SHA_ROTR(e,6) ^ SHA_ROTR(e,11) ^ SHA_ROTR(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        t1 = h + S1 + ch + SHA_K[i] + m[i];
        uint32_t S0 = SHA_ROTR(a,2) ^ SHA_ROTR(a,13) ^ SHA_ROTR(a,22);
        uint32_t mj = (a & b_) ^ (a & c) ^ (b_ & c);
        t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b_; b_ = a; a = t1 + t2;
    }
    s->state[0] += a; s->state[1] += b_; s->state[2] += c; s->state[3] += d;
    s->state[4] += e; s->state[5] += f;  s->state[6] += g; s->state[7] += h;
}

static void sha256_init(Sha256 *s) {
    s->state[0] = 0x6a09e667; s->state[1] = 0xbb67ae85;
    s->state[2] = 0x3c6ef372; s->state[3] = 0xa54ff53a;
    s->state[4] = 0x510e527f; s->state[5] = 0x9b05688c;
    s->state[6] = 0x1f83d9ab; s->state[7] = 0x5be0cd19;
    s->bitlen = 0; s->buflen = 0;
}

static void sha256_update(Sha256 *s, const void *data, size_t len) {
    const uint8_t *p = data;
    s->bitlen += (uint64_t)len * 8;
    for (size_t i = 0; i < len; i++) {
        s->buffer[s->buflen++] = p[i];
        if (s->buflen == 64) { sha256_transform(s, s->buffer); s->buflen = 0; }
    }
}

static void sha256_final(Sha256 *s, uint8_t out[32]) {
    size_t i = s->buflen;
    if (i < 56) {
        s->buffer[i++] = 0x80;
        while (i < 56) s->buffer[i++] = 0;
    } else {
        s->buffer[i++] = 0x80;
        while (i < 64) s->buffer[i++] = 0;
        sha256_transform(s, s->buffer);
        memset(s->buffer, 0, 56);
    }
    for (int j = 0; j < 8; j++)
        s->buffer[56 + j] = (uint8_t)(s->bitlen >> (56 - j*8));
    sha256_transform(s, s->buffer);
    for (int j = 0; j < 8; j++) {
        out[j*4]   = (uint8_t)(s->state[j] >> 24);
        out[j*4+1] = (uint8_t)(s->state[j] >> 16);
        out[j*4+2] = (uint8_t)(s->state[j] >> 8);
        out[j*4+3] = (uint8_t)(s->state[j]);
    }
}

static int sha256_file(const char *path, char hex_out[65]) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    Sha256 s;
    sha256_init(&s);
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        sha256_update(&s, buf, n);
    fclose(f);
    uint8_t digest[32];
    sha256_final(&s, digest);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        hex_out[i*2]   = hex[(digest[i] >> 4) & 0xf];
        hex_out[i*2+1] = hex[digest[i] & 0xf];
    }
    hex_out[64] = '\0';
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
   PCG32 PRNG
   ═══════════════════════════════════════════════════════════════════════════ */

typedef struct { uint64_t state, inc; } Rng;

static uint32_t pcg32(Rng *r) {
    uint64_t s = r->state;
    r->state = s * 6364136223846793005ULL + r->inc;
    uint32_t x = (uint32_t)(((s >> 18u) ^ s) >> 27u);
    uint32_t rot = (uint32_t)(s >> 59u);
    return (x >> rot) | (x << ((~rot + 1u) & 31u));
}

static void rng_init(Rng *r, uint64_t seed) {
    r->state = 0;
    r->inc = (seed << 1u) | 1u;
    pcg32(r);
    r->state += seed;
    pcg32(r);
}

static double rng_f64(Rng *r) {
    return (double)pcg32(r) / 4294967295.0;
}

static double rng_range(Rng *r, double lo, double hi) {
    return lo + rng_f64(r) * (hi - lo);
}

static int rng_int(Rng *r, int lo, int hi) {
    return lo + (int)(pcg32(r) % (uint32_t)(hi - lo));
}

/* ═══════════════════════════════════════════════════════════════════════════
   Constants
   ═══════════════════════════════════════════════════════════════════════════ */

#define VDIM       14
#define KNN_K      5
#define THRESHOLD  0.6
#define REF_SEED   42ULL
#define PAY_SEED   4242ULL
#define MAX_KNOWN  6
#define ID_LEN     16
#define TS_LEN     32
#define MAX_MCC    32

/* ═══════════════════════════════════════════════════════════════════════════
   Types
   ═══════════════════════════════════════════════════════════════════════════ */

typedef enum { LEGIT, FRAUD, BORDERLINE } Profile;

typedef struct {
    double max_amount, max_installments, amount_vs_avg_ratio;
    double max_minutes, max_km, max_tx_count_24h, max_merchant_avg_amount;
} NormCfg;

typedef struct { char code[8]; double risk; } MCCEntry;
typedef struct { MCCEntry e[MAX_MCC]; int n; } MCCMap;

typedef struct {
    char id[32];
    double amount;
    int installments;
    char requested_at[TS_LEN];
    double cust_avg;
    int tx_count_24h;
    char known[MAX_KNOWN][ID_LEN];
    int known_n;
    char merch_id[ID_LEN];
    char mcc[8];
    double merch_avg;
    int is_online, card_present;
    double km_home;
    int has_last;
    char last_ts[TS_LEN];
    double last_km;
} Request;

typedef struct { double v[VDIM]; char label[8]; } RefVec;

typedef struct {
    Request req;
    double vec[VDIM];
    int approved;
    double fraud_score;
} TestEntry;

/* ═══════════════════════════════════════════════════════════════════════════
   Helpers
   ═══════════════════════════════════════════════════════════════════════════ */

static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static double round2(double v)  { return round(v * 100.0) / 100.0; }
static double round4(double v)  { return round(v * 10000.0) / 10000.0; }

static double mcc_lookup(const MCCMap *m, const char *code) {
    for (int i = 0; i < m->n; i++)
        if (strcmp(m->e[i].code, code) == 0) return m->e[i].risk;
    return 0.5;
}

/* ═══════════════════════════════════════════════════════════════════════════
   JSON number formatting — clean output without floating-point noise
   ═══════════════════════════════════════════════════════════════════════════ */

static cJSON *jnum(double v) {
    char buf[64];
    if (v == floor(v) && fabs(v) < 1e15) {
        snprintf(buf, sizeof(buf), "%.0f", v);
    } else {
        snprintf(buf, sizeof(buf), "%.10f", v);
        size_t len = strlen(buf);
        while (len > 1 && buf[len - 1] == '0') len--;
        if (buf[len - 1] == '.') len--;
        buf[len] = '\0';
    }
    return cJSON_CreateRaw(buf);
}

static cJSON *jnum_array(const double *vals, int n) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++)
        cJSON_AddItemToArray(arr, jnum(vals[i]));
    return arr;
}

/* ═══════════════════════════════════════════════════════════════════════════
   File I/O + Config
   ═══════════════════════════════════════════════════════════════════════════ */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)len + 1);
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "error: failed to read %s\n", path);
        free(buf);
        fclose(f);
        exit(1);
    }
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static NormCfg load_norm(const char *path) {
    char *s = read_file(path);
    cJSON *j = cJSON_Parse(s);
    if (!j) { fprintf(stderr, "error: invalid JSON in %s\n", path); exit(1); }
    NormCfg c = {
        .max_amount            = cJSON_GetObjectItem(j, "max_amount")->valuedouble,
        .max_installments      = cJSON_GetObjectItem(j, "max_installments")->valuedouble,
        .amount_vs_avg_ratio   = cJSON_GetObjectItem(j, "amount_vs_avg_ratio")->valuedouble,
        .max_minutes           = cJSON_GetObjectItem(j, "max_minutes")->valuedouble,
        .max_km                = cJSON_GetObjectItem(j, "max_km")->valuedouble,
        .max_tx_count_24h      = cJSON_GetObjectItem(j, "max_tx_count_24h")->valuedouble,
        .max_merchant_avg_amount = cJSON_GetObjectItem(j, "max_merchant_avg_amount")->valuedouble,
    };
    cJSON_Delete(j);
    free(s);
    return c;
}

static RefVec *load_refs(const char *path, int *out_n) {
    char *s = read_file(path);
    cJSON *j = cJSON_Parse(s);
    if (!j || !cJSON_IsArray(j)) {
        fprintf(stderr, "error: invalid refs JSON in %s\n", path);
        exit(1);
    }
    int n = cJSON_GetArraySize(j);
    RefVec *refs = malloc((size_t)n * sizeof(RefVec));
    int i = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, j) {
        cJSON *vec   = cJSON_GetObjectItem(item, "vector");
        cJSON *label = cJSON_GetObjectItem(item, "label");
        if (!cJSON_IsArray(vec) || cJSON_GetArraySize(vec) != VDIM) {
            fprintf(stderr, "error: ref %d has invalid vector (expected %d dims)\n", i, VDIM);
            exit(1);
        }
        if (!cJSON_IsString(label)) {
            fprintf(stderr, "error: ref %d missing label\n", i);
            exit(1);
        }
        for (int k = 0; k < VDIM; k++)
            refs[i].v[k] = cJSON_GetArrayItem(vec, k)->valuedouble;
        strncpy(refs[i].label, label->valuestring, 7);
        refs[i].label[7] = '\0';
        i++;
    }
    cJSON_Delete(j);
    free(s);
    *out_n = n;
    return refs;
}

static MCCMap load_mcc(const char *path) {
    char *s = read_file(path);
    cJSON *j = cJSON_Parse(s);
    if (!j) { fprintf(stderr, "error: invalid JSON in %s\n", path); exit(1); }
    MCCMap m = {.n = 0};
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, j) {
        if (m.n >= MAX_MCC) break;
        strncpy(m.e[m.n].code, item->string, 7);
        m.e[m.n].code[7] = '\0';
        m.e[m.n].risk = item->valuedouble;
        m.n++;
    }
    cJSON_Delete(j);
    free(s);
    return m;
}

/* ═══════════════════════════════════════════════════════════════════════════
   Timestamp
   ═══════════════════════════════════════════════════════════════════════════ */

static time_t ts_epoch(const char *ts) {
    int y, mo, d, h, mi, s;
    sscanf(ts, "%d-%d-%dT%d:%d:%dZ", &y, &mo, &d, &h, &mi, &s);
    struct tm t = {0};
    t.tm_year = y - 1900;
    t.tm_mon  = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min  = mi;
    t.tm_sec  = s;
    return timegm(&t);
}

static void epoch_to_ts(time_t ep, char *buf) {
    struct tm *t = gmtime(&ep);
    sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
}

/* Day of week: Monday=0, Sunday=6 (Tomohiko Sakamoto) */
static int day_of_week(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    int dow = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7; /* 0=Sun */
    return (dow + 6) % 7; /* 0=Mon, 6=Sun */
}

/* ═══════════════════════════════════════════════════════════════════════════
   Profile + Request generation
   ═══════════════════════════════════════════════════════════════════════════ */

/*
 * fraud_ratio: desired fraud fraction (0.0 to 1.0).
 * 10% of the fraud budget becomes borderline, the rest is pure fraud.
 * E.g. fraud_ratio=0.30 -> 70% legit, 27% fraud, 3% borderline.
 */
static Profile pick_profile(Rng *r, double fraud_ratio) {
    double borderline = fraud_ratio * 0.10;
    double v = rng_f64(r);
    if (v < 1.0 - fraud_ratio) return LEGIT;
    if (v < 1.0 - borderline)  return FRAUD;
    return BORDERLINE;
}

static const char *LEGIT_MCCS[] = {"5411", "5812", "5912", "5311"};
static const char *FRAUD_MCCS[] = {"7995", "7801", "7802"};

static Request gen_request(Rng *r, Profile p, const MCCMap *mcc_map) {
    Request req = {0};

    sprintf(req.id, "tx-%u", pcg32(r));

    switch (p) {
        case LEGIT:      req.amount = rng_range(r, 10, 500); break;
        case FRAUD:      req.amount = rng_range(r, 2000, 10000); break;
        case BORDERLINE: req.amount = rng_range(r, 400, 3000); break;
    }
    req.amount = round2(req.amount);

    switch (p) {
        case LEGIT:      req.installments = rng_int(r, 1, 4); break;
        case FRAUD:      req.installments = rng_int(r, 6, 13); break;
        case BORDERLINE: req.installments = rng_int(r, 3, 8); break;
    }

    int day = rng_int(r, 10, 28);
    int h_lo = 8, h_hi = 21;
    switch (p) {
        case LEGIT:      h_lo = 8;  h_hi = 21; break;
        case FRAUD:      h_lo = 0;  h_hi = 7;  break;
        case BORDERLINE: h_lo = 6;  h_hi = 23; break;
    }
    int hour = rng_int(r, h_lo, h_hi);
    int mn   = rng_int(r, 0, 60);
    int sec  = rng_int(r, 0, 60);
    sprintf(req.requested_at, "2026-03-%02dT%02d:%02d:%02dZ", day, hour, mn, sec);

    switch (p) {
        case LEGIT:      req.cust_avg = rng_range(r, req.amount / 0.5, req.amount * 2.0); break;
        case FRAUD:      req.cust_avg = rng_range(r, 50, 300); break;
        case BORDERLINE: req.cust_avg = rng_range(r, 100, 500); break;
    }
    req.cust_avg = round2(req.cust_avg);

    switch (p) {
        case LEGIT:      req.tx_count_24h = rng_int(r, 1, 6); break;
        case FRAUD:      req.tx_count_24h = rng_int(r, 8, 21); break;
        case BORDERLINE: req.tx_count_24h = rng_int(r, 4, 12); break;
    }

    req.known_n = rng_int(r, 2, 6);
    for (int i = 0; i < req.known_n; i++)
        sprintf(req.known[i], "MERC-%03d", rng_int(r, 1, 20));

    switch (p) {
        case LEGIT:
            strcpy(req.merch_id, req.known[rng_int(r, 0, req.known_n)]);
            break;
        case FRAUD:
            sprintf(req.merch_id, "MERC-%03d", rng_int(r, 50, 100));
            break;
        case BORDERLINE:
            if (rng_f64(r) < 0.5)
                strcpy(req.merch_id, req.known[rng_int(r, 0, req.known_n)]);
            else
                sprintf(req.merch_id, "MERC-%03d", rng_int(r, 30, 60));
            break;
    }

    switch (p) {
        case LEGIT:      strcpy(req.mcc, LEGIT_MCCS[rng_int(r, 0, 4)]); break;
        case FRAUD:      strcpy(req.mcc, FRAUD_MCCS[rng_int(r, 0, 3)]); break;
        case BORDERLINE: strcpy(req.mcc, mcc_map->e[rng_int(r, 0, mcc_map->n)].code); break;
    }

    switch (p) {
        case LEGIT:      req.merch_avg = rng_range(r, 30, 500); break;
        case FRAUD:      req.merch_avg = rng_range(r, 20, 100); break;
        case BORDERLINE: req.merch_avg = rng_range(r, 50, 300); break;
    }
    req.merch_avg = round2(req.merch_avg);

    switch (p) {
        case LEGIT:      req.is_online = rng_f64(r) < 0.3; break;
        case FRAUD:      req.is_online = rng_f64(r) < 0.8; break;
        case BORDERLINE: req.is_online = rng_f64(r) < 0.5; break;
    }
    req.card_present = req.is_online ? 0 : (rng_f64(r) < 0.9);

    switch (p) {
        case LEGIT:      req.km_home = rng_range(r, 0, 50); break;
        case FRAUD:      req.km_home = rng_range(r, 200, 1000); break;
        case BORDERLINE: req.km_home = rng_range(r, 30, 400); break;
    }

    if (rng_f64(r) < 0.2) {
        req.has_last = 0;
    } else {
        req.has_last = 1;
        time_t req_ep = ts_epoch(req.requested_at);
        int mins_back = 30;
        switch (p) {
            case LEGIT:      mins_back = rng_int(r, 30, 720); break;
            case FRAUD:      mins_back = rng_int(r, 1, 10); break;
            case BORDERLINE: mins_back = rng_int(r, 5, 120); break;
        }
        epoch_to_ts(req_ep - mins_back * 60, req.last_ts);
        switch (p) {
            case LEGIT:      req.last_km = rng_range(r, 0, 20); break;
            case FRAUD:      req.last_km = rng_range(r, 200, 1000); break;
            case BORDERLINE: req.last_km = rng_range(r, 20, 300); break;
        }
    }

    return req;
}

/* ═══════════════════════════════════════════════════════════════════════════
   Normalization — 14 dimensions
   ═══════════════════════════════════════════════════════════════════════════ */

static void normalize(const Request *req, const NormCfg *cfg, const MCCMap *mcc_map, double *out) {
    int y, mo, d, h, mi, s;
    sscanf(req->requested_at, "%d-%d-%dT%d:%d:%dZ", &y, &mo, &d, &h, &mi, &s);
    int dow = day_of_week(y, mo, d);

    out[0]  = clamp01(req->amount / cfg->max_amount);
    out[1]  = clamp01((double)req->installments / cfg->max_installments);
    out[2]  = clamp01((req->amount / req->cust_avg) / cfg->amount_vs_avg_ratio);
    out[3]  = (double)h / 23.0;
    out[4]  = (double)dow / 6.0;

    if (req->has_last) {
        double mins = difftime(ts_epoch(req->requested_at), ts_epoch(req->last_ts)) / 60.0;
        out[5] = clamp01(mins / cfg->max_minutes);
        out[6] = clamp01(req->last_km / cfg->max_km);
    } else {
        out[5] = -1.0;
        out[6] = -1.0;
    }

    out[7]  = clamp01(req->km_home / cfg->max_km);
    out[8]  = clamp01((double)req->tx_count_24h / cfg->max_tx_count_24h);
    out[9]  = req->is_online ? 1.0 : 0.0;
    out[10] = req->card_present ? 1.0 : 0.0;

    int known = 0;
    for (int i = 0; i < req->known_n; i++)
        if (strcmp(req->known[i], req->merch_id) == 0) { known = 1; break; }
    out[11] = known ? 0.0 : 1.0;

    out[12] = mcc_lookup(mcc_map, req->mcc);
    out[13] = clamp01(req->merch_avg / cfg->max_merchant_avg_amount);
}

/* ═══════════════════════════════════════════════════════════════════════════
   KNN — euclidean distance, brute-force
   ═══════════════════════════════════════════════════════════════════════════ */

static double euclidean_dist(const double *a, const double *b) {
    double sum = 0;
    for (int i = 0; i < VDIM; i++) {
        double d = a[i] - b[i];
        sum += d * d;
    }
    return sqrt(sum);
}

static void knn_classify(const double *vec, const RefVec *refs, int nrefs,
                          int *out_approved, double *out_score) {
    double dists[KNN_K];
    int    idxs[KNN_K];
    for (int i = 0; i < KNN_K; i++) { dists[i] = 1e30; idxs[i] = -1; }

    for (int i = 0; i < nrefs; i++) {
        double d = euclidean_dist(vec, refs[i].v);
        for (int j = 0; j < KNN_K; j++) {
            if (d < dists[j]) {
                for (int k = KNN_K - 1; k > j; k--) {
                    dists[k] = dists[k - 1];
                    idxs[k]  = idxs[k - 1];
                }
                dists[j] = d;
                idxs[j]  = i;
                break;
            }
        }
    }

    int fraud_n = 0;
    for (int i = 0; i < KNN_K; i++)
        if (idxs[i] >= 0 && strcmp(refs[idxs[i]].label, "fraud") == 0)
            fraud_n++;

    *out_score    = (double)fraud_n / KNN_K;
    *out_approved = *out_score < THRESHOLD;
}

/* ═══════════════════════════════════════════════════════════════════════════
   JSON output
   ═══════════════════════════════════════════════════════════════════════════ */

static cJSON *request_to_json(const Request *r) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "id", r->id);

    cJSON *tx = cJSON_AddObjectToObject(obj, "transaction");
    cJSON_AddItemToObject(tx, "amount", jnum(r->amount));
    cJSON_AddItemToObject(tx, "installments", jnum(r->installments));
    cJSON_AddStringToObject(tx, "requested_at", r->requested_at);

    cJSON *cust = cJSON_AddObjectToObject(obj, "customer");
    cJSON_AddItemToObject(cust, "avg_amount", jnum(r->cust_avg));
    cJSON_AddItemToObject(cust, "tx_count_24h", jnum(r->tx_count_24h));
    cJSON *km = cJSON_AddArrayToObject(cust, "known_merchants");
    for (int i = 0; i < r->known_n; i++)
        cJSON_AddItemToArray(km, cJSON_CreateString(r->known[i]));

    cJSON *merch = cJSON_AddObjectToObject(obj, "merchant");
    cJSON_AddStringToObject(merch, "id", r->merch_id);
    cJSON_AddStringToObject(merch, "mcc", r->mcc);
    cJSON_AddItemToObject(merch, "avg_amount", jnum(r->merch_avg));

    cJSON *term = cJSON_AddObjectToObject(obj, "terminal");
    cJSON_AddBoolToObject(term, "is_online", r->is_online);
    cJSON_AddBoolToObject(term, "card_present", r->card_present);
    cJSON_AddItemToObject(term, "km_from_home", jnum(r->km_home));

    if (r->has_last) {
        cJSON *last = cJSON_AddObjectToObject(obj, "last_transaction");
        cJSON_AddStringToObject(last, "timestamp", r->last_ts);
        cJSON_AddItemToObject(last, "km_from_current", jnum(r->last_km));
    } else {
        cJSON_AddNullToObject(obj, "last_transaction");
    }

    return obj;
}

static int pretty_json = 0;

static void write_json_file(const char *path, cJSON *json) {
    char *str = pretty_json ? cJSON_Print(json) : cJSON_PrintUnformatted(json);
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "error: cannot write %s\n", path); exit(1); }
    fputs(str, f);
    fputc('\n', f);
    fclose(f);
    free(str);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Main
   ═══════════════════════════════════════════════════════════════════════════ */

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --refs N             number of reference vectors (default: 200)\n"
        "  --payloads N         number of test payloads (default: 1000)\n"
        "  --fraud-ratio-refs F      fraud ratio for references, 0.0 to 1.0 (default: 0.30)\n"
        "  --fraud-ratio-payloads F  fraud ratio for payloads, 0.0 to 1.0 (default: 0.30)\n"
        "  --norm-cfg PATH      path to normalization.json (default: resources/normalization.json)\n"
        "  --mcc-cfg PATH       path to mcc_risk.json (default: resources/mcc_risk.json)\n"
        "  --refs-out PATH      output path for references.json (default: resources/references.json)\n"
        "  --reuse-refs         skip reference generation; load from --refs-in instead\n"
        "  --refs-in PATH       input path for references.json when --reuse-refs is set\n"
        "                       (default: resources/references.json)\n"
        "  --payloads-out PATH  output path for test-data.json (default: test/test-data.json)\n"
        "  --pretty-json        indent JSON output (default: compact)\n"
        "  --help               show this message\n",
        prog);
}

int main(int argc, char **argv) {
    int ref_size = 200, payload_size = 1000;
    const char *norm_path     = "resources/normalization.json";
    const char *mcc_path      = "resources/mcc_risk.json";
    double fraud_ratio_refs     = 0.30;
    double fraud_ratio_payloads = 0.30;
    const char *refs_out      = "resources/references.json";
    const char *refs_in       = "resources/references.json";
    int reuse_refs            = 0;
    const char *payloads_out  = "test/test-data.json";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--refs") == 0 && i + 1 < argc)
            ref_size = atoi(argv[++i]);
        else if (strcmp(argv[i], "--payloads") == 0 && i + 1 < argc)
            payload_size = atoi(argv[++i]);
        else if (strcmp(argv[i], "--norm-cfg") == 0 && i + 1 < argc)
            norm_path = argv[++i];
        else if (strcmp(argv[i], "--mcc-cfg") == 0 && i + 1 < argc)
            mcc_path = argv[++i];
        else if (strcmp(argv[i], "--fraud-ratio-refs") == 0 && i + 1 < argc)
            fraud_ratio_refs = atof(argv[++i]);
        else if (strcmp(argv[i], "--fraud-ratio-payloads") == 0 && i + 1 < argc)
            fraud_ratio_payloads = atof(argv[++i]);
        else if (strcmp(argv[i], "--refs-out") == 0 && i + 1 < argc)
            refs_out = argv[++i];
        else if (strcmp(argv[i], "--refs-in") == 0 && i + 1 < argc)
            refs_in = argv[++i];
        else if (strcmp(argv[i], "--reuse-refs") == 0)
            reuse_refs = 1;
        else if (strcmp(argv[i], "--payloads-out") == 0 && i + 1 < argc)
            payloads_out = argv[++i];
        else if (strcmp(argv[i], "--pretty-json") == 0)
            pretty_json = 1;
        else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    printf("Loading configuration...\n");
    NormCfg norm = load_norm(norm_path);
    MCCMap  mcc  = load_mcc(mcc_path);

    /* --- Referencias --- */
    RefVec *refs = NULL;
    Rng rng;

    if (reuse_refs) {
        printf("Loading reference vectors from %s...\n", refs_in);
        refs = load_refs(refs_in, &ref_size);
        printf("  -> loaded %d vectors\n", ref_size);
    } else {
        printf("Generating %d reference vectors...\n", ref_size);
        refs = malloc((size_t)ref_size * sizeof(RefVec));
        rng_init(&rng, REF_SEED);

        for (int i = 0; i < ref_size; i++) {
            Profile p   = pick_profile(&rng, fraud_ratio_refs);
            Request req = gen_request(&rng, p, &mcc);
            normalize(&req, &norm, &mcc, refs[i].v);
            for (int j = 0; j < VDIM; j++) refs[i].v[j] = round4(refs[i].v[j]);

            if (p == BORDERLINE)
                strcpy(refs[i].label, rng_f64(&rng) < 0.5 ? "fraud" : "legit");
            else
                strcpy(refs[i].label, p == FRAUD ? "fraud" : "legit");
        }

        cJSON *refs_json = cJSON_CreateArray();
        for (int i = 0; i < ref_size; i++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddItemToObject(item, "vector", jnum_array(refs[i].v, VDIM));
            cJSON_AddStringToObject(item, "label", refs[i].label);
            cJSON_AddItemToArray(refs_json, item);
        }
        write_json_file(refs_out, refs_json);
        cJSON_Delete(refs_json);
        printf("  -> %s (%d vectors)\n", refs_out, ref_size);
    }

    /* --- Checksum das referências --- */
    const char *refs_path = reuse_refs ? refs_in : refs_out;
    char refs_checksum[65];
    if (sha256_file(refs_path, refs_checksum) != 0) {
        fprintf(stderr, "error: cannot read %s for checksum\n", refs_path);
        return 1;
    }
    printf("References SHA-256: %s\n", refs_checksum);

    /* --- Payloads de teste --- */
    printf("Generating %d test payloads...\n", payload_size);
    rng_init(&rng, PAY_SEED);

    TestEntry *entries = malloc((size_t)payload_size * sizeof(TestEntry));
    for (int i = 0; i < payload_size; i++) {
        Profile p = pick_profile(&rng, fraud_ratio_payloads);
        entries[i].req = gen_request(&rng, p, &mcc);
        normalize(&entries[i].req, &norm, &mcc, entries[i].vec);
        for (int j = 0; j < VDIM; j++) entries[i].vec[j] = round4(entries[i].vec[j]);
        knn_classify(entries[i].vec, refs, ref_size, &entries[i].approved, &entries[i].fraud_score);
        entries[i].fraud_score = round4(entries[i].fraud_score);
    }

    /* Stats */
    int fraud_n = 0, legit_n = 0, edge_n = 0;
    for (int i = 0; i < payload_size; i++) {
        if (entries[i].approved) legit_n++; else fraud_n++;
        if (entries[i].fraud_score == THRESHOLD) edge_n++;
    }

    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "references_checksum_sha256", refs_checksum);

    cJSON *stats = cJSON_AddObjectToObject(root, "stats");
    cJSON_AddItemToObject(stats, "total",           jnum(payload_size));
    cJSON_AddItemToObject(stats, "fraud_count",     jnum(fraud_n));
    cJSON_AddItemToObject(stats, "legit_count",     jnum(legit_n));
    cJSON_AddItemToObject(stats, "fraud_rate",      jnum(round4((double)fraud_n / payload_size)));
    cJSON_AddItemToObject(stats, "legit_rate",      jnum(round4((double)legit_n / payload_size)));
    cJSON_AddItemToObject(stats, "edge_case_count", jnum(edge_n));
    cJSON_AddItemToObject(stats, "edge_case_rate",  jnum(round4((double)edge_n / payload_size)));

    cJSON *arr = cJSON_AddArrayToObject(root, "entries");
    for (int i = 0; i < payload_size; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddItemToObject(entry, "request", request_to_json(&entries[i].req));

        cJSON *info = cJSON_AddObjectToObject(entry, "info");
        cJSON_AddItemToObject(info, "vector", jnum_array(entries[i].vec, VDIM));

        cJSON *resp = cJSON_AddObjectToObject(info, "expected_response");
        cJSON_AddBoolToObject(resp, "approved", entries[i].approved);
        cJSON_AddItemToObject(resp, "fraud_score", jnum(entries[i].fraud_score));

        cJSON_AddItemToArray(arr, entry);
    }

    write_json_file(payloads_out, root);
    cJSON_Delete(root);

    printf("  -> %s (%d payloads)\n", payloads_out, payload_size);
    printf("Stats: total=%d fraud=%d (%.1f%%) legit=%d (%.1f%%) edge=%d (%.1f%%)\n",
           payload_size, fraud_n, 100.0 * fraud_n / payload_size,
           legit_n, 100.0 * legit_n / payload_size,
           edge_n, 100.0 * edge_n / payload_size);

    free(refs);
    free(entries);
    return 0;
}
