#!/usr/bin/env bash

# $1=participant $2=repo-url $3=submission-id $4=commit $5=issue-number
# stdin: JSON com {participant, id, repo-url, results, runtime-info, ...}

cat test/results.json | jq

echo "exporting results for $1@$4 (issue #$5)"
