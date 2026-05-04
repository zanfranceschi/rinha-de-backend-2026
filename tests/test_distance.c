/*
 * test_distance.c — TDD-spec (RED) para distancia euclidiana
 *
 * R37-R41: cobrindo identidade, simetria, distancia unitaria,
 * participacao de sentinels e relacao distance_sq vs distance_euclidean.
 *
 * Cada funcao retorna 0 (pass) ou 1 (fail).
 * Os stubs em distance.c retornam 0.0f, portanto a maioria deve falhar (RED).
 */

#include "test.h"
#include "distance.h"

#include <math.h>
#include <string.h>

/* Vetor auxiliar: todos zeros em 14 dims */
static void zero_vec(float v[VECTOR_DIMS])
{
    for (int i = 0; i < VECTOR_DIMS; i++) v[i] = 0.0f;
}

/* -------------------------------------------------------------------------
 * R37: distance_sq(a, a) == 0  (identidade)
 *
 * Usa vetor com valores nao-zero para distinguir o caso "stub retorna 0"
 * do caso "calculo real retorna 0". A implementacao correta deve calcular
 * sum((a[i]-a[i])^2) = 0, enquanto um stub que retorna 0 tambem passa.
 * Para confirmar que a identidade e testada genuinamente, este teste
 * TAMBEM verifica que a funcao nao retorna 0 para vetores distintos
 * (isso e coberto por R39, que falhara com o stub).
 * ------------------------------------------------------------------------- */
static int test_distance_sq_identity(void)
{
    float a[VECTOR_DIMS];
    /* Vetor com valores representativos do dominio (exemplo legitimo da spec) */
    a[0]  = 0.0041f;
    a[1]  = 0.1667f;
    a[2]  = 0.05f;
    a[3]  = 0.7826f;
    a[4]  = 0.3333f;
    a[5]  = -1.0f;   /* sentinel */
    a[6]  = -1.0f;   /* sentinel */
    a[7]  = 0.0292f;
    a[8]  = 0.15f;
    a[9]  = 0.0f;
    a[10] = 1.0f;
    a[11] = 0.0f;
    a[12] = 0.15f;
    a[13] = 0.006f;

    ASSERT_FLOAT_EQ(distance_sq(a, a), 0.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R38: distance_sq(a, b) == distance_sq(b, a)  (simetria)
 *
 * Verifica simetria E valor concreto esperado para garantir que o stub
 * (que retorna 0.0f) falhe. O valor esperado foi calculado manualmente
 * a partir dos dois vetores dos exemplos da spec.
 *
 * Calculo manual (dims onde a != b):
 *   dim[0]: (0.0041-0.9506)^2 = (-0.9465)^2 = 0.89585
 *   dim[1]: (0.1667-0.8333)^2 = (-0.6666)^2 = 0.44436
 *   dim[2]: (0.05  -1.0000)^2 = (-0.9500)^2 = 0.90250
 *   dim[3]: (0.7826-0.2174)^2 = ( 0.5652)^2 = 0.31945
 *   dim[4]: (0.3333-0.8333)^2 = (-0.5000)^2 = 0.25000
 *   dim[5]: (-1.0  -(-1.0))^2 = 0.0
 *   dim[6]: (-1.0  -(-1.0))^2 = 0.0
 *   dim[7]: (0.0292-0.9523)^2 = (-0.9231)^2 = 0.85211
 *   dim[8]: (0.15  -1.0000)^2 = (-0.8500)^2 = 0.72250
 *   dim[9]: (0.0   -0.0   )^2 = 0.0
 *   dim[10]:(1.0   -1.0   )^2 = 0.0
 *   dim[11]:(0.0   -1.0   )^2 = 1.00000
 *   dim[12]:(0.15  -0.75  )^2 = (-0.60)^2  = 0.36000
 *   dim[13]:(0.006 -0.0055)^2 = ( 0.0005)^2 = 0.00000025
 *   TOTAL ≈ 5.7468
 *   (Nota: dim[11] contribui 1.0 — soma correta e 5.7468, nao 4.7468)
 * ------------------------------------------------------------------------- */
static int test_distance_sq_symmetry(void)
{
    float a[VECTOR_DIMS], b[VECTOR_DIMS];

    /* Vetor a: exemplo legitimo da spec */
    a[0]=0.0041f; a[1]=0.1667f; a[2]=0.05f;   a[3]=0.7826f;
    a[4]=0.3333f; a[5]=-1.0f;   a[6]=-1.0f;   a[7]=0.0292f;
    a[8]=0.15f;   a[9]=0.0f;    a[10]=1.0f;   a[11]=0.0f;
    a[12]=0.15f;  a[13]=0.006f;

    /* Vetor b: exemplo fraudulento da spec */
    b[0]=0.9506f; b[1]=0.8333f; b[2]=1.0f;    b[3]=0.2174f;
    b[4]=0.8333f; b[5]=-1.0f;   b[6]=-1.0f;   b[7]=0.9523f;
    b[8]=1.0f;    b[9]=0.0f;    b[10]=1.0f;   b[11]=1.0f;
    b[12]=0.75f;  b[13]=0.0055f;

    float d_ab = distance_sq(a, b);
    float d_ba = distance_sq(b, a);

    /* Simetria: d(a,b) == d(b,a) */
    ASSERT_FLOAT_EQ(d_ab, d_ba, 1e-5f);

    /* Valor concreto: garante que stub retornando 0.0f falhe */
    ASSERT_FLOAT_EQ(d_ab, 5.7468f, 0.02f);

    return 0;
}

/* -------------------------------------------------------------------------
 * R39: distance_sq([1,0,...,0], [0,0,...,0]) == 1.0
 * Verifica que a distancia entre vetores unitarios ortogonais e correta.
 * ------------------------------------------------------------------------- */
static int test_distance_sq_unit_vector(void)
{
    float a[VECTOR_DIMS], b[VECTOR_DIMS];
    zero_vec(a);
    zero_vec(b);
    a[0] = 1.0f;  /* apenas a primeira dimensao difere */

    /* distancia^2 = (1-0)^2 + 0 + ... + 0 = 1.0 */
    ASSERT_FLOAT_EQ(distance_sq(a, b), 1.0f, 1e-6f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R40: sentinel -1.0 participa normalmente do calculo (NAO e ignorado)
 *
 * Regra do projeto: sentinels sao valores como qualquer outro no calculo
 * de distancia — a VP-Tree foi treinada com os mesmos sentinels.
 * ------------------------------------------------------------------------- */
static int test_distance_sq_sentinel_participates(void)
{
    float a[VECTOR_DIMS], b[VECTOR_DIMS];
    zero_vec(a);
    zero_vec(b);

    /* a[5] = -1 (sentinel), b[5] = 0.5 (valor normal) */
    a[5] = -1.0f;
    b[5] =  0.5f;

    /* Somente dim[5] difere: dist^2 = (-1 - 0.5)^2 = 2.25 */
    float expected = (-1.0f - 0.5f) * (-1.0f - 0.5f);  /* = 2.25 */
    ASSERT_FLOAT_EQ(distance_sq(a, b), expected, 1e-5f);
    return 0;
}

/* -------------------------------------------------------------------------
 * R41: distance_euclidean(a, b) == sqrt(distance_sq(a, b))
 *
 * Garante consistencia entre as duas funcoes de distancia.
 * Tambem verifica valor concreto para que o stub (retorna 0.0f) falhe.
 * sqrt(5.7468) ≈ 2.3973
 * ------------------------------------------------------------------------- */
static int test_distance_euclidean_equals_sqrt_of_sq(void)
{
    float a[VECTOR_DIMS], b[VECTOR_DIMS];

    /* Vetor a: exemplo legitimo */
    a[0]=0.0041f; a[1]=0.1667f; a[2]=0.05f;   a[3]=0.7826f;
    a[4]=0.3333f; a[5]=-1.0f;   a[6]=-1.0f;   a[7]=0.0292f;
    a[8]=0.15f;   a[9]=0.0f;    a[10]=1.0f;   a[11]=0.0f;
    a[12]=0.15f;  a[13]=0.006f;

    /* Vetor b: exemplo fraudulento */
    b[0]=0.9506f; b[1]=0.8333f; b[2]=1.0f;    b[3]=0.2174f;
    b[4]=0.8333f; b[5]=-1.0f;   b[6]=-1.0f;   b[7]=0.9523f;
    b[8]=1.0f;    b[9]=0.0f;    b[10]=1.0f;   b[11]=1.0f;
    b[12]=0.75f;  b[13]=0.0055f;

    float sq     = distance_sq(a, b);
    float euclid = distance_euclidean(a, b);

    /* Consistencia: euclid deve ser sqrt(sq) */
    ASSERT_FLOAT_EQ(euclid, sqrtf(sq), 1e-5f);

    /* Valor concreto: garante que stub retornando 0.0f falhe */
    ASSERT_FLOAT_EQ(euclid, sqrtf(5.7468f), 0.02f);

    return 0;
}

/* -------------------------------------------------------------------------
 * Suite runner — chamada de test_main.c
 * ------------------------------------------------------------------------- */
int run_distance_tests(void)
{
    printf("\n=== Distance Tests ===\n");

    RUN_TEST(test_distance_sq_identity);
    RUN_TEST(test_distance_sq_symmetry);
    RUN_TEST(test_distance_sq_unit_vector);
    RUN_TEST(test_distance_sq_sentinel_participates);
    RUN_TEST(test_distance_euclidean_equals_sqrt_of_sq);

    return _tests_failed;
}
