/*
 * vectorize.c — Implementacao GREEN das 14 dimensoes de vetorizacao
 *
 * Regras criticas:
 *   - clamp01: fmaxf(0.0f, fminf(1.0f, x))
 *   - Dims 5,6 = SENTINEL (-1.0f) quando has_last_tx == 0 ou last_ts == NULL
 *   - MCC nao mapeado → 0.5f (NAO zero, NAO erro)
 *   - day_of_week: Monday=0, Sunday=6 (ISO 8601) — C tm_wday usa Sunday=0
 *   - parse ISO 8601 manual (strptime NAO e C standard)
 *   - timegm NAO e C standard — calculo manual de epoch
 */

#include "vectorize.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * clamp01
 * ------------------------------------------------------------------------- */

float clamp01(float x)
{
    return fmaxf(0.0f, fminf(1.0f, x));
}

/* -------------------------------------------------------------------------
 * Parse helpers — ISO 8601: "YYYY-MM-DDTHH:MM:SSZ"
 *   Y: [0..3]
 *   M: [5..6]
 *   D: [8..9]
 *   H: [11..12]
 *   m: [14..15]
 *   S: [17..18]
 * ------------------------------------------------------------------------- */

/* Extrai inteiro de 2 digitos na posicao pos da string iso */
static int extract2(const char *iso, int pos)
{
    return (iso[pos] - '0') * 10 + (iso[pos + 1] - '0');
}

/* Extrai inteiro de 4 digitos na posicao pos da string iso */
static int extract4(const char *iso, int pos)
{
    return (iso[pos]     - '0') * 1000
         + (iso[pos + 1] - '0') * 100
         + (iso[pos + 2] - '0') * 10
         + (iso[pos + 3] - '0');
}

int parse_utc_hour(const char *iso_timestamp)
{
    if (!iso_timestamp) return 0;
    return extract2(iso_timestamp, 11);
}

/*
 * Algoritmo de Tomohiko Sakamoto — retorna 0=Sunday..6=Saturday
 * Conversor para ISO 8601: iso_dow = (result + 6) % 7 → Mon=0, Sun=6
 */
static int sakamoto_dow(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

int parse_day_of_week(const char *iso_timestamp)
{
    if (!iso_timestamp) return 0;
    int y = extract4(iso_timestamp, 0);
    int m = extract2(iso_timestamp, 5);
    int d = extract2(iso_timestamp, 8);
    int sak = sakamoto_dow(y, m, d); /* 0=Sun, 1=Mon, ..., 6=Sat */
    return (sak + 6) % 7;            /* Mon=0, Tue=1, ..., Sun=6 */
}

/*
 * Converte ISO 8601 UTC para segundos desde epoch (1970-01-01T00:00:00Z).
 * NAO usa mktime (assume localtime). Implementacao manual de UTC epoch.
 *
 * Formula: dias acumulados desde 1970-01-01 * 86400 + horas*3600 + min*60 + seg
 */
static long long iso_to_epoch_seconds(const char *iso)
{
    int y = extract4(iso, 0);
    int m = extract2(iso, 5);
    int d = extract2(iso, 8);
    int h = extract2(iso, 11);
    int mn = extract2(iso, 14);
    int s  = extract2(iso, 17);

    /*
     * Calculo de dias julianos desde 1970-01-01 sem usar mktime:
     * Algoritmo baseado em "Julian Day Number" simplificado para Unix epoch.
     *
     * Numero de dias desde 1970-01-01 para (y, m, d):
     *   Ajuste para meses: jan e fev do ano y sao tratados como mes 13 e 14 do ano y-1
     */
    int y2 = (m <= 2) ? y - 1 : y;
    int m2 = (m <= 2) ? m + 12 : m;

    /* Dias acumulados ate o fim de cada mes (formula astronomica) */
    long long jdn = (long long)d
                  + (153LL * m2 - 457LL) / 5
                  + 365LL * y2
                  + y2 / 4
                  - y2 / 100
                  + y2 / 400
                  + 1721119LL;  /* constante astronomica para JDN */

    /* JDN para 1970-01-01 = 2440588 */
    long long days_since_epoch = jdn - 2440588LL;

    return days_since_epoch * 86400LL
           + (long long)h  * 3600LL
           + (long long)mn * 60LL
           + (long long)s;
}

float compute_minutes_diff(const char *ts_current, const char *ts_last)
{
    if (!ts_current || !ts_last) return 0.0f;
    long long epoch_curr = iso_to_epoch_seconds(ts_current);
    long long epoch_last = iso_to_epoch_seconds(ts_last);
    long long diff_sec   = epoch_curr - epoch_last;
    return (float)diff_sec / 60.0f;
}

/* -------------------------------------------------------------------------
 * Dimensoes individuais
 * ------------------------------------------------------------------------- */

/* dim[0]: clamp(amount / 10000) */
float vec_amount(float amount)
{
    return clamp01(amount / MAX_AMOUNT);
}

/* dim[1]: clamp(installments / 12) */
float vec_installments(int installments)
{
    return clamp01((float)installments / MAX_INSTALLMENTS);
}

/* dim[2]: avg <= 0 → 0.0f (evita div por zero / NaN); senao clamp((amount/avg)/10) */
float vec_amount_vs_avg(float amount, float avg_amount)
{
    if (avg_amount <= 0.0f) return 0.0f;
    return clamp01((amount / avg_amount) / AMOUNT_VS_AVG_RATIO);
}

/* dim[3]: hour / 23 (UTC) */
float vec_hour_of_day(const char *iso_timestamp)
{
    return (float)parse_utc_hour(iso_timestamp) / 23.0f;
}

/* dim[4]: dow / 6 (Mon=0, Sun=6) */
float vec_day_of_week(const char *iso_timestamp)
{
    return (float)parse_day_of_week(iso_timestamp) / 6.0f;
}

/*
 * dim[5]: SENTINEL se last_ts == NULL, senao clamp(minutes_diff / 1440)
 * A funcao recebe current_ts e last_ts; se last_ts e NULL → sentinel.
 */
float vec_minutes_since_last(const char *current_ts, const char *last_ts)
{
    if (!last_ts) return SENTINEL;
    float minutes = compute_minutes_diff(current_ts, last_ts);
    return clamp01(minutes / MAX_MINUTES);
}

/* dim[6]: SENTINEL se has_last_tx == 0, senao clamp(km / 1000) */
float vec_km_from_last(float km, int has_last_tx)
{
    if (!has_last_tx) return SENTINEL;
    return clamp01(km / MAX_KM);
}

/* dim[7]: clamp(km / 1000) */
float vec_km_from_home(float km)
{
    return clamp01(km / MAX_KM);
}

/* dim[8]: clamp(count / 20) */
float vec_tx_count_24h(int count)
{
    return clamp01((float)count / MAX_TX_COUNT_24H);
}

/* dim[9]: 1.0 se online, 0.0 caso contrario */
float vec_is_online(int is_online)
{
    return is_online ? 1.0f : 0.0f;
}

/* dim[10]: 1.0 se card_present, 0.0 caso contrario */
float vec_card_present(int card_present)
{
    return card_present ? 1.0f : 0.0f;
}

/* dim[11]: 1.0 se merchant_id NAO esta em known_merchants, 0.0 se esta */
float vec_unknown_merchant(const char *merchant_id,
                            const char **known_merchants, int num_known)
{
    if (!merchant_id) return 1.0f;
    for (int i = 0; i < num_known; i++) {
        if (known_merchants[i] && strcmp(merchant_id, known_merchants[i]) == 0) {
            return 0.0f;  /* conhecido */
        }
    }
    return 1.0f;  /* desconhecido */
}

/*
 * dim[12]: lookup na tabela de 10 MCCs conhecidos; default 0.5f
 * Tabela: recursos/mcc_risk.json (10 entradas hardcoded)
 */
float vec_mcc_risk(const char *mcc)
{
    if (!mcc) return MCC_RISK_DEFAULT;

    static const struct { const char *code; float risk; } mcc_table[] = {
        {"5411", 0.15f},
        {"5812", 0.30f},
        {"5912", 0.20f},
        {"5944", 0.45f},
        {"7801", 0.80f},
        {"7802", 0.75f},
        {"7995", 0.85f},
        {"4511", 0.35f},
        {"5311", 0.25f},
        {"5999", 0.50f},
    };

    for (int i = 0; i < 10; i++) {
        if (strcmp(mcc, mcc_table[i].code) == 0) {
            return mcc_table[i].risk;
        }
    }
    return MCC_RISK_DEFAULT;
}

/* dim[13]: clamp(avg / 10000) */
float vec_merchant_avg_amount(float avg_amount)
{
    return clamp01(avg_amount / MAX_MERCHANT_AVG);
}

/* -------------------------------------------------------------------------
 * vectorize() — preenche out[14] com todas as dimensoes
 * ------------------------------------------------------------------------- */

void vectorize(const transaction_t *tx, float out[VECTOR_DIMS])
{
    out[0]  = vec_amount(tx->amount);
    out[1]  = vec_installments(tx->installments);
    out[2]  = vec_amount_vs_avg(tx->amount, tx->customer_avg_amount);
    out[3]  = vec_hour_of_day(tx->requested_at);
    out[4]  = vec_day_of_week(tx->requested_at);
    out[5]  = tx->has_last_tx
              ? vec_minutes_since_last(tx->requested_at, tx->last_tx_timestamp)
              : SENTINEL;
    out[6]  = tx->has_last_tx
              ? vec_km_from_last(tx->last_tx_km, tx->has_last_tx)
              : SENTINEL;
    out[7]  = vec_km_from_home(tx->km_from_home);
    out[8]  = vec_tx_count_24h(tx->customer_tx_count_24h);
    out[9]  = vec_is_online(tx->is_online);
    out[10] = vec_card_present(tx->card_present);
    out[11] = vec_unknown_merchant(tx->merchant_id,
                                   tx->known_merchants,
                                   tx->num_known_merchants);
    out[12] = vec_mcc_risk(tx->merchant_mcc);
    out[13] = vec_merchant_avg_amount(tx->merchant_avg_amount);
}
