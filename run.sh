#!/usr/bin/env bash
mkdir test-dir
cp resources/references.json test-dir
k6 run test/test.js > /dev/null
cat summary.json | jq
rm summary.json
