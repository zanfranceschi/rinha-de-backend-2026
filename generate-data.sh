#!/usr/bin/env bash
set -euo pipefail


time ./data-generator/generate \
    --refs 50000 \
    --refs-out resources/references.json \
    --payloads 15000 \
    --payloads-out test/test-data.json \
    --fraud-ratio 0.35 \
    --mcc-cfg resources/mcc_risk.json \
