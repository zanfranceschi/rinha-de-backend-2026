#include "rinha.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int nearf(float a, float b) {
    return fabsf(a - b) < 0.0015f;
}

int main(void) {
    const char *payload =
        "{\"id\":\"tx-1329056812\",\"transaction\":{\"amount\":41.12,\"installments\":2,\"requested_at\":\"2026-03-11T18:45:53Z\"},"
        "\"customer\":{\"avg_amount\":82.24,\"tx_count_24h\":3,\"known_merchants\":[\"MERC-003\",\"MERC-016\"]},"
        "\"merchant\":{\"id\":\"MERC-016\",\"mcc\":\"5411\",\"avg_amount\":60.25},"
        "\"terminal\":{\"is_online\":false,\"card_present\":true,\"km_from_home\":29.2331036248},\"last_transaction\":null}";
    const float expected[RINHA_DIMS] = {
        0.004112f, 0.166667f, 0.05f, 0.782609f, 0.333333f, -1.0f, -1.0f,
        0.029233f, 0.15f, 0.0f, 1.0f, 0.0f, 0.15f, 0.006025f
    };

    rinha_payload parsed;
    rinha_vector vector;
    if (!rinha_parse_payload(payload, strlen(payload), &parsed)) {
        fprintf(stderr, "parse failed\n");
        return 1;
    }
    rinha_vectorize(&parsed, &vector);
    for (int i = 0; i < RINHA_DIMS; ++i) {
        if (!nearf(vector.v[i], expected[i])) {
            fprintf(stderr, "dim %d got %.6f expected %.6f\n", i, vector.v[i], expected[i]);
            return 1;
        }
    }
    puts("examples ok");
    return 0;
}
