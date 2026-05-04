/*
 * test_vptree.c — TDD-spec (RED) para VP-Tree KNN
 *
 * R50: vptree_search retorna exatamente k=5 vizinhos
 * R51: resultado identico ao brute force (100% recall) — mini dataset
 * R52: funciona com vetores contendo sentinel -1.0f nas dims 5 e 6
 * R53: 100% recall contra brute force com todos os 80 vetores do dataset exemplo
 *
 * Todos os testes devem FALHAR com o stub em vptree.c.
 * Quando a implementacao real for feita, todos devem PASSAR sem alterar
 * nenhuma linha deste arquivo.
 *
 * brute_force_knn() e implementada localmente — e a especificacao de
 * "correto" que a VP-Tree deve replicar.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "test.h"
#include "vectorize.h"
#include "distance.h"
#include "vptree.h"

/* =========================================================================
 * Dataset mini: primeiros 10 vetores do example-references.json
 * Extraidos manualmente — valores float exatos do arquivo JSON.
 * ========================================================================= */

#define MINI_N 10

static float mini_vectors[MINI_N][VECTOR_DIMS] = {
    /* idx 0 — legit */
    {0.01f,   0.0833f, 0.05f, 0.8261f, 0.1667f, -1.0f,   -1.0f,   0.0432f, 0.25f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0416f},
    /* idx 1 — legit */
    {0.0109f, 0.1667f, 0.05f, 0.3913f, 0.6667f,  0.3007f, 0.0139f, 0.0154f, 0.2f,  0.0f, 1.0f, 0.0f, 0.15f, 0.0282f},
    /* idx 2 — legit */
    {0.0336f, 0.1667f, 0.05f, 0.4348f, 0.6667f,  0.1278f, 0.0008f, 0.017f,  0.1f,  0.0f, 1.0f, 0.0f, 0.2f,  0.02f  },
    /* idx 3 — legit */
    {0.0415f, 0.25f,   0.05f, 0.7391f, 1.0f,     0.2375f, 0.0121f, 0.0005f, 0.2f,  0.0f, 1.0f, 0.0f, 0.3f,  0.0493f},
    /* idx 4 — legit */
    {0.0291f, 0.0833f, 0.05f, 0.3913f, 0.3333f,  0.3028f, 0.0044f, 0.028f,  0.1f,  0.0f, 1.0f, 0.0f, 0.3f,  0.043f },
    /* idx 5 — fraud */
    {0.5796f, 0.9167f, 1.0f,  0.0435f, 0.0f,     0.0056f, 0.4394f, 0.4598f, 0.4f,  1.0f, 0.0f, 1.0f, 0.85f, 0.0032f},
    /* idx 6 — legit */
    {0.0035f, 0.1667f, 0.05f, 0.4783f, 0.8333f,  0.2264f, 0.001f,  0.0488f, 0.05f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0231f},
    /* idx 7 — fraud */
    {0.9708f, 1.0f,    1.0f,  0.1304f, 0.3333f, -1.0f,   -1.0f,   0.6657f, 1.0f,  1.0f, 0.0f, 1.0f, 0.75f, 0.0077f},
    /* idx 8 — legit */
    {0.0092f, 0.0833f, 0.05f, 0.6522f, 1.0f,     0.0417f, 0.0116f, 0.0025f, 0.1f,  0.0f, 1.0f, 0.0f, 0.15f, 0.0101f},
    /* idx 9 — fraud */
    {0.3536f, 0.5f,    1.0f,  0.087f,  0.6667f,  0.0049f, 0.8445f, 0.8925f, 0.8f,  1.0f, 0.0f, 1.0f, 0.85f, 0.0035f},
};

static uint8_t mini_labels[MINI_N] = {0, 0, 0, 0, 0, 1, 0, 1, 0, 1};

/* =========================================================================
 * Dataset completo: todos os 80 vetores do example-references.json
 * ========================================================================= */

#define FULL_N 80

static float full_vectors[FULL_N][VECTOR_DIMS] = {
    /* 0  legit */ {0.01f,   0.0833f, 0.05f,   0.8261f, 0.1667f, -1.0f,   -1.0f,   0.0432f, 0.25f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0416f},
    /* 1  legit */ {0.0109f, 0.1667f, 0.05f,   0.3913f, 0.6667f,  0.3007f, 0.0139f, 0.0154f, 0.2f,  0.0f, 1.0f, 0.0f, 0.15f, 0.0282f},
    /* 2  legit */ {0.0336f, 0.1667f, 0.05f,   0.4348f, 0.6667f,  0.1278f, 0.0008f, 0.017f,  0.1f,  0.0f, 1.0f, 0.0f, 0.2f,  0.02f  },
    /* 3  legit */ {0.0415f, 0.25f,   0.05f,   0.7391f, 1.0f,     0.2375f, 0.0121f, 0.0005f, 0.2f,  0.0f, 1.0f, 0.0f, 0.3f,  0.0493f},
    /* 4  legit */ {0.0291f, 0.0833f, 0.05f,   0.3913f, 0.3333f,  0.3028f, 0.0044f, 0.028f,  0.1f,  0.0f, 1.0f, 0.0f, 0.3f,  0.043f },
    /* 5  fraud */ {0.5796f, 0.9167f, 1.0f,    0.0435f, 0.0f,     0.0056f, 0.4394f, 0.4598f, 0.4f,  1.0f, 0.0f, 1.0f, 0.85f, 0.0032f},
    /* 6  legit */ {0.0035f, 0.1667f, 0.05f,   0.4783f, 0.8333f,  0.2264f, 0.001f,  0.0488f, 0.05f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0231f},
    /* 7  fraud */ {0.9708f, 1.0f,    1.0f,    0.1304f, 0.3333f, -1.0f,   -1.0f,   0.6657f, 1.0f,  1.0f, 0.0f, 1.0f, 0.75f, 0.0077f},
    /* 8  legit */ {0.0092f, 0.0833f, 0.05f,   0.6522f, 1.0f,     0.0417f, 0.0116f, 0.0025f, 0.1f,  0.0f, 1.0f, 0.0f, 0.15f, 0.0101f},
    /* 9  fraud */ {0.3536f, 0.5f,    1.0f,    0.087f,  0.6667f,  0.0049f, 0.8445f, 0.8925f, 0.8f,  1.0f, 0.0f, 1.0f, 0.85f, 0.0035f},
    /* 10 legit */ {0.0346f, 0.1667f, 0.05f,   0.4348f, 0.6667f,  0.2243f, 0.0017f, 0.0361f, 0.15f, 0.0f, 1.0f, 0.0f, 0.25f, 0.0481f},
    /* 11 legit */ {0.0033f, 0.25f,   0.05f,   0.3913f, 0.1667f,  0.391f,  0.0075f, 0.0429f, 0.05f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0083f},
    /* 12 fraud */ {0.2142f, 0.5f,    1.0f,    0.0435f, 0.3333f,  0.0021f, 0.3179f, 0.2136f, 0.5f,  1.0f, 0.0f, 1.0f, 0.75f, 0.0046f},
    /* 13 legit */ {0.0293f, 0.0833f, 0.05f,   0.6957f, 0.5f,     0.4375f, 0.0104f, 0.0037f, 0.05f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0427f},
    /* 14 fraud */ {0.2398f, 0.8333f, 1.0f,    0.087f,  0.6667f,  0.0028f, 0.4616f, 0.2795f, 0.4f,  1.0f, 0.0f, 1.0f, 0.85f, 0.0067f},
    /* 15 legit */ {0.0268f, 0.0833f, 0.05f,   0.5217f, 1.0f,     0.2583f, 0.0146f, 0.0494f, 0.2f,  0.0f, 1.0f, 0.0f, 0.25f, 0.0339f},
    /* 16 legit */ {0.0064f, 0.1667f, 0.05f,   0.7826f, 0.6667f,  0.1458f, 0.0013f, 0.0225f, 0.05f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0377f},
    /* 17 legit */ {0.0223f, 0.0833f, 0.05f,   0.5652f, 0.8333f,  0.1618f, 0.0187f, 0.013f,  0.15f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0299f},
    /* 18 legit */ {0.045f,  0.25f,   0.05f,   0.8261f, 0.3333f,  0.1646f, 0.0077f, 0.0494f, 0.2f,  0.0f, 1.0f, 0.0f, 0.15f, 0.0202f},
    /* 19 legit */ {0.0146f, 0.25f,   0.05f,   0.7826f, 1.0f,     0.4292f, 0.0004f, 0.0093f, 0.1f,  0.0f, 1.0f, 0.0f, 0.25f, 0.0209f},
    /* 20 fraud */ {0.648f,  0.6667f, 1.0f,    0.1739f, 0.8333f,  0.0014f, 0.6968f, 0.5176f, 0.9f,  1.0f, 0.0f, 1.0f, 0.75f, 0.0088f},
    /* 21 legit */ {0.0838f, 0.25f,   0.2467f, 0.913f,  0.5f,     0.0201f, 0.2836f, 0.2979f, 0.4f,  1.0f, 0.0f, 0.0f, 0.8f,  0.0078f},
    /* 22 legit */ {0.0445f, 0.25f,   0.05f,   0.3913f, 0.0f,     0.0354f, 0.0055f, 0.0283f, 0.15f, 0.0f, 1.0f, 0.0f, 0.3f,  0.0169f},
    /* 23 legit */ {0.05f,   0.0833f, 0.05f,   0.4783f, 1.0f,    -1.0f,   -1.0f,   0.0247f, 0.05f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0091f},
    /* 24 fraud */ {0.9312f, 0.5f,    1.0f,    0.087f,  0.6667f, -1.0f,   -1.0f,   0.9155f, 0.65f, 0.0f, 1.0f, 1.0f, 0.75f, 0.0056f},
    /* 25 legit */ {0.0142f, 0.25f,   0.05f,   0.5217f, 0.6667f,  0.2486f, 0.0079f, 0.0069f, 0.05f, 1.0f, 0.0f, 0.0f, 0.2f,  0.0403f},
    /* 26 fraud */ {0.6536f, 0.5f,    1.0f,    0.087f,  0.0f,     0.0049f, 0.9984f, 0.4868f, 0.65f, 1.0f, 0.0f, 1.0f, 0.8f,  0.0082f},
    /* 27 legit */ {0.0461f, 0.0833f, 0.05f,   0.3478f, 0.5f,    -1.0f,   -1.0f,   0.0009f, 0.05f, 0.0f, 1.0f, 0.0f, 0.3f,  0.0144f},
    /* 28 legit */ {0.0488f, 0.0833f, 0.05f,   0.8261f, 0.3333f,  0.2125f, 0.0123f, 0.0156f, 0.05f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0403f},
    /* 29 legit */ {0.0046f, 0.1667f, 0.05f,   0.8696f, 1.0f,     0.2847f, 0.0073f, 0.011f,  0.2f,  0.0f, 1.0f, 0.0f, 0.3f,  0.0214f},
    /* 30 fraud */ {0.7938f, 0.75f,   1.0f,    0.1739f, 0.0f,     0.0063f, 0.3999f, 0.6f,    0.5f,  1.0f, 0.0f, 1.0f, 0.8f,  0.0094f},
    /* 31 legit */ {0.002f,  0.1667f, 0.05f,   0.6522f, 0.8333f,  0.2326f, 0.018f,  0.0161f, 0.1f,  1.0f, 0.0f, 0.0f, 0.2f,  0.0198f},
    /* 32 legit */ {0.006f,  0.25f,   0.05f,   0.8696f, 1.0f,     0.2028f, 0.0021f, 0.0211f, 0.05f, 1.0f, 0.0f, 0.0f, 0.25f, 0.0314f},
    /* 33 legit */ {0.0241f, 0.1667f, 0.05f,   0.6957f, 1.0f,     0.1347f, 0.0078f, 0.016f,  0.2f,  0.0f, 1.0f, 0.0f, 0.3f,  0.0221f},
    /* 34 legit */ {0.022f,  0.1667f, 0.05f,   0.8696f, 0.6667f,  0.1111f, 0.0157f, 0.0496f, 0.2f,  0.0f, 1.0f, 0.0f, 0.25f, 0.0066f},
    /* 35 legit */ {0.0082f, 0.0833f, 0.05f,   0.8696f, 0.8333f, -1.0f,   -1.0f,   0.0083f, 0.15f, 0.0f, 1.0f, 0.0f, 0.3f,  0.0048f},
    /* 36 fraud */ {0.4832f, 0.8333f, 1.0f,    0.0435f, 0.8333f,  0.0007f, 0.6549f, 0.3262f, 0.55f, 1.0f, 0.0f, 1.0f, 0.75f, 0.0033f},
    /* 37 legit */ {0.0161f, 0.0833f, 0.05f,   0.4783f, 0.5f,     0.0646f, 0.0033f, 0.0297f, 0.05f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0032f},
    /* 38 legit */ {0.0206f, 0.25f,   0.05f,   0.6957f, 0.1667f,  0.2681f, 0.0184f, 0.04f,   0.2f,  0.0f, 1.0f, 0.0f, 0.15f, 0.0238f},
    /* 39 legit */ {0.0342f, 0.1667f, 0.05f,   0.7391f, 0.5f,     0.1472f, 0.0181f, 0.0353f, 0.25f, 1.0f, 0.0f, 0.0f, 0.15f, 0.0135f},
    /* 40 legit */ {0.028f,  0.25f,   0.05f,   0.5217f, 0.1667f,  0.1778f, 0.0037f, 0.0462f, 0.15f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0435f},
    /* 41 fraud */ {0.7052f, 0.5f,    1.0f,    0.2174f, 0.1667f,  0.0028f, 0.7768f, 0.7146f, 0.55f, 1.0f, 0.0f, 1.0f, 0.75f, 0.0082f},
    /* 42 legit */ {0.0216f, 0.1667f, 0.05f,   0.7826f, 0.6667f, -1.0f,   -1.0f,   0.0458f, 0.15f, 0.0f, 1.0f, 0.0f, 0.3f,  0.0424f},
    /* 43 legit */ {0.0337f, 0.0833f, 0.05f,   0.5217f, 0.6667f,  0.359f,  0.0182f, 0.0389f, 0.1f,  0.0f, 1.0f, 0.0f, 0.25f, 0.0316f},
    /* 44 legit */ {0.01f,   0.0833f, 0.05f,   0.5652f, 0.1667f,  0.2382f, 0.0026f, 0.0435f, 0.25f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0149f},
    /* 45 legit */ {0.033f,  0.25f,   0.05f,   0.6522f, 0.6667f,  0.166f,  0.0155f, 0.0312f, 0.25f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0414f},
    /* 46 legit */ {0.0496f, 0.0833f, 0.05f,   0.3913f, 0.0f,     0.3188f, 0.0184f, 0.05f,   0.05f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0498f},
    /* 47 legit */ {0.0439f, 0.1667f, 0.05f,   0.4783f, 0.1667f,  0.091f,  0.0192f, 0.0121f, 0.2f,  1.0f, 0.0f, 0.0f, 0.25f, 0.0216f},
    /* 48 legit */ {0.041f,  0.0833f, 0.05f,   0.6522f, 0.1667f,  0.2556f, 0.0011f, 0.0259f, 0.25f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0046f},
    /* 49 legit */ {0.0479f, 0.1667f, 0.05f,   0.7391f, 0.5f,     0.4104f, 0.0002f, 0.0429f, 0.1f,  1.0f, 0.0f, 0.0f, 0.25f, 0.0372f},
    /* 50 legit */ {0.0325f, 0.0833f, 0.05f,   0.4348f, 0.1667f,  0.3125f, 0.001f,  0.0318f, 0.05f, 0.0f, 0.0f, 0.0f, 0.3f,  0.0324f},
    /* 51 fraud */ {0.9686f, 0.5833f, 1.0f,    0.2174f, 0.1667f,  0.0028f, 0.5814f, 0.7333f, 0.85f, 0.0f, 1.0f, 1.0f, 0.8f,  0.0055f},
    /* 52 fraud */ {0.1145f, 0.4167f, 0.2463f, 0.2609f, 1.0f,     0.0618f, 0.0521f, 0.2831f, 0.45f, 1.0f, 0.0f, 1.0f, 0.3f,  0.0177f},
    /* 53 legit */ {0.022f,  0.25f,   0.05f,   0.6087f, 0.5f,     0.4021f, 0.0074f, 0.0323f, 0.15f, 1.0f, 0.0f, 0.0f, 0.2f,  0.012f },
    /* 54 legit */ {0.01f,   0.25f,   0.05f,   0.6087f, 0.5f,    -1.0f,   -1.0f,   0.0139f, 0.1f,  0.0f, 1.0f, 0.0f, 0.15f, 0.0121f},
    /* 55 fraud */ {0.2511f, 1.0f,    1.0f,    0.1304f, 0.0f,    -1.0f,   -1.0f,   0.3296f, 0.85f, 1.0f, 0.0f, 1.0f, 0.85f, 0.0088f},
    /* 56 legit */ {0.0168f, 0.25f,   0.05f,   0.3913f, 0.0f,     0.3667f, 0.0076f, 0.0339f, 0.25f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0064f},
    /* 57 fraud */ {0.5203f, 0.75f,   1.0f,    0.0f,    0.8333f,  0.0042f, 0.752f,  0.8856f, 0.8f,  1.0f, 0.0f, 1.0f, 0.85f, 0.0063f},
    /* 58 legit */ {0.0273f, 0.1667f, 0.05f,   0.6087f, 0.6667f,  0.2438f, 0.0079f, 0.0052f, 0.25f, 1.0f, 0.0f, 0.0f, 0.15f, 0.0331f},
    /* 59 legit */ {0.0323f, 0.25f,   0.05f,   0.7826f, 0.1667f,  0.1722f, 0.0167f, 0.0065f, 0.2f,  1.0f, 0.0f, 0.0f, 0.3f,  0.0237f},
    /* 60 legit */ {0.0383f, 0.1667f, 0.05f,   0.7391f, 0.6667f, -1.0f,   -1.0f,   0.0095f, 0.25f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0262f},
    /* 61 legit */ {0.0076f, 0.0833f, 0.05f,   0.3478f, 0.6667f,  0.2208f, 0.011f,  0.0215f, 0.05f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0207f},
    /* 62 legit */ {0.0344f, 0.25f,   0.05f,   0.7826f, 0.6667f,  0.2694f, 0.0005f, 0.0199f, 0.25f, 0.0f, 1.0f, 0.0f, 0.25f, 0.0396f},
    /* 63 fraud */ {0.5487f, 0.9167f, 1.0f,    0.2609f, 0.3333f,  0.0042f, 0.4317f, 0.5357f, 0.8f,  1.0f, 0.0f, 1.0f, 0.8f,  0.0054f},
    /* 64 legit */ {0.02f,   0.1667f, 0.05f,   0.3478f, 0.5f,     0.4368f, 0.0063f, 0.0406f, 0.05f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0034f},
    /* 65 legit */ {0.0237f, 0.25f,   0.05f,   0.6957f, 0.5f,     0.1604f, 0.0141f, 0.0159f, 0.2f,  0.0f, 1.0f, 0.0f, 0.2f,  0.0035f},
    /* 66 legit */ {0.0326f, 0.1667f, 0.05f,   0.6957f, 0.1667f, -1.0f,   -1.0f,   0.0098f, 0.05f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0143f},
    /* 67 fraud */ {0.5198f, 1.0f,    1.0f,    0.087f,  0.6667f,  0.0028f, 0.713f,  0.275f,  0.95f, 0.0f, 1.0f, 1.0f, 0.85f, 0.0086f},
    /* 68 fraud */ {0.1801f, 0.25f,   0.5297f, 0.3478f, 0.3333f,  0.0083f, 0.0451f, 0.1368f, 0.4f,  0.0f, 1.0f, 0.0f, 0.25f, 0.015f },
    /* 69 legit */ {0.0488f, 0.25f,   0.05f,   0.8261f, 0.3333f,  0.1576f, 0.0059f, 0.0276f, 0.05f, 0.0f, 1.0f, 0.0f, 0.3f,  0.036f },
    /* 70 fraud */ {0.2308f, 0.5833f, 1.0f,    0.2174f, 0.3333f,  0.0028f, 0.9673f, 0.2263f, 0.7f,  1.0f, 0.0f, 1.0f, 0.85f, 0.006f },
    /* 71 fraud */ {0.3717f, 1.0f,    1.0f,    0.0435f, 0.1667f,  0.0021f, 0.2157f, 0.6494f, 0.5f,  1.0f, 0.0f, 1.0f, 0.8f,  0.0085f},
    /* 72 legit */ {0.0148f, 0.25f,   0.05f,   0.7826f, 0.0f,     0.4236f, 0.0024f, 0.0358f, 0.1f,  0.0f, 1.0f, 0.0f, 0.2f,  0.009f },
    /* 73 legit */ {0.0103f, 0.0833f, 0.05f,   0.6522f, 0.8333f,  0.3521f, 0.0099f, 0.0369f, 0.05f, 0.0f, 1.0f, 0.0f, 0.3f,  0.0338f},
    /* 74 legit */ {0.0467f, 0.0833f, 0.05f,   0.8261f, 0.8333f,  0.4326f, 0.0178f, 0.0012f, 0.15f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0202f},
    /* 75 legit */ {0.0011f, 0.25f,   0.05f,   0.5217f, 0.3333f, -1.0f,   -1.0f,   0.0286f, 0.15f, 1.0f, 0.0f, 0.0f, 0.2f,  0.0294f},
    /* 76 legit */ {0.0479f, 0.25f,   0.05f,   0.6087f, 0.0f,     0.2153f, 0.0152f, 0.045f,  0.25f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0064f},
    /* 77 legit */ {0.0043f, 0.0833f, 0.05f,   0.4348f, 0.1667f,  0.3778f, 0.0073f, 0.0183f, 0.2f,  1.0f, 0.0f, 0.0f, 0.2f,  0.0123f},
    /* 78 legit */ {0.04f,   0.25f,   0.05f,   0.4348f, 0.5f,    -1.0f,   -1.0f,   0.0377f, 0.25f, 1.0f, 0.0f, 0.0f, 0.15f, 0.0271f},
    /* 79 fraud */ {0.8194f, 0.75f,   1.0f,    0.2609f, 0.3333f, -1.0f,   -1.0f,   0.9679f, 0.4f,  1.0f, 0.0f, 1.0f, 0.75f, 0.0067f},
};

static uint8_t full_labels[FULL_N] = {
    0,0,0,0,0, 1,0,1,0,1,  /* 0-9  */
    0,0,1,0,1, 0,0,0,0,0,  /* 10-19*/
    1,0,0,0,1, 0,1,0,0,0,  /* 20-29*/
    1,0,0,0,0, 0,1,0,0,0,  /* 30-39*/
    0,1,0,0,0, 0,0,0,0,0,  /* 40-49*/
    0,1,1,0,0, 1,0,1,0,0,  /* 50-59*/
    0,0,0,1,0, 0,0,1,1,0,  /* 60-69*/
    1,1,0,0,0, 0,0,0,0,1   /* 70-79*/
};

/* =========================================================================
 * brute_force_knn — implementacao de referencia (scan linear)
 *
 * Calcula distancia euclidiana de query para todos os vetores no dataset
 * e retorna os k menores. Usa selection partial sort O(N*k).
 *
 * Esta e a definicao de "correto" que a VP-Tree deve replicar.
 * ========================================================================= */

typedef struct {
    int   index;
    float dist;
} bf_neighbor_t;

static void brute_force_knn(const float *vectors, int num_vectors,
                             const float query[VECTOR_DIMS],
                             int k, bf_neighbor_t *out)
{
    /* Inicializa heap com distancias infinitas */
    for (int i = 0; i < k; i++) {
        out[i].index = -1;
        out[i].dist  = 1e38f;
    }

    for (int i = 0; i < num_vectors; i++) {
        const float *v = vectors + (size_t)i * VECTOR_DIMS;
        float d = distance_euclidean(query, v);

        /* Verifica se e menor que o pior vizinho atual */
        int worst = 0;
        for (int j = 1; j < k; j++) {
            if (out[j].dist > out[worst].dist) worst = j;
        }

        if (d < out[worst].dist) {
            out[worst].index = i;
            out[worst].dist  = d;
        }
    }
}

/* Verifica se um indice esta presente no resultado do brute force */
static int bf_contains(const bf_neighbor_t *bf, int k, int idx)
{
    for (int i = 0; i < k; i++) {
        if (bf[i].index == idx) return 1;
    }
    return 0;
}

/* Verifica se um indice esta presente no resultado da VP-Tree */
static int vp_contains(const neighbor_t *vp, int k, int idx)
{
    for (int i = 0; i < k; i++) {
        if (vp[i].index == idx) return 1;
    }
    return 0;
}

/* =========================================================================
 * R50: vptree_search retorna exatamente k=5 vizinhos
 *
 * Regra de negocio: fraud_score = fraudes_nos_5_vizinhos / 5.
 * A busca SEMPRE retorna exatamente KNN_K=5 vizinhos — nem mais, nem menos.
 * ========================================================================= */
static int test_vptree_returns_exactly_k_neighbors(void)
{
    vptree_t tree;
    neighbor_t results[KNN_K];

    /* Arrange: construir VP-Tree com 10 vetores */
    int rc = vptree_build(&tree,
                          (float *)mini_vectors,
                          mini_labels,
                          MINI_N);
    ASSERT_INT_EQ(rc, 0);

    /* Query: vetor proximo ao centro do dataset */
    float query[VECTOR_DIMS] = {
        0.05f, 0.25f, 0.05f, 0.5f, 0.5f,
        0.2f,  0.01f, 0.05f, 0.15f, 0.0f,
        1.0f,  0.0f,  0.2f,  0.03f
    };

    /* Act */
    int found = vptree_search(&tree, query, KNN_K, results);

    /* Assert */
    ASSERT_INT_EQ(found, KNN_K);  /* deve retornar exatamente 5 */

    vptree_free(&tree);
    return 0;
}

/* =========================================================================
 * R51: VP-Tree retorna os mesmos 5 vizinhos que brute force (100% recall)
 *
 * Regra critica: a VP-Tree deve implementar busca EXATA, nao aproximada.
 * O teste oficial da Rinha usa brute force euclidiano — qualquer divergencia
 * nos vizinhos retornados causaria falsos positivos/negativos.
 *
 * Testado com 3 queries distintas no mini dataset (10 vetores).
 * ========================================================================= */
static int test_vptree_matches_brute_force_mini_dataset(void)
{
    vptree_t tree;
    neighbor_t vp_results[KNN_K];
    bf_neighbor_t bf_results[KNN_K];

    int rc = vptree_build(&tree,
                          (float *)mini_vectors,
                          mini_labels,
                          MINI_N);
    ASSERT_INT_EQ(rc, 0);

    /* Query 1: vetor proximo a vizinhanca legit (cluster baixo-amount) */
    {
        float q[VECTOR_DIMS] = {
            0.02f, 0.15f, 0.05f, 0.6f, 0.5f,
            0.2f,  0.01f, 0.02f, 0.15f, 0.0f,
            1.0f,  0.0f,  0.2f,  0.03f
        };

        brute_force_knn((float *)mini_vectors, MINI_N, q, KNN_K, bf_results);
        int found = vptree_search(&tree, q, KNN_K, vp_results);
        ASSERT_INT_EQ(found, KNN_K);

        /* Todos os 5 indices do brute force devem estar no resultado da VP-Tree */
        for (int i = 0; i < KNN_K; i++) {
            int bf_idx = bf_results[i].index;
            ASSERT_TRUE(vp_contains(vp_results, KNN_K, bf_idx));
        }

        /* Todos os 5 indices da VP-Tree devem estar no resultado do brute force */
        for (int i = 0; i < KNN_K; i++) {
            int vp_idx = vp_results[i].index;
            ASSERT_TRUE(bf_contains(bf_results, KNN_K, vp_idx));
        }
    }

    /* Query 2: vetor proximo a vizinhanca fraud (cluster alto-amount) */
    {
        float q[VECTOR_DIMS] = {
            0.7f,  0.8f,  1.0f,  0.15f, 0.4f,
            0.01f, 0.6f,  0.6f,  0.7f,  1.0f,
            0.0f,  1.0f,  0.8f,  0.005f
        };

        brute_force_knn((float *)mini_vectors, MINI_N, q, KNN_K, bf_results);
        int found = vptree_search(&tree, q, KNN_K, vp_results);
        ASSERT_INT_EQ(found, KNN_K);

        for (int i = 0; i < KNN_K; i++) {
            ASSERT_TRUE(vp_contains(vp_results, KNN_K, bf_results[i].index));
        }
        for (int i = 0; i < KNN_K; i++) {
            ASSERT_TRUE(bf_contains(bf_results, KNN_K, vp_results[i].index));
        }
    }

    /* Query 3: vetor exatamente igual ao vetor idx=3 (deve ser o vizinho mais proximo) */
    {
        float q[VECTOR_DIMS];
        for (int d = 0; d < VECTOR_DIMS; d++) q[d] = mini_vectors[3][d];

        brute_force_knn((float *)mini_vectors, MINI_N, q, KNN_K, bf_results);
        int found = vptree_search(&tree, q, KNN_K, vp_results);
        ASSERT_INT_EQ(found, KNN_K);

        /* idx=3 deve estar em ambos os resultados */
        ASSERT_TRUE(bf_contains(bf_results, KNN_K, 3));
        ASSERT_TRUE(vp_contains(vp_results, KNN_K, 3));

        /* Recall total */
        for (int i = 0; i < KNN_K; i++) {
            ASSERT_TRUE(vp_contains(vp_results, KNN_K, bf_results[i].index));
        }
    }

    vptree_free(&tree);
    return 0;
}

/* =========================================================================
 * R52: VP-Tree funciona corretamente com vetores contendo sentinel -1.0f
 *
 * Regra de negocio: dimensoes 5 (minutes_since_last_tx) e 6 (km_from_last_tx)
 * recebem sentinel -1.0f quando last_transaction e null. Este valor DEVE ser
 * preservado — nao substituido por 0, nao clamped.
 *
 * A distancia euclidiana com sentinels e matematicamente valida (o sentinel
 * afasta esses vetores dos vetores sem last_tx normais, o que e o comportamento
 * correto para o dominio).
 *
 * Usa 15 vetores para garantir que o stub (que retorna indices 0..4 sequenciais)
 * diverge do brute force — os vizinhos corretos do query de fraude com sentinel
 * estao nos indices altos do array, nao nos indices 0-4.
 * ========================================================================= */
static int test_vptree_handles_sentinel_minus_one(void)
{
    /*
     * 15 vetores: primeiros 10 sao todos "legit sem sentinel" com coords
     * que os afastam do query. Os ultimos 5 sao os vizinhos reais
     * (com sentinel -1) — o stub vai errar ao retornar indices 0-4.
     */
    #define SENTINEL_N 15
    static float sv[SENTINEL_N][VECTOR_DIMS] = {
        /* idx 0-9: legit sem sentinel, region high-hour (longe do query) */
        {0.045f, 0.25f,   0.05f, 0.9f, 0.5f, 0.3f,  0.02f, 0.05f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        {0.044f, 0.25f,   0.05f, 0.9f, 0.5f, 0.28f, 0.02f, 0.04f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        {0.043f, 0.0833f, 0.05f, 0.9f, 0.5f, 0.25f, 0.01f, 0.04f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        {0.042f, 0.0833f, 0.05f, 0.9f, 0.4f, 0.22f, 0.01f, 0.04f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        {0.041f, 0.1667f, 0.05f, 0.9f, 0.4f, 0.21f, 0.01f, 0.03f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        {0.04f,  0.1667f, 0.05f, 0.9f, 0.4f, 0.20f, 0.01f, 0.03f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        {0.039f, 0.1667f, 0.05f, 0.9f, 0.4f, 0.19f, 0.01f, 0.03f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        {0.038f, 0.1667f, 0.05f, 0.9f, 0.4f, 0.18f, 0.01f, 0.03f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        {0.037f, 0.1667f, 0.05f, 0.9f, 0.3f, 0.17f, 0.01f, 0.03f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        {0.036f, 0.1667f, 0.05f, 0.9f, 0.3f, 0.16f, 0.01f, 0.03f, 0.2f, 0.0f, 1.0f, 0.0f, 0.2f, 0.02f},
        /* idx 10-14: vetores com sentinel -1, proximos ao query */
        {0.01f,   0.0833f, 0.05f, 0.8261f, 0.1667f, -1.0f, -1.0f, 0.0432f, 0.25f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0416f},
        {0.05f,   0.0833f, 0.05f, 0.4783f, 1.0f,    -1.0f, -1.0f, 0.0247f, 0.05f, 0.0f, 1.0f, 0.0f, 0.2f,  0.0091f},
        {0.0082f, 0.0833f, 0.05f, 0.8696f, 0.8333f, -1.0f, -1.0f, 0.0083f, 0.15f, 0.0f, 1.0f, 0.0f, 0.3f,  0.0048f},
        {0.0216f, 0.1667f, 0.05f, 0.7826f, 0.6667f, -1.0f, -1.0f, 0.0458f, 0.15f, 0.0f, 1.0f, 0.0f, 0.3f,  0.0424f},
        {0.0383f, 0.1667f, 0.05f, 0.7391f, 0.6667f, -1.0f, -1.0f, 0.0095f, 0.25f, 0.0f, 1.0f, 0.0f, 0.15f, 0.0262f},
    };
    static uint8_t sl[SENTINEL_N] = {0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0};

    vptree_t tree;
    neighbor_t vp_results[KNN_K];
    bf_neighbor_t bf_results[KNN_K];

    int rc = vptree_build(&tree, (float *)sv, sl, SENTINEL_N);
    ASSERT_INT_EQ(rc, 0);

    /*
     * Query com sentinel — os 5 vizinhos corretos sao os indices 10-14
     * (vetores com -1 nas dims 5 e 6, proximos ao query).
     * O stub retorna [0,1,2,3,4] — incorreto — e deve FALHAR aqui.
     */
    float q[VECTOR_DIMS] = {
        0.02f,  0.1f,    0.05f, 0.75f, 0.5f,
        -1.0f, -1.0f,                        /* sentinels */
        0.03f,  0.15f,   0.0f,
        1.0f,   0.0f,    0.25f, 0.03f
    };

    brute_force_knn((float *)sv, SENTINEL_N, q, KNN_K, bf_results);
    int found = vptree_search(&tree, q, KNN_K, vp_results);

    ASSERT_INT_EQ(found, KNN_K);

    /* Recall: todos os vizinhos do brute force devem estar na VP-Tree */
    for (int i = 0; i < KNN_K; i++) {
        ASSERT_TRUE(vp_contains(vp_results, KNN_K, bf_results[i].index));
    }

    /* VP-Tree nao deve retornar indices invalidos */
    for (int i = 0; i < found; i++) {
        ASSERT_TRUE(vp_results[i].index >= 0);
        ASSERT_TRUE(vp_results[i].index < SENTINEL_N);
    }

    vptree_free(&tree);
    return 0;
}

/* =========================================================================
 * R53: VP-Tree com dataset completo (80 vetores) — 100% recall em 3 queries
 *
 * Este e o teste de integracao mais proximo do cenario real.
 * 80 vetores representam a diversidade do dataset de 3M (legit + fraud,
 * com e sem sentinels, diferentes ranges de amount e mcc_risk).
 *
 * 3 queries cobrindo diferentes regioes do espaco de features:
 *   Q1 — regiao legit (baixo amount, sem fraud patterns)
 *   Q2 — regiao fraud (alto amount, online, unknown merchant)
 *   Q3 — regiao mista (amount medio, com sentinel -1)
 * ========================================================================= */
static int test_vptree_full_dataset_100_recall(void)
{
    vptree_t tree;
    neighbor_t vp_results[KNN_K];
    bf_neighbor_t bf_results[KNN_K];

    int rc = vptree_build(&tree,
                          (float *)full_vectors,
                          full_labels,
                          FULL_N);
    ASSERT_INT_EQ(rc, 0);

    /* Q1 — regiao legit: baixo amount, card present, merchant conhecido */
    {
        float q[VECTOR_DIMS] = {
            0.025f, 0.1667f, 0.05f, 0.65f, 0.5f,
            0.25f,  0.01f,   0.03f, 0.15f, 0.0f,
            1.0f,   0.0f,    0.2f,  0.025f
        };

        brute_force_knn((float *)full_vectors, FULL_N, q, KNN_K, bf_results);
        int found = vptree_search(&tree, q, KNN_K, vp_results);
        ASSERT_INT_EQ(found, KNN_K);

        for (int i = 0; i < KNN_K; i++) {
            ASSERT_TRUE(vp_contains(vp_results, KNN_K, bf_results[i].index));
        }
        for (int i = 0; i < KNN_K; i++) {
            ASSERT_TRUE(bf_contains(bf_results, KNN_K, vp_results[i].index));
        }
    }

    /* Q2 — regiao fraud: alto amount, online, unknown merchant, alto km */
    {
        float q[VECTOR_DIMS] = {
            0.75f, 0.85f, 1.0f,  0.15f, 0.3f,
            0.005f, 0.7f, 0.7f,  0.75f, 1.0f,
            0.0f,  1.0f,  0.8f,  0.008f
        };

        brute_force_knn((float *)full_vectors, FULL_N, q, KNN_K, bf_results);
        int found = vptree_search(&tree, q, KNN_K, vp_results);
        ASSERT_INT_EQ(found, KNN_K);

        for (int i = 0; i < KNN_K; i++) {
            ASSERT_TRUE(vp_contains(vp_results, KNN_K, bf_results[i].index));
        }
        for (int i = 0; i < KNN_K; i++) {
            ASSERT_TRUE(bf_contains(bf_results, KNN_K, vp_results[i].index));
        }
    }

    /* Q3 — regiao mista: amount medio, sentinel -1.0f nas dims 5 e 6 */
    {
        float q[VECTOR_DIMS] = {
            0.035f, 0.1667f, 0.05f, 0.7826f, 0.6667f,
            -1.0f,  -1.0f,          /* sentinels */
            0.04f,  0.2f,    0.0f,
            1.0f,   0.0f,    0.25f, 0.03f
        };

        brute_force_knn((float *)full_vectors, FULL_N, q, KNN_K, bf_results);
        int found = vptree_search(&tree, q, KNN_K, vp_results);
        ASSERT_INT_EQ(found, KNN_K);

        for (int i = 0; i < KNN_K; i++) {
            ASSERT_TRUE(vp_contains(vp_results, KNN_K, bf_results[i].index));
        }
        for (int i = 0; i < KNN_K; i++) {
            ASSERT_TRUE(bf_contains(bf_results, KNN_K, vp_results[i].index));
        }
    }

    vptree_free(&tree);
    return 0;
}

/* =========================================================================
 * Suite runner — chamada de test_main.c
 * ========================================================================= */
int run_vptree_tests(void)
{
    printf("\n=== VP-Tree Tests ===\n");

    RUN_TEST(test_vptree_returns_exactly_k_neighbors);
    RUN_TEST(test_vptree_matches_brute_force_mini_dataset);
    RUN_TEST(test_vptree_handles_sentinel_minus_one);
    RUN_TEST(test_vptree_full_dataset_100_recall);

    return _tests_failed;
}
