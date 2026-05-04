#ifndef DISTANCE_H
#define DISTANCE_H

#include "vectorize.h"

/* Squared Euclidean distance (skip sqrt for KNN — order preserved) */
float distance_sq(const float a[VECTOR_DIMS], const float b[VECTOR_DIMS]);

/* Full Euclidean distance (for validation/debugging) */
float distance_euclidean(const float a[VECTOR_DIMS], const float b[VECTOR_DIMS]);

#endif
