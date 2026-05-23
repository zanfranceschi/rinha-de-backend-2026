#!/usr/bin/env bash

export K6_NO_USAGE_REPORT=true
export BASE_URL="${BASE_URL:-http://localhost:9999}"

k6 run test/test.js > /dev/null 2>&1
cat test/results.json | jq
