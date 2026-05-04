/*
 * quantize.c — Implementacao GREEN da quantizacao int16
 *
 * Regras criticas:
 *   - QUANT_SCALE = 10000: range float [-1.0, 1.0] -> int16 [-10000, +10000]
 *   - Sentinel -1.0f -> -10000 (exato, dentro do range int16)
 *   - Clamp para [-10000, +10000] previne overflow int16
 *   - distance_sq_i16: early exit quando soma parcial excede tau
 *   - float32 SEMPRE (nunca float64) nas conversoes
 *   - int64_t para acumulador de distancia (evita overflow: 14 * 20000^2 = 5.6e9 > INT32_MAX)
 */

#include "quantize.h"

/*
 * Quantize float32 vector to int16.
 *
 * Formula: dst[i] = clamp((int)(src[i] * QUANT_SCALE), -QUANT_SCALE, QUANT_SCALE)
 *
 * Para valores em [-1.0, 1.0]:
 *   -1.0f * 10000 = -10000  (sentinel preservado)
 *    0.0f * 10000 = 0
 *    1.0f * 10000 = 10000
 *
 * O clamp e necessario para valores marginalmente fora de [-1,1] devido a
 * erros de ponto flutuante. int16_t range: -32768..+32767, portanto -10000..+10000
 * e seguro sem overflow de int16.
 */
void quantize_f32_to_i16(const float src[VECTOR_DIMS], int16_t dst[VECTOR_DIMS])
{
    for (int i = 0; i < VECTOR_DIMS; i++) {
        /* Multiplica por QUANT_SCALE e arredonda para inteiro mais proximo */
        float scaled = src[i] * (float)QUANT_SCALE;

        /* Clamp para evitar overflow de int16 */
        if (scaled > (float)QUANT_SCALE) {
            scaled = (float)QUANT_SCALE;
        } else if (scaled < -(float)QUANT_SCALE) {
            scaled = -(float)QUANT_SCALE;
        }

        dst[i] = (int16_t)scaled;
    }
}

/*
 * Squared L2 distance entre dois vetores int16 com early exit.
 *
 * Computa sum((query[i] - target[i])^2) dimensao por dimensao.
 * Apos cada dimensao, se sum > tau, retorna tau+1 imediatamente.
 *
 * Acumulador int64_t: necessario pois cada termo pode ser ate:
 *   (10000 - (-10000))^2 = 20000^2 = 400.000.000
 * 14 * 400.000.000 = 5.600.000.000 > INT32_MAX (2.147.483.647)
 *
 * Use INT64_MAX como tau para desabilitar early exit.
 */
int64_t distance_sq_i16(const int16_t query[VECTOR_DIMS],
                         const int16_t target[VECTOR_DIMS],
                         int64_t tau)
{
    int64_t sum = 0;

    for (int i = 0; i < VECTOR_DIMS; i++) {
        /* Diferenca em int32 para evitar overflow de int16 */
        int32_t diff = (int32_t)query[i] - (int32_t)target[i];
        sum += (int64_t)diff * (int64_t)diff;

        /* Early exit: se ja excede tau, nao vale continuar */
        if (sum > tau) {
            return tau + 1;
        }
    }

    return sum;
}
