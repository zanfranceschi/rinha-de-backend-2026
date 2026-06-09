#!/usr/bin/env bash
set -euo pipefail

# Acumula test/results.json em test/final-results.json (cria se não existir).

[[ -f test/final-results.json ]] || echo '[]' > test/final-results.json

tmp=$(mktemp)
jq --slurpfile new test/results.json \
   --arg ts "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
   '. + [$new[0] + {timestamp: $ts}]' \
   test/final-results.json > "$tmp"
mv "$tmp" test/final-results.json
