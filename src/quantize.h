#ifndef QUANTIZE_H
#define QUANTIZE_H

#include <stdint.h>
#include "vectorize.h"

/*
 * Quantization: maps float [-1.0, 1.0] to int16 [-10000, +10000]
 * Scale factor: 10000 (proven by top submissions)
 *
 * Memory reduction: float32 (4B) -> int16 (2B) = 2x
 * 3M * 14 * 4B = 168MB -> 3M * 14 * 2B = 84MB
 */
#define QUANT_SCALE 10000

/*
 * Quantize float32 vector to int16.
 * Each dimension: dst[i] = clamp((int16_t)(src[i] * QUANT_SCALE), -10000, 10000)
 * Sentinel -1.0f -> -10000 (exact, within int16 range).
 */
void quantize_f32_to_i16(const float src[VECTOR_DIMS], int16_t dst[VECTOR_DIMS]);

/*
 * Squared L2 distance between two int16 vectors with early exit.
 *
 * Computes sum of (query[i] - target[i])^2 for each dimension.
 * After each dimension, if the running sum exceeds tau, returns tau+1
 * immediately (early exit). This saves ~70% of computations on average
 * during KNN search where most candidates are far from the query.
 *
 * Scale: result is in int16 units squared. To convert to float32 distance:
 *   float_dist = (float)i16_dist / ((float)QUANT_SCALE * (float)QUANT_SCALE)
 *
 * Returns: squared distance (int64), or tau+1 if partial sum exceeds tau.
 * Use INT64_MAX as tau to disable early exit.
 */
int64_t distance_sq_i16(const int16_t query[VECTOR_DIMS],
                         const int16_t target[VECTOR_DIMS],
                         int64_t tau);

/*
 * Convert int16 squared distance to equivalent float32 distance scale.
 * float_dist = int16_dist / (QUANT_SCALE * QUANT_SCALE)
 *
 * This allows comparing int16 distances with float32 distances.
 */
static inline float i16_dist_to_f32(int64_t dist) {
    return (float)dist / ((float)QUANT_SCALE * (float)QUANT_SCALE);
}

#endif /* QUANTIZE_H */
