/*
 * test_vectorize.c — TDD-spec (RED) para as 14 dimensoes de vetorizacao
 *
 * R01-R36: cobrindo clamp, todas as dimensoes individualmente, parse ISO 8601,
 * e os dois exemplos end-to-end da spec (legitimo e fraudulento).
 *
 * Cada funcao retorna 0 (pass) ou 1 (fail).
 * Os stubs em vectorize.c retornam 0.0f, portanto TODOS devem falhar (RED).
 */

#include "test.h"
#include "vectorize.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * R01-R03: clamp01
 * ------------------------------------------------------------------------- */

/* R01: clamp01 de valor negativo deve retornar 0.0 */
static int test_clamp01_negative(void)
{
    ASSERT_FLOAT_EQ(clamp01(-0.5f), 0.0f, 1e-6f);
    return 0;
}

/* R02: clamp01 de valor acima de 1 deve retornar 1.0 */
static int test_clamp01_above_one(void)
{
    ASSERT_FLOAT_EQ(clamp01(1.5f), 1.0f, 1e-6f);
    return 0;
}

/* R03: clamp01 de valor dentro do intervalo deve ser preservado */
static int test_clamp01_within_range(void)
{
    ASSERT_FLOAT_EQ(clamp01(0.5f), 0.5f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R04-R06: vec_amount — dim[0] = clamp(amount / 10000)
 * ------------------------------------------------------------------------- */

/* R04: amount=500 → 500/10000 = 0.05 */
static int test_vec_amount_normal(void)
{
    ASSERT_FLOAT_EQ(vec_amount(500.0f), 0.05f, 1e-6f);
    return 0;
}

/* R05: amount=15000 → clamp(15000/10000) = clamp(1.5) = 1.0 */
static int test_vec_amount_overflow(void)
{
    ASSERT_FLOAT_EQ(vec_amount(15000.0f), 1.0f, 1e-6f);
    return 0;
}

/* R06: amount=0 → 0/10000 = 0.0 */
static int test_vec_amount_zero(void)
{
    ASSERT_FLOAT_EQ(vec_amount(0.0f), 0.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R07-R08: vec_installments — dim[1] = clamp(installments / 12)
 * ------------------------------------------------------------------------- */

/* R07: installments=6 → 6/12 = 0.5 */
static int test_vec_installments_normal(void)
{
    ASSERT_FLOAT_EQ(vec_installments(6), 0.5f, 1e-6f);
    return 0;
}

/* R08: installments=15 → clamp(15/12) = clamp(1.25) = 1.0 */
static int test_vec_installments_overflow(void)
{
    ASSERT_FLOAT_EQ(vec_installments(15), 1.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R09-R10: vec_amount_vs_avg — dim[2] = clamp((amount/avg)/10)
 * ------------------------------------------------------------------------- */

/* R09: amount=100, avg=200 → (100/200)/10 = 0.5/10 = 0.05 */
static int test_vec_amount_vs_avg_normal(void)
{
    ASSERT_FLOAT_EQ(vec_amount_vs_avg(100.0f, 200.0f), 0.05f, 1e-6f);
    return 0;
}

/* R10: avg=0 nao deve causar crash; resultado deve ser um float valido [0,1] */
static int test_vec_amount_vs_avg_zero_avg(void)
{
    float result = vec_amount_vs_avg(100.0f, 0.0f);
    /* Resultado deve ser um numero finito em [0.0, 1.0] ou exatamente 1.0
     * (overflow tratado por clamp). Nao pode ser NaN nem Inf. */
    ASSERT_TRUE(result >= 0.0f && result <= 1.0f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R11-R13: vec_hour_of_day — dim[3] = hour / 23 (UTC)
 * ------------------------------------------------------------------------- */

/* R11: hora 00 → 0/23 = 0.0 */
static int test_vec_hour_midnight(void)
{
    ASSERT_FLOAT_EQ(vec_hour_of_day("2026-03-11T00:00:00Z"), 0.0f, 1e-6f);
    return 0;
}

/* R12: hora 23 → 23/23 = 1.0 */
static int test_vec_hour_last_hour(void)
{
    ASSERT_FLOAT_EQ(vec_hour_of_day("2026-03-11T23:00:00Z"), 1.0f, 1e-6f);
    return 0;
}

/* R13: hora 18 → 18/23 ≈ 0.7826 (do exemplo legitimo da spec) */
static int test_vec_hour_spec_example(void)
{
    /* 18/23 = 0.78260... */
    ASSERT_FLOAT_EQ(vec_hour_of_day("2026-03-11T18:45:53Z"),
                    18.0f / 23.0f, 1e-3f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R14-R16: vec_day_of_week — dim[4] = dow / 6 (Mon=0, Sun=6, ISO)
 * ------------------------------------------------------------------------- */

/* R14: 2026-03-09 = Monday → ISO dow 0 → 0/6 = 0.0 */
static int test_vec_dow_monday(void)
{
    ASSERT_FLOAT_EQ(vec_day_of_week("2026-03-09T12:00:00Z"), 0.0f, 1e-6f);
    return 0;
}

/* R15: 2026-03-15 = Sunday → ISO dow 6 → 6/6 = 1.0 */
static int test_vec_dow_sunday(void)
{
    ASSERT_FLOAT_EQ(vec_day_of_week("2026-03-15T12:00:00Z"), 1.0f, 1e-6f);
    return 0;
}

/* R16: 2026-03-11 = Wednesday → ISO dow 2 → 2/6 ≈ 0.3333 */
static int test_vec_dow_wednesday_spec_example(void)
{
    ASSERT_FLOAT_EQ(vec_day_of_week("2026-03-11T18:45:53Z"),
                    2.0f / 6.0f, 1e-3f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R17: parse_day_of_week — multiplas datas conhecidas retornam ISO correto
 * ------------------------------------------------------------------------- */

/* R17: verifica mapping ISO (Mon=0..Sun=6) para 4 datas conhecidas */
static int test_parse_day_of_week_multiple_dates(void)
{
    /* 2026-03-09 = Monday = 0 */
    ASSERT_INT_EQ(parse_day_of_week("2026-03-09T00:00:00Z"), 0);
    /* 2026-03-10 = Tuesday = 1 */
    ASSERT_INT_EQ(parse_day_of_week("2026-03-10T00:00:00Z"), 1);
    /* 2026-03-14 = Saturday = 5 */
    ASSERT_INT_EQ(parse_day_of_week("2026-03-14T00:00:00Z"), 5);
    /* 2026-03-15 = Sunday = 6 */
    ASSERT_INT_EQ(parse_day_of_week("2026-03-15T00:00:00Z"), 6);
    return 0;
}

/* -------------------------------------------------------------------------
 * R18-R20: vec_minutes_since_last — dim[5]
 * ------------------------------------------------------------------------- */

/* R18: last_tx=null (has_last=0) → sentinel -1.0 */
static int test_vec_minutes_since_no_last_tx(void)
{
    /* Quando has_last_tx=0, a funcao ignora os timestamps e retorna -1.0 */
    ASSERT_FLOAT_EQ(vec_minutes_since_last("2026-03-11T18:45:53Z", NULL),
                    -1.0f, 1e-6f);
    return 0;
}

/* R19: diff=227.3min → clamp(227.3/1440) ≈ 0.1578
 * "2026-03-11T18:45:53Z" - "2026-03-11T14:58:35Z" = 3h47m18s = 227.3min */
static int test_vec_minutes_since_normal(void)
{
    float result = compute_minutes_diff("2026-03-11T18:45:53Z",
                                        "2026-03-11T14:58:35Z");
    /* Diferenca = 18*60+45+53/60 - (14*60+58+35/60) = 1125.883 - 898.583 = 227.3 min */
    ASSERT_FLOAT_EQ(result, 227.3f, 0.5f);

    float normalized = clamp01(result / MAX_MINUTES);
    /* 227.3 / 1440 ≈ 0.1578 */
    ASSERT_FLOAT_EQ(normalized, 227.3f / 1440.0f, 1e-3f);
    return 0;
}

/* R20: minutos > 1440 → clamp em 1.0 */
static int test_vec_minutes_since_overflow(void)
{
    /* Forcando um diff grande (mais de 24h): nao ha como passar por timestamp
     * sem construir datas fixas, entao testamos clamp direto */
    float big_minutes = 2880.0f; /* 48h */
    ASSERT_FLOAT_EQ(clamp01(big_minutes / MAX_MINUTES), 1.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R21-R23: vec_km_from_last — dim[6]
 * ------------------------------------------------------------------------- */

/* R21: has_last_tx=0 → sentinel -1.0 */
static int test_vec_km_from_last_no_last_tx(void)
{
    ASSERT_FLOAT_EQ(vec_km_from_last(0.0f, 0), -1.0f, 1e-6f);
    return 0;
}

/* R22: km=500, has_last=1 → 500/1000 = 0.5 */
static int test_vec_km_from_last_normal(void)
{
    ASSERT_FLOAT_EQ(vec_km_from_last(500.0f, 1), 0.5f, 1e-6f);
    return 0;
}

/* R23: km=1500, has_last=1 → clamp(1500/1000) = 1.0 */
static int test_vec_km_from_last_overflow(void)
{
    ASSERT_FLOAT_EQ(vec_km_from_last(1500.0f, 1), 1.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R24: vec_km_from_home — dim[7] = clamp(km / 1000)
 * ------------------------------------------------------------------------- */

/* R24: km=29.23 → 29.23/1000 ≈ 0.02923 (do exemplo legitimo da spec) */
static int test_vec_km_from_home_spec_example(void)
{
    ASSERT_FLOAT_EQ(vec_km_from_home(29.23f), 29.23f / 1000.0f, 1e-4f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R25-R26: vec_tx_count_24h — dim[8] = clamp(count / 20)
 * ------------------------------------------------------------------------- */

/* R25: count=10 → 10/20 = 0.5 */
static int test_vec_tx_count_24h_normal(void)
{
    ASSERT_FLOAT_EQ(vec_tx_count_24h(10), 0.5f, 1e-6f);
    return 0;
}

/* R26: count=25 → clamp(25/20) = clamp(1.25) = 1.0 */
static int test_vec_tx_count_24h_overflow(void)
{
    ASSERT_FLOAT_EQ(vec_tx_count_24h(25), 1.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R27: vec_is_online — dim[9] = 1 se online, 0 se nao
 * ------------------------------------------------------------------------- */

/* R27: is_online=1 → 1.0; is_online=0 → 0.0 */
static int test_vec_is_online(void)
{
    ASSERT_FLOAT_EQ(vec_is_online(1), 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(vec_is_online(0), 0.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R28: vec_card_present — dim[10] = 1 se presente, 0 se nao
 * ------------------------------------------------------------------------- */

/* R28: card_present=1 → 1.0; card_present=0 → 0.0 */
static int test_vec_card_present(void)
{
    ASSERT_FLOAT_EQ(vec_card_present(1), 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(vec_card_present(0), 0.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R29-R30: vec_unknown_merchant — dim[11]
 * 1 se merchant.id NAO esta em known_merchants, 0 se esta
 * ------------------------------------------------------------------------- */

/* R29: "MERC-999" nao esta em ["MERC-001"] → 1.0 (desconhecido) */
static int test_vec_unknown_merchant_unknown(void)
{
    const char *known[] = { "MERC-001" };
    ASSERT_FLOAT_EQ(vec_unknown_merchant("MERC-999", known, 1), 1.0f, 1e-6f);
    return 0;
}

/* R30: "MERC-001" esta em ["MERC-001"] → 0.0 (conhecido) */
static int test_vec_unknown_merchant_known(void)
{
    const char *known[] = { "MERC-001" };
    ASSERT_FLOAT_EQ(vec_unknown_merchant("MERC-001", known, 1), 0.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R31-R33: vec_mcc_risk — dim[12]
 * ------------------------------------------------------------------------- */

/* R31: mcc="5411" (supermercado) → 0.15 (valor do mcc_risk.json) */
static int test_vec_mcc_risk_mapped_5411(void)
{
    ASSERT_FLOAT_EQ(vec_mcc_risk("5411"), 0.15f, 1e-6f);
    return 0;
}

/* R32: mcc="9999" nao mapeado → default 0.5 */
static int test_vec_mcc_risk_unmapped(void)
{
    ASSERT_FLOAT_EQ(vec_mcc_risk("9999"), 0.5f, 1e-6f);
    return 0;
}

/* R33: mcc=NULL nao deve causar crash → default 0.5 */
static int test_vec_mcc_risk_null(void)
{
    ASSERT_FLOAT_EQ(vec_mcc_risk(NULL), 0.5f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R34: vec_merchant_avg_amount — dim[13] = clamp(avg / 10000)
 * ------------------------------------------------------------------------- */

/* R34: avg=298.95 → 298.95/10000 ≈ 0.029895 */
static int test_vec_merchant_avg_amount_normal(void)
{
    ASSERT_FLOAT_EQ(vec_merchant_avg_amount(298.95f),
                    298.95f / 10000.0f, 1e-5f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R35: vectorize() com exemplo LEGITIMO da spec
 *
 * Payload:
 *   transaction:  amount=41.12, installments=2, requested_at="2026-03-11T18:45:53Z"
 *   customer:     avg_amount=82.24, tx_count_24h=3, known_merchants=["MERC-003","MERC-016"]
 *   merchant:     id="MERC-016", mcc="5411", avg_amount=60.25
 *   terminal:     is_online=false, card_present=true, km_from_home=29.23
 *   last_transaction: null
 *
 * Vetor esperado (da spec):
 *   [0.0041, 0.1667, 0.05, 0.7826, 0.3333, -1, -1, 0.0292, 0.15, 0, 1, 0, 0.15, 0.006]
 * ------------------------------------------------------------------------- */
static int test_vectorize_legit_spec_example(void)
{
    const char *known[] = { "MERC-003", "MERC-016" };

    transaction_t tx;
    tx.amount                = 41.12f;
    tx.installments          = 2;
    tx.requested_at          = "2026-03-11T18:45:53Z";
    tx.customer_avg_amount   = 82.24f;
    tx.customer_tx_count_24h = 3;
    tx.known_merchants       = known;
    tx.num_known_merchants   = 2;
    tx.merchant_id           = "MERC-016";
    tx.merchant_mcc          = "5411";
    tx.merchant_avg_amount   = 60.25f;
    tx.is_online             = 0;  /* false */
    tx.card_present          = 1;  /* true */
    tx.km_from_home          = 29.23f;
    tx.has_last_tx           = 0;
    tx.last_tx_timestamp     = NULL;
    tx.last_tx_km            = 0.0f;

    float vec[VECTOR_DIMS];
    vectorize(&tx, vec);

    /* dim[0]: 41.12/10000 = 0.004112 */
    ASSERT_FLOAT_EQ(vec[0],  41.12f / 10000.0f, 1e-3f);
    /* dim[1]: 2/12 ≈ 0.1667 */
    ASSERT_FLOAT_EQ(vec[1],  2.0f / 12.0f, 1e-3f);
    /* dim[2]: (41.12/82.24)/10 = 0.5/10 = 0.05 */
    ASSERT_FLOAT_EQ(vec[2],  0.05f, 1e-3f);
    /* dim[3]: 18/23 ≈ 0.7826 */
    ASSERT_FLOAT_EQ(vec[3],  18.0f / 23.0f, 1e-3f);
    /* dim[4]: Wednesday=2 → 2/6 ≈ 0.3333 */
    ASSERT_FLOAT_EQ(vec[4],  2.0f / 6.0f, 1e-3f);
    /* dim[5]: last_tx=null → sentinel -1 */
    ASSERT_FLOAT_EQ(vec[5],  -1.0f, 1e-6f);
    /* dim[6]: last_tx=null → sentinel -1 */
    ASSERT_FLOAT_EQ(vec[6],  -1.0f, 1e-6f);
    /* dim[7]: 29.23/1000 ≈ 0.02923 */
    ASSERT_FLOAT_EQ(vec[7],  29.23f / 1000.0f, 1e-3f);
    /* dim[8]: 3/20 = 0.15 */
    ASSERT_FLOAT_EQ(vec[8],  3.0f / 20.0f, 1e-3f);
    /* dim[9]: is_online=false → 0.0 */
    ASSERT_FLOAT_EQ(vec[9],  0.0f, 1e-6f);
    /* dim[10]: card_present=true → 1.0 */
    ASSERT_FLOAT_EQ(vec[10], 1.0f, 1e-6f);
    /* dim[11]: MERC-016 esta em known_merchants → 0.0 */
    ASSERT_FLOAT_EQ(vec[11], 0.0f, 1e-6f);
    /* dim[12]: mcc="5411" → 0.15 */
    ASSERT_FLOAT_EQ(vec[12], 0.15f, 1e-6f);
    /* dim[13]: 60.25/10000 = 0.006025 */
    ASSERT_FLOAT_EQ(vec[13], 60.25f / 10000.0f, 1e-3f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R36: vectorize() com exemplo FRAUDULENTO da spec
 *
 * Payload:
 *   transaction:  amount=9505.97, installments=10, requested_at="2026-03-14T05:15:12Z"
 *   customer:     avg_amount=81.28, tx_count_24h=20, known_merchants=["MERC-008","MERC-007","MERC-005"]
 *   merchant:     id="MERC-068", mcc="7802", avg_amount=54.86
 *   terminal:     is_online=false, card_present=true, km_from_home=952.27
 *   last_transaction: null
 *
 * Vetor esperado (da spec):
 *   [0.9506, 0.8333, 1.0, 0.2174, 0.8333, -1, -1, 0.9523, 1.0, 0, 1, 1, 0.75, 0.0055]
 * ------------------------------------------------------------------------- */
static int test_vectorize_fraud_spec_example(void)
{
    const char *known[] = { "MERC-008", "MERC-007", "MERC-005" };

    transaction_t tx;
    tx.amount                = 9505.97f;
    tx.installments          = 10;
    tx.requested_at          = "2026-03-14T05:15:12Z";
    tx.customer_avg_amount   = 81.28f;
    tx.customer_tx_count_24h = 20;
    tx.known_merchants       = known;
    tx.num_known_merchants   = 3;
    tx.merchant_id           = "MERC-068";
    tx.merchant_mcc          = "7802";
    tx.merchant_avg_amount   = 54.86f;
    tx.is_online             = 0;  /* false */
    tx.card_present          = 1;  /* true */
    tx.km_from_home          = 952.27f;
    tx.has_last_tx           = 0;
    tx.last_tx_timestamp     = NULL;
    tx.last_tx_km            = 0.0f;

    float vec[VECTOR_DIMS];
    vectorize(&tx, vec);

    /* dim[0]: 9505.97/10000 ≈ 0.9506 */
    ASSERT_FLOAT_EQ(vec[0],  9505.97f / 10000.0f, 1e-3f);
    /* dim[1]: 10/12 ≈ 0.8333 */
    ASSERT_FLOAT_EQ(vec[1],  10.0f / 12.0f, 1e-3f);
    /* dim[2]: (9505.97/81.28)/10 ≈ 11.697 → clamp = 1.0 */
    ASSERT_FLOAT_EQ(vec[2],  1.0f, 1e-3f);
    /* dim[3]: hour=5 → 5/23 ≈ 0.2174 */
    ASSERT_FLOAT_EQ(vec[3],  5.0f / 23.0f, 1e-3f);
    /* dim[4]: 2026-03-14 = Saturday = ISO dow 5 → 5/6 ≈ 0.8333 */
    ASSERT_FLOAT_EQ(vec[4],  5.0f / 6.0f, 1e-3f);
    /* dim[5]: last_tx=null → sentinel -1 */
    ASSERT_FLOAT_EQ(vec[5],  -1.0f, 1e-6f);
    /* dim[6]: last_tx=null → sentinel -1 */
    ASSERT_FLOAT_EQ(vec[6],  -1.0f, 1e-6f);
    /* dim[7]: 952.27/1000 ≈ 0.9523 */
    ASSERT_FLOAT_EQ(vec[7],  952.27f / 1000.0f, 1e-3f);
    /* dim[8]: 20/20 = 1.0 */
    ASSERT_FLOAT_EQ(vec[8],  1.0f, 1e-6f);
    /* dim[9]: is_online=false → 0.0 */
    ASSERT_FLOAT_EQ(vec[9],  0.0f, 1e-6f);
    /* dim[10]: card_present=true → 1.0 */
    ASSERT_FLOAT_EQ(vec[10], 1.0f, 1e-6f);
    /* dim[11]: MERC-068 NAO esta em known_merchants → 1.0 (desconhecido) */
    ASSERT_FLOAT_EQ(vec[11], 1.0f, 1e-6f);
    /* dim[12]: mcc="7802" → 0.75 */
    ASSERT_FLOAT_EQ(vec[12], 0.75f, 1e-6f);
    /* dim[13]: 54.86/10000 = 0.005486 */
    ASSERT_FLOAT_EQ(vec[13], 54.86f / 10000.0f, 1e-3f);
    return 0;
}

/* -------------------------------------------------------------------------
 * Suite runner — chamada de test_main.c
 * ------------------------------------------------------------------------- */

int run_vectorize_tests(void)
{
    printf("\n=== Vectorize Tests ===\n");

    RUN_TEST(test_clamp01_negative);
    RUN_TEST(test_clamp01_above_one);
    RUN_TEST(test_clamp01_within_range);

    RUN_TEST(test_vec_amount_normal);
    RUN_TEST(test_vec_amount_overflow);
    RUN_TEST(test_vec_amount_zero);

    RUN_TEST(test_vec_installments_normal);
    RUN_TEST(test_vec_installments_overflow);

    RUN_TEST(test_vec_amount_vs_avg_normal);
    RUN_TEST(test_vec_amount_vs_avg_zero_avg);

    RUN_TEST(test_vec_hour_midnight);
    RUN_TEST(test_vec_hour_last_hour);
    RUN_TEST(test_vec_hour_spec_example);

    RUN_TEST(test_vec_dow_monday);
    RUN_TEST(test_vec_dow_sunday);
    RUN_TEST(test_vec_dow_wednesday_spec_example);
    RUN_TEST(test_parse_day_of_week_multiple_dates);

    RUN_TEST(test_vec_minutes_since_no_last_tx);
    RUN_TEST(test_vec_minutes_since_normal);
    RUN_TEST(test_vec_minutes_since_overflow);

    RUN_TEST(test_vec_km_from_last_no_last_tx);
    RUN_TEST(test_vec_km_from_last_normal);
    RUN_TEST(test_vec_km_from_last_overflow);

    RUN_TEST(test_vec_km_from_home_spec_example);

    RUN_TEST(test_vec_tx_count_24h_normal);
    RUN_TEST(test_vec_tx_count_24h_overflow);

    RUN_TEST(test_vec_is_online);
    RUN_TEST(test_vec_card_present);

    RUN_TEST(test_vec_unknown_merchant_unknown);
    RUN_TEST(test_vec_unknown_merchant_known);

    RUN_TEST(test_vec_mcc_risk_mapped_5411);
    RUN_TEST(test_vec_mcc_risk_unmapped);
    RUN_TEST(test_vec_mcc_risk_null);

    RUN_TEST(test_vec_merchant_avg_amount_normal);

    RUN_TEST(test_vectorize_legit_spec_example);
    RUN_TEST(test_vectorize_fraud_spec_example);

    return _tests_failed;
}
