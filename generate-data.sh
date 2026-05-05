#!/usr/bin/env bash
set -euo pipefail

# time ./data-generator/generate \
#     --refs 3000000 \
#     --refs-out resources/references.json \
#     --payloads 39100 \
#     --payloads-out test/test-data.json \
#     --fraud-ratio-refs 0.35 \
#     --fraud-ratio-payloads 0.47 \
#     --mcc-cfg resources/mcc_risk.json
# gzip -9 -k resources/references.json

time ./data-generator/generate \
    --reuse-refs \
    --payloads-seed 4242 \
    --payloads 54100 \
    --payloads-out test/test-data.json \
    --fraud-ratio-payloads 0.47 \
    --mcc-cfg resources/mcc_risk.json
