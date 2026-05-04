#ifndef VECTORIZE_H
#define VECTORIZE_H

#define VECTOR_DIMS 14
#define SENTINEL -1.0f

/* Normalization constants (resources/normalization.json) */
#define MAX_AMOUNT           10000.0f
#define MAX_INSTALLMENTS     12.0f
#define AMOUNT_VS_AVG_RATIO  10.0f
#define MAX_MINUTES          1440.0f
#define MAX_KM               1000.0f
#define MAX_TX_COUNT_24H     20.0f
#define MAX_MERCHANT_AVG     10000.0f

/* MCC risk default for unmapped codes */
#define MCC_RISK_DEFAULT     0.5f

/* Fraud threshold */
#define FRAUD_THRESHOLD      0.6f
#define KNN_K                5

/* Transaction payload (parsed from JSON) */
typedef struct {
    float amount;
    int   installments;
    const char *requested_at;         /* ISO 8601 UTC */

    float customer_avg_amount;
    int   customer_tx_count_24h;
    const char **known_merchants;     /* array of merchant IDs */
    int   num_known_merchants;

    const char *merchant_id;
    const char *merchant_mcc;
    float merchant_avg_amount;

    int   is_online;                  /* boolean */
    int   card_present;               /* boolean */
    float km_from_home;

    int   has_last_tx;                /* 0 if last_transaction is null */
    const char *last_tx_timestamp;    /* ISO 8601 UTC, NULL if no last tx */
    float last_tx_km;                 /* km_from_current, 0 if no last tx */
} transaction_t;

/* Clamp x to [0.0, 1.0] */
float clamp01(float x);

/* Individual dimension functions — each independently testable */
float vec_amount(float amount);
float vec_installments(int installments);
float vec_amount_vs_avg(float amount, float avg_amount);
float vec_hour_of_day(const char *iso_timestamp);
float vec_day_of_week(const char *iso_timestamp);
float vec_minutes_since_last(const char *current_ts, const char *last_ts);
float vec_km_from_last(float km, int has_last_tx);
float vec_km_from_home(float km);
float vec_tx_count_24h(int count);
float vec_is_online(int is_online);
float vec_card_present(int card_present);
float vec_unknown_merchant(const char *merchant_id,
                           const char **known_merchants, int num_known);
float vec_mcc_risk(const char *mcc);
float vec_merchant_avg_amount(float avg_amount);

/* Full vectorization: fills out[14] from transaction */
void vectorize(const transaction_t *tx, float out[VECTOR_DIMS]);

/* Extract UTC hour (0-23) from ISO 8601 string */
int parse_utc_hour(const char *iso_timestamp);

/* Extract day of week (Mon=0, Sun=6) from ISO 8601 string */
int parse_day_of_week(const char *iso_timestamp);

/* Compute minutes between two ISO 8601 timestamps */
float compute_minutes_diff(const char *ts_current, const char *ts_last);

#endif
