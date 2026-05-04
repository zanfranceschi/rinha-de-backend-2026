/*
 * test_quantize.c — TDD-spec (RED) para quantizacao int16
 *
 * Q1: quantize_f32_to_i16 converte valores conhecidos corretamente
 * Q2: roundtrip precision — dequantizado dentro de 0.0001 do original
 * Q3: sentinel -1.0f -> int16 -10000 (preservado, nao clamped)
 * Q4: distance_sq_i16 matches float distance_sq within 1% tolerance
 * Q5: early exit — tau pequeno causa retorno antecipado > tau
 * Q6: distancia zero (mesmo vetor)
 * Q7: simetria — distance_sq_i16(a,b) == distance_sq_i16(b,a)
 *
 * Cada funcao retorna 0 (pass) ou 1 (fail).
 * Os stubs em quantize.c retornam 0/0 — a maioria deve falhar (RED).
 */

#include "test.h"
#include "quantize.h"
#include "distance.h"

#include <stdint.h>
#include <math.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Q1: quantize_f32_to_i16 converte valores conhecidos corretamente
 *
 * Verifica a conversao de float conhecido para int16 esperado.
 * Formula: int16 = (int16_t)(float * QUANT_SCALE)
 * QUANT_SCALE = 10000
 *
 * Exemplos:
 *   0.5f   -> 5000
 *   1.0f   -> 10000
 *   0.0f   -> 0
 *  -1.0f   -> -10000  (sentinel)
 *   0.25f  -> 2500
 * ------------------------------------------------------------------------- */
static int test_quantize_known_values(void)
{
    float src[VECTOR_DIMS];
    int16_t dst[VECTOR_DIMS];

    src[0]  = 0.5f;
    src[1]  = 1.0f;
    src[2]  = 0.0f;
    src[3]  = -1.0f;   /* sentinel */
    src[4]  = 0.25f;
    src[5]  = -1.0f;   /* sentinel */
    src[6]  = -1.0f;   /* sentinel */
    src[7]  = 0.75f;
    src[8]  = 0.1f;
    src[9]  = 0.0f;
    src[10] = 1.0f;
    src[11] = 0.0f;
    src[12] = 0.5f;
    src[13] = 0.0416f;

    quantize_f32_to_i16(src, dst);

    ASSERT_INT_EQ(dst[0],   5000);
    ASSERT_INT_EQ(dst[1],  10000);
    ASSERT_INT_EQ(dst[2],      0);
    ASSERT_INT_EQ(dst[3], -10000);
    ASSERT_INT_EQ(dst[4],   2500);
    ASSERT_INT_EQ(dst[5], -10000);
    ASSERT_INT_EQ(dst[6], -10000);
    ASSERT_INT_EQ(dst[7],   7500);
    ASSERT_INT_EQ(dst[8],   1000);
    ASSERT_INT_EQ(dst[9],      0);
    ASSERT_INT_EQ(dst[10], 10000);
    ASSERT_INT_EQ(dst[11],     0);
    ASSERT_INT_EQ(dst[12],  5000);
    /* dst[13] = round(0.0416 * 10000) = 416 — aceita 415..417 */
    ASSERT_TRUE(dst[13] >= 415 && dst[13] <= 417);

    return 0;
}

/* -------------------------------------------------------------------------
 * Q2: Roundtrip precision — dequantizado dentro de 0.0001 do original
 *
 * Quantiza vetor e converte de volta para float usando a formula inversa:
 *   float_approx = (float)int16 / QUANT_SCALE
 *
 * Para valores no range [0,1] a precisao deve ser melhor que 1/10000 = 0.0001.
 * Sentinels (-1.0f) devem ter a mesma precisao.
 * ------------------------------------------------------------------------- */
static int test_quantize_roundtrip_precision(void)
{
    float src[VECTOR_DIMS];
    int16_t dst[VECTOR_DIMS];

    /* Vetor representativo: legit com sentinel nas dims 5,6 */
    src[0]  = 0.0041f;
    src[1]  = 0.1667f;
    src[2]  = 0.05f;
    src[3]  = 0.7826f;
    src[4]  = 0.3333f;
    src[5]  = -1.0f;    /* sentinel */
    src[6]  = -1.0f;    /* sentinel */
    src[7]  = 0.0292f;
    src[8]  = 0.15f;
    src[9]  = 0.0f;
    src[10] = 1.0f;
    src[11] = 0.0f;
    src[12] = 0.15f;
    src[13] = 0.006f;

    quantize_f32_to_i16(src, dst);

    /* Dequantizar e comparar com original */
    for (int i = 0; i < VECTOR_DIMS; i++) {
        float recovered = (float)dst[i] / (float)QUANT_SCALE;
        ASSERT_FLOAT_EQ(recovered, src[i], 0.0001f);
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Q3: Sentinel -1.0f e preservado como -10000 em int16
 *
 * Regra critica: dims 5,6 = -1.0f quando last_transaction e null.
 * Este valor sentinel nao deve ser clamped, truncado, ou alterado.
 * -1.0f * 10000 = -10000, que e representavel em int16 (range -32768..32767).
 *
 * Verifica tambem que o valor nao e confundido com 0 (que seria errado)
 * ou com qualquer outro valor.
 * ------------------------------------------------------------------------- */
static int test_quantize_sentinel(void)
{
    float src[VECTOR_DIMS];
    int16_t dst[VECTOR_DIMS];

    /* Vetor todo zero, exceto sentinels nas dims 5 e 6 */
    for (int i = 0; i < VECTOR_DIMS; i++) src[i] = 0.0f;
    src[5] = -1.0f;
    src[6] = -1.0f;

    quantize_f32_to_i16(src, dst);

    /* Dims 5 e 6: devem ser exatamente -10000 */
    ASSERT_INT_EQ(dst[5], -10000);
    ASSERT_INT_EQ(dst[6], -10000);

    /* Demais dims: devem ser 0 */
    for (int i = 0; i < VECTOR_DIMS; i++) {
        if (i == 5 || i == 6) continue;
        ASSERT_INT_EQ(dst[i], 0);
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Q4: distance_sq_i16 produz resultado equivalente ao distance_sq float
 *
 * Quantizacao introduz erro de arredondamento. A distancia em int16 deve
 * aproximar a distancia float dentro de 1% de tolerancia.
 *
 * Relacao entre as escalas:
 *   float dist_sq         — soma de (a[i]-b[i])^2 onde a,b in [-1,1]
 *   int16 dist_sq_i16     — soma de (ai-bi)^2 onde ai,bi in [-10000,10000]
 *   conversao: float_dist = int16_dist / (QUANT_SCALE^2)
 *
 * Usa dois vetores com distancia conhecida ~5.7468 (do test_distance.c).
 * ------------------------------------------------------------------------- */
static int test_distance_i16_matches_float(void)
{
    float a_f[VECTOR_DIMS], b_f[VECTOR_DIMS];
    int16_t a_i[VECTOR_DIMS], b_i[VECTOR_DIMS];

    /* Vetor a: exemplo legit da spec */
    a_f[0]=0.0041f; a_f[1]=0.1667f; a_f[2]=0.05f;   a_f[3]=0.7826f;
    a_f[4]=0.3333f; a_f[5]=-1.0f;   a_f[6]=-1.0f;   a_f[7]=0.0292f;
    a_f[8]=0.15f;   a_f[9]=0.0f;    a_f[10]=1.0f;   a_f[11]=0.0f;
    a_f[12]=0.15f;  a_f[13]=0.006f;

    /* Vetor b: exemplo fraud da spec */
    b_f[0]=0.9506f; b_f[1]=0.8333f; b_f[2]=1.0f;    b_f[3]=0.2174f;
    b_f[4]=0.8333f; b_f[5]=-1.0f;   b_f[6]=-1.0f;   b_f[7]=0.9523f;
    b_f[8]=1.0f;    b_f[9]=0.0f;    b_f[10]=1.0f;   b_f[11]=1.0f;
    b_f[12]=0.75f;  b_f[13]=0.0055f;

    quantize_f32_to_i16(a_f, a_i);
    quantize_f32_to_i16(b_f, b_i);

    float float_dist = distance_sq(a_f, b_f);

    /* Sem tau (INT64_MAX): sem early exit */
    int64_t i16_dist = distance_sq_i16(a_i, b_i, INT64_MAX);

    /* Converter de volta para escala float */
    float i16_dist_f32 = i16_dist_to_f32(i16_dist);

    /* Deve estar dentro de 1% do valor float */
    float ratio = fabsf(i16_dist_f32 - float_dist) / float_dist;
    ASSERT_TRUE(ratio < 0.01f);

    /* Sanidade: valor float conhecido ~5.7468 */
    ASSERT_FLOAT_EQ(float_dist, 5.7468f, 0.02f);

    return 0;
}

/* -------------------------------------------------------------------------
 * Q5: Early exit — tau pequeno causa retorno antecipado > tau
 *
 * Regra de performance: se a soma parcial ja excede tau,
 * distance_sq_i16 deve retornar tau+1 imediatamente.
 *
 * Usa vetores com distancia grande (a_i, b_i do Q4, dist ~5.7468)
 * e seta tau = 0 para forcar early exit ja na primeira dimensao.
 * ------------------------------------------------------------------------- */
static int test_distance_i16_early_exit(void)
{
    float a_f[VECTOR_DIMS], b_f[VECTOR_DIMS];
    int16_t a_i[VECTOR_DIMS], b_i[VECTOR_DIMS];

    /* Vetores com distancia grande entre si */
    a_f[0]=0.0041f; a_f[1]=0.1667f; a_f[2]=0.05f;   a_f[3]=0.7826f;
    a_f[4]=0.3333f; a_f[5]=-1.0f;   a_f[6]=-1.0f;   a_f[7]=0.0292f;
    a_f[8]=0.15f;   a_f[9]=0.0f;    a_f[10]=1.0f;   a_f[11]=0.0f;
    a_f[12]=0.15f;  a_f[13]=0.006f;

    b_f[0]=0.9506f; b_f[1]=0.8333f; b_f[2]=1.0f;    b_f[3]=0.2174f;
    b_f[4]=0.8333f; b_f[5]=-1.0f;   b_f[6]=-1.0f;   b_f[7]=0.9523f;
    b_f[8]=1.0f;    b_f[9]=0.0f;    b_f[10]=1.0f;   b_f[11]=1.0f;
    b_f[12]=0.75f;  b_f[13]=0.0055f;

    quantize_f32_to_i16(a_f, a_i);
    quantize_f32_to_i16(b_f, b_i);

    /*
     * tau = 0: qualquer diferenca entre a e b (dim 0 ja difere muito)
     * deve triggerar early exit imediatamente.
     */
    int64_t tau = 0;
    int64_t result = distance_sq_i16(a_i, b_i, tau);

    /* Deve retornar tau+1 = 1 (early exit ativado) */
    ASSERT_TRUE(result > tau);

    /*
     * tau = INT64_MAX: sem early exit — resultado completo
     * deve ser bem maior que tau=0 result (que foi cortado).
     */
    int64_t full_result = distance_sq_i16(a_i, b_i, INT64_MAX);
    ASSERT_TRUE(full_result > result);

    return 0;
}

/* -------------------------------------------------------------------------
 * Q6: distancia zero — mesmo vetor retorna 0
 *
 * distance_sq_i16(v, v, max) deve ser 0 para qualquer vetor v.
 * Inclui vetores com sentinel -1.0f.
 * ------------------------------------------------------------------------- */
static int test_distance_i16_zero(void)
{
    float src[VECTOR_DIMS];
    int16_t v[VECTOR_DIMS];

    /* Vetor com sentinel nas dims 5,6 */
    src[0]  = 0.0041f;
    src[1]  = 0.1667f;
    src[2]  = 0.05f;
    src[3]  = 0.7826f;
    src[4]  = 0.3333f;
    src[5]  = -1.0f;
    src[6]  = -1.0f;
    src[7]  = 0.0292f;
    src[8]  = 0.15f;
    src[9]  = 0.0f;
    src[10] = 1.0f;
    src[11] = 0.0f;
    src[12] = 0.15f;
    src[13] = 0.006f;

    quantize_f32_to_i16(src, v);

    /* distance_sq_i16(v, v, max) deve ser exatamente 0 */
    int64_t result = distance_sq_i16(v, v, INT64_MAX);
    ASSERT_TRUE(result == 0);

    /* Mesmo teste com vetor de zeros */
    int16_t zeros[VECTOR_DIMS];
    memset(zeros, 0, sizeof(zeros));
    result = distance_sq_i16(zeros, zeros, INT64_MAX);
    ASSERT_TRUE(result == 0);

    return 0;
}

/* -------------------------------------------------------------------------
 * Q7: Simetria — distance_sq_i16(a,b) == distance_sq_i16(b,a)
 *
 * A distancia deve ser simetrica: d(a,b) == d(b,a).
 * Testa com tau=INT64_MAX (sem early exit) para garantir calculo completo.
 * Testa com multiplos pares de vetores.
 * ------------------------------------------------------------------------- */
static int test_distance_i16_symmetry(void)
{
    float a_f[VECTOR_DIMS], b_f[VECTOR_DIMS];
    int16_t a_i[VECTOR_DIMS], b_i[VECTOR_DIMS];

    /* Par 1: legit vs fraud (vetores da spec) */
    a_f[0]=0.0041f; a_f[1]=0.1667f; a_f[2]=0.05f;   a_f[3]=0.7826f;
    a_f[4]=0.3333f; a_f[5]=-1.0f;   a_f[6]=-1.0f;   a_f[7]=0.0292f;
    a_f[8]=0.15f;   a_f[9]=0.0f;    a_f[10]=1.0f;   a_f[11]=0.0f;
    a_f[12]=0.15f;  a_f[13]=0.006f;

    b_f[0]=0.9506f; b_f[1]=0.8333f; b_f[2]=1.0f;    b_f[3]=0.2174f;
    b_f[4]=0.8333f; b_f[5]=-1.0f;   b_f[6]=-1.0f;   b_f[7]=0.9523f;
    b_f[8]=1.0f;    b_f[9]=0.0f;    b_f[10]=1.0f;   b_f[11]=1.0f;
    b_f[12]=0.75f;  b_f[13]=0.0055f;

    quantize_f32_to_i16(a_f, a_i);
    quantize_f32_to_i16(b_f, b_i);

    int64_t d_ab = distance_sq_i16(a_i, b_i, INT64_MAX);
    int64_t d_ba = distance_sq_i16(b_i, a_i, INT64_MAX);

    ASSERT_TRUE(d_ab == d_ba);

    /* Par 2: vetores com zeros e uns */
    int16_t c[VECTOR_DIMS], d[VECTOR_DIMS];
    memset(c, 0, sizeof(c));
    memset(d, 0, sizeof(d));
    c[0] = 10000;   /* max positivo */
    d[3] = -10000;  /* sentinel/negativo */
    d[5] = -10000;

    int64_t d_cd = distance_sq_i16(c, d, INT64_MAX);
    int64_t d_dc = distance_sq_i16(d, c, INT64_MAX);

    ASSERT_TRUE(d_cd == d_dc);

    return 0;
}

/* -------------------------------------------------------------------------
 * Suite runner — chamada de test_main.c
 * ------------------------------------------------------------------------- */
int run_quantize_tests(void)
{
    printf("\n=== Quantize Tests ===\n");

    RUN_TEST(test_quantize_known_values);
    RUN_TEST(test_quantize_roundtrip_precision);
    RUN_TEST(test_quantize_sentinel);
    RUN_TEST(test_distance_i16_matches_float);
    RUN_TEST(test_distance_i16_early_exit);
    RUN_TEST(test_distance_i16_zero);
    RUN_TEST(test_distance_i16_symmetry);

    return _tests_failed;
}
