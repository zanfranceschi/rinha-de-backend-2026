/*
 * distance.c — Implementacao GREEN de distancia euclidiana
 *
 * Regras criticas:
 *   - distance_sq: sem sqrt (ordem preservada para KNN)
 *   - float32 SEMPRE (nunca float64)
 *   - Sentinel -1.0f participa normalmente do calculo (NAO e ignorado)
 *   - VECTOR_DIMS = 14
 */

#include "distance.h"

#include <math.h>

/* Soma dos quadrados das diferencas em todas as 14 dimensoes */
float distance_sq(const float a[VECTOR_DIMS], const float b[VECTOR_DIMS])
{
    float sum = 0.0f;
    for (int i = 0; i < VECTOR_DIMS; i++) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sum;
}

/* Distancia euclidiana completa (para validacao e debug) */
float distance_euclidean(const float a[VECTOR_DIMS], const float b[VECTOR_DIMS])
{
    return sqrtf(distance_sq(a, b));
}
