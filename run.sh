#!/usr/bin/env bash

k6 run --no-summary test/test.js > /dev/null 2>&1
cat test/results.json | jq
