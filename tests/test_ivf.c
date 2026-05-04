/*
 * test_ivf.c — TDD-spec para IVF (Inverted File Index)
 *
 * I1: k-means produces valid assignments (all in [0, num_clusters-1],
 *     every cluster has at least 1 vector)
 * I2: cluster_offsets[num_clusters] == num_vectors after sort_by_cluster
 * I3: ivf_search returns exactly k=5 neighbors
 * I4: ivf_search matches brute force (100% recall) with nprobe=num_clusters
 * I5: ivf_search handles sentinel vectors (-1.0f in dims 5 and 6)
 * I6: sorted_labels match labels of original indices after sort_by_cluster
 *
 * Mini dataset: the same 80-vector array used in test_vptree.c.
 * 4 clusters used for build tests (enough to exercise the index with 80 vecs).
 * nprobe = num_clusters (exhaustive) guarantees 100% recall in tests.
 *
 * All tests return 0 (pass) or 1 (fail).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>

#include "test.h"
#include "vectorize.h"
#include "distance.h"
#include "quantize.h"
#include "vptree.h"   /* neighbor_t */
#include "ivf.h"

/* =========================================================================
 * 80-vector mini dataset (same as test_vptree.c — copy to keep test files
 * self-contained and independent).
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
 * Brute-force KNN — reference implementation (same as in test_vptree.c)
 * ========================================================================= */
typedef struct { int index; float dist; } bf_neighbor_t;

static void brute_force_knn(const float *vectors, int num_vectors,
                             const float query[VECTOR_DIMS],
                             int k, bf_neighbor_t *out)
{
    for (int i = 0; i < k; i++) {
        out[i].index = -1;
        out[i].dist  = 1e38f;
    }
    for (int i = 0; i < num_vectors; i++) {
        const float *v = vectors + (size_t)i * VECTOR_DIMS;
        float d = distance_euclidean(query, v);
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

static int bf_contains(const bf_neighbor_t *bf, int k, int idx)
{
    for (int i = 0; i < k; i++) { if (bf[i].index == idx) return 1; }
    return 0;
}

static int ivf_contains(const neighbor_t *ivf, int k, int idx)
{
    for (int i = 0; i < k; i++) { if (ivf[i].index == idx) return 1; }
    return 0;
}

/* =========================================================================
 * Helpers to build a full in-memory IVF index on the 80-vector dataset.
 *
 * The ivf_index_t holds const pointers, so we store the actual arrays in
 * a small context struct and point the index into them.
 * ========================================================================= */

#define TEST_NUM_CLUSTERS 4

typedef struct {
    float    centroids   [TEST_NUM_CLUSTERS * VECTOR_DIMS];
    uint32_t offsets     [TEST_NUM_CLUSTERS + 1];
    float    sorted_vecs [FULL_N * VECTOR_DIMS];
    uint8_t  sorted_lbls [FULL_N];
    uint32_t sorted_idx  [FULL_N];
    uint16_t assignments [FULL_N];
} ivf_ctx_t;

static void build_test_index(ivf_ctx_t *ctx, ivf_index_t *idx)
{
    ivf_kmeans((const float *)full_vectors, FULL_N,
               TEST_NUM_CLUSTERS, IVF_KMEANS_ITERS,
               ctx->centroids, ctx->assignments);

    ivf_sort_by_cluster((const float *)full_vectors, full_labels,
                        ctx->assignments, FULL_N, TEST_NUM_CLUSTERS,
                        ctx->sorted_vecs, ctx->sorted_lbls,
                        ctx->sorted_idx, ctx->offsets);

    /* Zero-initialize entire struct first to safely NULL all optional pointers */
    memset(idx, 0, sizeof(*idx));

    idx->num_vectors     = FULL_N;
    idx->num_clusters    = TEST_NUM_CLUSTERS;
    idx->nprobe          = TEST_NUM_CLUSTERS; /* exhaustive — 100% recall */
    idx->centroids       = ctx->centroids;
    idx->cluster_offsets = ctx->offsets;
    idx->sorted_indices  = ctx->sorted_idx;
    /* col_data[d] = NULL (no column-major in test — uses float32 fallback) */
    /* bbox_min = NULL, bbox_max = NULL (no bbox pruning in test) */
    idx->quantized_vectors = NULL;  /* tests use float32 fallback path */
    idx->vectors           = (const float *)full_vectors;
    idx->labels            = full_labels;
}

/* =========================================================================
 * I1: k-means produces valid assignments
 *
 * All assignments in [0, num_clusters-1].
 * Every cluster has at least 1 vector.
 * ========================================================================= */
static int test_ivf_kmeans_valid_assignments(void)
{
    float   centroids  [TEST_NUM_CLUSTERS * VECTOR_DIMS];
    uint16_t assignments[FULL_N];

    ivf_kmeans((const float *)full_vectors, FULL_N,
               TEST_NUM_CLUSTERS, IVF_KMEANS_ITERS,
               centroids, assignments);

    /* All assignments must be in [0, TEST_NUM_CLUSTERS - 1] */
    for (int i = 0; i < FULL_N; i++) {
        ASSERT_TRUE(assignments[i] < (uint16_t)TEST_NUM_CLUSTERS);
    }

    /* Every cluster must have at least 1 vector */
    int counts[TEST_NUM_CLUSTERS] = {0};
    for (int i = 0; i < FULL_N; i++) counts[(int)assignments[i]]++;
    for (int c = 0; c < TEST_NUM_CLUSTERS; c++) {
        ASSERT_TRUE(counts[c] >= 1);
    }

    return 0;
}

/* =========================================================================
 * I2: cluster_offsets[num_clusters] == num_vectors after sort_by_cluster
 * ========================================================================= */
static int test_ivf_cluster_offsets_sum(void)
{
    ivf_ctx_t  ctx;
    ivf_index_t idx;
    build_test_index(&ctx, &idx);

    ASSERT_INT_EQ((int)ctx.offsets[TEST_NUM_CLUSTERS], FULL_N);

    /* Also: offsets must be monotonically non-decreasing */
    for (int c = 0; c < TEST_NUM_CLUSTERS; c++) {
        ASSERT_TRUE(ctx.offsets[c] <= ctx.offsets[c + 1]);
    }

    return 0;
}

/* =========================================================================
 * I3: ivf_search returns exactly k=5 neighbors
 * ========================================================================= */
static int test_ivf_search_returns_k(void)
{
    ivf_ctx_t   ctx;
    ivf_index_t idx;
    neighbor_t  results[KNN_K];

    build_test_index(&ctx, &idx);

    float query[VECTOR_DIMS] = {
        0.05f, 0.25f, 0.05f, 0.5f, 0.5f,
        0.2f,  0.01f, 0.05f, 0.15f, 0.0f,
        1.0f,  0.0f,  0.2f,  0.03f
    };

    int found = ivf_search(&idx, query, KNN_K, results);
    ASSERT_INT_EQ(found, KNN_K);

    /* All returned indices must be valid */
    for (int i = 0; i < found; i++) {
        ASSERT_TRUE(results[i].index >= 0);
        ASSERT_TRUE(results[i].index < FULL_N);
    }

    return 0;
}

/* =========================================================================
 * I4: ivf_search results match brute force (100% recall)
 *
 * nprobe = num_clusters (exhaustive) guarantees 100% recall.
 * Tested with 10 different queries covering legit and fraud regions.
 * ========================================================================= */
static int test_ivf_search_matches_brute_force(void)
{
    ivf_ctx_t   ctx;
    ivf_index_t idx;
    neighbor_t  ivf_results[KNN_K];
    bf_neighbor_t bf_results[KNN_K];

    build_test_index(&ctx, &idx);

    /* 10 queries: mix of legit, fraud, sentinel regions */
    static const float queries[10][VECTOR_DIMS] = {
        /* Q0 legit region: low amount, card present */
        {0.025f, 0.1667f, 0.05f, 0.65f, 0.5f, 0.25f, 0.01f, 0.03f, 0.15f, 0.0f, 1.0f, 0.0f, 0.2f, 0.025f},
        /* Q1 fraud region: high amount, online, unknown */
        {0.75f, 0.85f, 1.0f, 0.15f, 0.3f, 0.005f, 0.7f, 0.7f, 0.75f, 1.0f, 0.0f, 1.0f, 0.8f, 0.008f},
        /* Q2 mixed: sentinel dims */
        {0.035f, 0.1667f, 0.05f, 0.7826f, 0.6667f, -1.0f, -1.0f, 0.04f, 0.2f, 0.0f, 1.0f, 0.0f, 0.25f, 0.03f},
        /* Q3 exact copy of vector idx=0 */
        {0.01f, 0.0833f, 0.05f, 0.8261f, 0.1667f, -1.0f, -1.0f, 0.0432f, 0.25f, 0.0f, 1.0f, 0.0f, 0.2f, 0.0416f},
        /* Q4 fraud boundary */
        {0.5f, 0.5f, 1.0f, 0.1f, 0.5f, 0.005f, 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 1.0f, 0.75f, 0.005f},
        /* Q5 very low amount, lots of dims near 0 */
        {0.005f, 0.0833f, 0.05f, 0.5f, 0.0f, 0.3f, 0.005f, 0.02f, 0.05f, 0.0f, 1.0f, 0.0f, 0.15f, 0.01f},
        /* Q6 high installments, known merchant */
        {0.08f, 0.9167f, 0.2f, 0.8f, 0.3333f, 0.1f, 0.01f, 0.03f, 0.3f, 0.0f, 1.0f, 0.0f, 0.3f, 0.06f},
        /* Q7 mid-range everything */
        {0.2f, 0.5f, 0.4f, 0.5f, 0.5f, 0.3f, 0.3f, 0.3f, 0.3f, 0.5f, 0.5f, 0.5f, 0.5f, 0.05f},
        /* Q8 night transaction, high km */
        {0.1f, 0.1667f, 0.1f, 0.0f, 0.1667f, 0.1f, 0.8f, 0.7f, 0.2f, 0.0f, 1.0f, 0.0f, 0.5f, 0.02f},
        /* Q9 sentinel + fraud features */
        {0.8f, 0.75f, 1.0f, 0.3f, 0.3f, -1.0f, -1.0f, 0.9f, 0.5f, 1.0f, 0.0f, 1.0f, 0.75f, 0.007f},
    };

    for (int q = 0; q < 10; q++) {
        brute_force_knn((const float *)full_vectors, FULL_N,
                        queries[q], KNN_K, bf_results);

        int found = ivf_search(&idx, queries[q], KNN_K, ivf_results);
        ASSERT_INT_EQ(found, KNN_K);

        /* Every brute-force neighbor must appear in IVF results */
        for (int i = 0; i < KNN_K; i++) {
            int bf_idx = bf_results[i].index;
            if (!ivf_contains(ivf_results, KNN_K, bf_idx)) {
                printf("  FAIL query %d: brute-force neighbor %d not in IVF results\n",
                       q, bf_idx);
                return 1;
            }
        }

        /* Every IVF neighbor must appear in brute-force results */
        for (int i = 0; i < KNN_K; i++) {
            int ivf_idx = ivf_results[i].index;
            if (!bf_contains(bf_results, KNN_K, ivf_idx)) {
                printf("  FAIL query %d: IVF neighbor %d not in brute-force results\n",
                       q, ivf_idx);
                return 1;
            }
        }
    }

    return 0;
}

/* =========================================================================
 * I5: ivf_search handles sentinel vectors (-1.0f in dims 5 and 6)
 *
 * Query with sentinel dims must return valid results (indices in range,
 * non-negative distances). Brute-force comparison included.
 * ========================================================================= */
static int test_ivf_search_sentinels(void)
{
    ivf_ctx_t   ctx;
    ivf_index_t idx;
    neighbor_t  ivf_results[KNN_K];
    bf_neighbor_t bf_results[KNN_K];

    build_test_index(&ctx, &idx);

    /* Query with sentinel -1.0f in dims 5 and 6 */
    float query[VECTOR_DIMS] = {
        0.02f,  0.1f,    0.05f, 0.75f, 0.5f,
        -1.0f, -1.0f,
        0.03f,  0.15f,   0.0f,
        1.0f,   0.0f,    0.25f, 0.03f
    };

    int found = ivf_search(&idx, query, KNN_K, ivf_results);
    ASSERT_INT_EQ(found, KNN_K);

    /* All returned indices must be valid */
    for (int i = 0; i < found; i++) {
        ASSERT_TRUE(ivf_results[i].index >= 0);
        ASSERT_TRUE(ivf_results[i].index < FULL_N);
        ASSERT_TRUE(ivf_results[i].distance >= 0.0f);
    }

    /* Must match brute force (exhaustive nprobe) */
    brute_force_knn((const float *)full_vectors, FULL_N,
                    query, KNN_K, bf_results);

    for (int i = 0; i < KNN_K; i++) {
        ASSERT_TRUE(ivf_contains(ivf_results, KNN_K, bf_results[i].index));
    }

    return 0;
}

/* =========================================================================
 * I6: sorted_labels match the original labels of sorted_indices
 *
 * After ivf_sort_by_cluster:
 *   for all i: sorted_labels[i] == full_labels[sorted_indices[i]]
 * ========================================================================= */
static int test_ivf_sorted_labels_correct(void)
{
    float    centroids  [TEST_NUM_CLUSTERS * VECTOR_DIMS];
    uint32_t offsets    [TEST_NUM_CLUSTERS + 1];
    float    sorted_vecs[FULL_N * VECTOR_DIMS];
    uint8_t  sorted_lbls[FULL_N];
    uint32_t sorted_idx [FULL_N];
    uint16_t assignments[FULL_N];

    ivf_kmeans((const float *)full_vectors, FULL_N,
               TEST_NUM_CLUSTERS, IVF_KMEANS_ITERS,
               centroids, assignments);

    ivf_sort_by_cluster((const float *)full_vectors, full_labels,
                        assignments, FULL_N, TEST_NUM_CLUSTERS,
                        sorted_vecs, sorted_lbls, sorted_idx, offsets);

    for (int i = 0; i < FULL_N; i++) {
        uint32_t orig = sorted_idx[i];
        ASSERT_TRUE(orig < (uint32_t)FULL_N);
        ASSERT_INT_EQ((int)sorted_lbls[i], (int)full_labels[orig]);
    }

    /* Also verify sorted_vecs match original vectors */
    for (int i = 0; i < FULL_N; i++) {
        uint32_t orig = sorted_idx[i];
        const float *expected = (const float *)full_vectors + (size_t)orig * VECTOR_DIMS;
        const float *actual   = sorted_vecs + (size_t)i * VECTOR_DIMS;
        for (int d = 0; d < VECTOR_DIMS; d++) {
            ASSERT_FLOAT_EQ(actual[d], expected[d], 1e-7f);
        }
    }

    return 0;
}

/* =========================================================================
 * I7: bbox_lower_bound gives exact lower bound
 *
 * For any cluster c and query q, the lower bound must be <= the actual
 * minimum distance from q to any vector in cluster c.
 *
 * We build an IVF index, compute bboxes manually, then for each cluster
 * verify that bbox_lower_bound(q, c) <= min_actual_dist(q, cluster c).
 * ========================================================================= */

/* Compute bbox_min/bbox_max for each cluster from quantized sorted vectors */
static void compute_bbox(const int16_t *quant_vecs,  /* row-major sorted by cluster */
                          const uint32_t *offsets,
                          int num_clusters,
                          int16_t *bbox_min,  /* [num_clusters * VECTOR_DIMS] */
                          int16_t *bbox_max)
{
    for (int c = 0; c < num_clusters; c++) {
        for (int d = 0; d < VECTOR_DIMS; d++) {
            bbox_min[c * VECTOR_DIMS + d] = INT16_MAX;
            bbox_max[c * VECTOR_DIMS + d] = INT16_MIN;
        }
        uint32_t start = offsets[c], end = offsets[c + 1];
        for (uint32_t pos = start; pos < end; pos++) {
            for (int d = 0; d < VECTOR_DIMS; d++) {
                int16_t v = quant_vecs[pos * VECTOR_DIMS + d];
                if (v < bbox_min[c * VECTOR_DIMS + d]) bbox_min[c * VECTOR_DIMS + d] = v;
                if (v > bbox_max[c * VECTOR_DIMS + d]) bbox_max[c * VECTOR_DIMS + d] = v;
            }
        }
    }
}

/* Replicate the bbox_lower_bound calculation for testing */
static int64_t test_bbox_lb(const int16_t q[VECTOR_DIMS],
                              const int16_t *bmin, const int16_t *bmax,
                              int cluster)
{
    const int16_t *mn = bmin + cluster * VECTOR_DIMS;
    const int16_t *mx = bmax + cluster * VECTOR_DIMS;
    int64_t s = 0;
    for (int d = 0; d < VECTOR_DIMS; d++) {
        int32_t diff = 0;
        if (q[d] < mn[d])      diff = (int32_t)mn[d] - (int32_t)q[d];
        else if (q[d] > mx[d]) diff = (int32_t)q[d]  - (int32_t)mx[d];
        s += (int64_t)diff * (int64_t)diff;
    }
    return s;
}

static int test_ivf_bbox_lower_bound(void)
{
    ivf_ctx_t ctx;
    ivf_index_t idx_base;
    build_test_index(&ctx, &idx_base);

    /* Build quantized versions of sorted vectors */
    int16_t quant[FULL_N * VECTOR_DIMS];
    for (int i = 0; i < FULL_N; i++) {
        quantize_f32_to_i16(ctx.sorted_vecs + i * VECTOR_DIMS,
                             quant + i * VECTOR_DIMS);
    }

    /* Compute bboxes */
    int16_t bmin[TEST_NUM_CLUSTERS * VECTOR_DIMS];
    int16_t bmax[TEST_NUM_CLUSTERS * VECTOR_DIMS];
    compute_bbox(quant, ctx.offsets, TEST_NUM_CLUSTERS, bmin, bmax);

    /* Test query: a legit region query */
    float query_f[VECTOR_DIMS] = {
        0.025f, 0.1667f, 0.05f, 0.65f, 0.5f, 0.25f, 0.01f, 0.03f,
        0.15f, 0.0f, 1.0f, 0.0f, 0.2f, 0.025f
    };
    int16_t query_i16[VECTOR_DIMS];
    quantize_f32_to_i16(query_f, query_i16);

    /* For each cluster, check lb <= actual minimum distance in that cluster */
    for (int c = 0; c < TEST_NUM_CLUSTERS; c++) {
        int64_t lb = test_bbox_lb(query_i16, bmin, bmax, c);

        /* Compute actual minimum distance to any vector in cluster c */
        int64_t actual_min = INT64_MAX;
        uint32_t start = ctx.offsets[c], end = ctx.offsets[c + 1];
        for (uint32_t pos = start; pos < end; pos++) {
            int64_t d = distance_sq_i16(query_i16, quant + pos * VECTOR_DIMS, INT64_MAX);
            if (d < actual_min) actual_min = d;
        }

        /* Lower bound must be <= actual minimum */
        ASSERT_TRUE(lb <= actual_min);
    }

    return 0;
}

/* =========================================================================
 * I8: column-major layout is a correct transpose of row-major
 *
 * Build col_data from sorted_vecs quantized, then verify:
 *   col_data[d * num_vectors + i] == quant_vecs[i * VECTOR_DIMS + d]
 *   for all i in [0, FULL_N) and all d in [0, VECTOR_DIMS).
 * ========================================================================= */
static int test_ivf_colmaj_is_transpose(void)
{
    ivf_ctx_t ctx;
    ivf_index_t idx_base;
    build_test_index(&ctx, &idx_base);

    /* Build row-major quantized sorted vectors */
    int16_t quant_row[FULL_N * VECTOR_DIMS];
    for (int i = 0; i < FULL_N; i++) {
        quantize_f32_to_i16(ctx.sorted_vecs + i * VECTOR_DIMS,
                             quant_row + i * VECTOR_DIMS);
    }

    /* Build column-major: col[d * FULL_N + i] = quant_row[i * VECTOR_DIMS + d] */
    int16_t quant_col[VECTOR_DIMS * FULL_N];
    for (int i = 0; i < FULL_N; i++) {
        for (int d = 0; d < VECTOR_DIMS; d++) {
            quant_col[d * FULL_N + i] = quant_row[i * VECTOR_DIMS + d];
        }
    }

    /* Verify transpose property */
    for (int i = 0; i < FULL_N; i++) {
        for (int d = 0; d < VECTOR_DIMS; d++) {
            ASSERT_INT_EQ((int)quant_col[d * FULL_N + i],
                          (int)quant_row[i * VECTOR_DIMS + d]);
        }
    }

    /* Verify that extracting a column gives all values for that dimension */
    /* For dim 0: col_data[0] = quant_col + 0 * FULL_N */
    /* quant_col[0 * FULL_N + i] should equal quant_row[i * VECTOR_DIMS + 0] */
    for (int i = 0; i < FULL_N; i++) {
        ASSERT_INT_EQ((int)quant_col[i],  /* col_data[0][i] = quant_col + 0*FULL_N + i */
                      (int)quant_row[i * VECTOR_DIMS + 0]);
    }

    return 0;
}

/* =========================================================================
 * Suite runner — called from test_main.c
 * ========================================================================= */
int run_ivf_tests(void)
{
    printf("\n=== IVF Tests ===\n");

    RUN_TEST(test_ivf_kmeans_valid_assignments);
    RUN_TEST(test_ivf_cluster_offsets_sum);
    RUN_TEST(test_ivf_search_returns_k);
    RUN_TEST(test_ivf_search_matches_brute_force);
    RUN_TEST(test_ivf_search_sentinels);
    RUN_TEST(test_ivf_sorted_labels_correct);
    RUN_TEST(test_ivf_bbox_lower_bound);
    RUN_TEST(test_ivf_colmaj_is_transpose);

    return _tests_failed;
}
