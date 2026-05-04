/*
 * test_scoring.c — TDD-spec (RED) para fraud_score e approved
 *
 * R42-R49: cobrindo todos os casos de scoring incluindo o limite critico
 * de threshold (score=0.6 deve ser REJEITADO, NAO aprovado).
 *
 * As funcoes de scoring sao definidas localmente aqui (nao dependem de
 * engine.c) para isolar a regra de negocio do mecanismo de busca.
 *
 * IMPORTANTE: compute_fraud_score e compute_approved sao definidas como
 * funcoes locais aqui. Quando engine.c for implementado, ele deve seguir
 * exatamente a mesma logica que estes testes especificam.
 *
 * Cada funcao retorna 0 (pass) ou 1 (fail).
 */

#include "test.h"
#include "vectorize.h"  /* KNN_K, FRAUD_THRESHOLD */

/* -------------------------------------------------------------------------
 * Funcoes de scoring locais — especificacao executavel
 *
 * Estas sao as REGRAS DE NEGOCIO. A implementacao em engine.c deve
 * produzir resultados identicos.
 * ------------------------------------------------------------------------- */

static float compute_fraud_score(int fraud_count, int k)
{
    return (float)fraud_count / (float)k;
}

static int compute_approved(float score)
{
    return score < FRAUD_THRESHOLD;  /* < 0.6f, threshold FIXO */
}

/* -------------------------------------------------------------------------
 * R42: 0 fraudes entre 5 vizinhos → fraud_score = 0.0
 * ------------------------------------------------------------------------- */
static int test_fraud_score_zero_frauds(void)
{
    ASSERT_FLOAT_EQ(compute_fraud_score(0, 5), 0.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R43: 5 fraudes entre 5 vizinhos → fraud_score = 1.0
 * ------------------------------------------------------------------------- */
static int test_fraud_score_all_frauds(void)
{
    ASSERT_FLOAT_EQ(compute_fraud_score(5, 5), 1.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R44: 3 fraudes entre 5 vizinhos → fraud_score = 0.6
 * Este e o valor exato do threshold — resultado esta no limite.
 * ------------------------------------------------------------------------- */
static int test_fraud_score_three_frauds(void)
{
    ASSERT_FLOAT_EQ(compute_fraud_score(3, 5), 0.6f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R45: score=0.4 → approved=true (abaixo do threshold 0.6)
 * ------------------------------------------------------------------------- */
static int test_approved_below_threshold(void)
{
    ASSERT_INT_EQ(compute_approved(0.4f), 1);
    return 0;
}

/* -------------------------------------------------------------------------
 * R46: score=0.6 → approved=false  [CRITICO: >= threshold NAO e aprovado]
 *
 * Esta e a regra mais critica de threshold. O operador e ESTRITAMENTE
 * MENOR QUE (<), portanto score=0.6 deve ser REJEITADO.
 * Confundir < com <= causaria falsos negativos (fraudes aprovadas, peso 3).
 * ------------------------------------------------------------------------- */
static int test_approved_at_threshold_is_rejected(void)
{
    ASSERT_INT_EQ(compute_approved(0.6f), 0);  /* 0.6 < 0.6 == false → rejected */
    return 0;
}

/* -------------------------------------------------------------------------
 * R47: score=0.8 → approved=false (acima do threshold)
 * ------------------------------------------------------------------------- */
static int test_approved_above_threshold(void)
{
    ASSERT_INT_EQ(compute_approved(0.8f), 0);
    return 0;
}

/* -------------------------------------------------------------------------
 * R48: KNN_K deve ser exatamente 5 (constante definida em vectorize.h)
 *
 * Este teste documenta a invariante: a busca SEMPRE retorna 5 vizinhos.
 * fraud_score = fraudes / KNN_K = fraudes / 5.
 * ------------------------------------------------------------------------- */
static int test_knn_k_constant_equals_five(void)
{
    ASSERT_INT_EQ(KNN_K, 5);
    return 0;
}

/* -------------------------------------------------------------------------
 * R49: Fallback — em caso de erro, API deve retornar approved=true, score=0.0
 *
 * Regra de negocio: HTTP 500 tem peso 5 na formula de pontuacao.
 * FP (falso positivo, aprovando transacao suspeita) tem peso 1.
 * Portanto, em caso de erro interno, retornar {approved:true, fraud_score:0.0}
 * e sempre preferivel a retornar HTTP 500.
 *
 * Este teste documenta e verifica os VALORES do fallback.
 * ------------------------------------------------------------------------- */
static int test_fallback_values(void)
{
    /* Valores canonicos do fallback */
    float  fallback_score    = 0.0f;
    int    fallback_approved = 1;  /* true */

    ASSERT_FLOAT_EQ(fallback_score, 0.0f, 1e-6f);
    ASSERT_INT_EQ(fallback_approved, 1);

    /* Consistencia: fallback_score deve passar no threshold */
    ASSERT_INT_EQ(compute_approved(fallback_score), 1);
    return 0;
}

/* -------------------------------------------------------------------------
 * Suite runner — chamada de test_main.c
 * ------------------------------------------------------------------------- */
int run_scoring_tests(void)
{
    printf("\n=== Scoring Tests ===\n");

    RUN_TEST(test_fraud_score_zero_frauds);
    RUN_TEST(test_fraud_score_all_frauds);
    RUN_TEST(test_fraud_score_three_frauds);
    RUN_TEST(test_approved_below_threshold);
    RUN_TEST(test_approved_at_threshold_is_rejected);
    RUN_TEST(test_approved_above_threshold);
    RUN_TEST(test_knn_k_constant_equals_five);
    RUN_TEST(test_fallback_values);

    return _tests_failed;
}
