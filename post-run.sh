#!/usr/bin/env bash
set -euo pipefail

# $1=participant $2=repo-url $3=submission-id $4=commit $5=issue-number
#
# exemplo:
#   ./post-run.sh zanfranceschi https://github.com/zanfranceschi/repo clojure abc1234 99

PARTICIPANT="${1:?participant obrigatório}"
REPO_URL="${2:?repo-url obrigatório}"
SUBMISSION_ID="${3:?submission-id obrigatório}"
COMMIT="${4:?commit obrigatório}"
ISSUE="${5:-}"

RESULTS_DIR=temporary-results
RESULTS_FILE="$RESULTS_DIR/results-preview.json"
RESULTS_REPO=https://github.com/arinhadebackend/arinhadebackend.github.io.git
RESULTS_BRANCH=2026-preview

# se submission-id veio como "default", resolve pelo primeiro id de participants/<participant>.json
if [[ "$SUBMISSION_ID" == "default" ]]; then
    PARTICIPANTS_FILE="participants/${PARTICIPANT}.json"
    [[ -f "$PARTICIPANTS_FILE" ]] || { echo "erro: $PARTICIPANTS_FILE não encontrado" >&2; exit 1; }
    SUBMISSION_ID=$(jq -r '.[0].id // empty' "$PARTICIPANTS_FILE")
    [[ -n "$SUBMISSION_ID" ]] || { echo "erro: nenhum submission em $PARTICIPANTS_FILE" >&2; exit 1; }
    echo "submission-id 'default' resolvido pra '$SUBMISSION_ID' (de $PARTICIPANTS_FILE)"
fi

echo "exporting results for $PARTICIPANT/$SUBMISSION_ID @ $COMMIT (issue #$ISSUE)"

# clone limpo (idempotente)
rm -rf $RESULTS_DIR
git clone --depth 1 --branch "$RESULTS_BRANCH" "$RESULTS_REPO" "$RESULTS_DIR"

# atualiza o JSON (tmp + mv pra não corromper em caso de falha)
RESULTS=$(cat test/results.json)
tmp=$(mktemp)
TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
jq --indent 4 \
   --arg p "$PARTICIPANT" \
   --arg s "$SUBMISSION_ID" \
   --arg url "$REPO_URL" \
   --arg ts "$TIMESTAMP" \
   --argjson data "$RESULTS" \
   '.[$p][$s] = ($data + {repo_url: $url, timestamp: $ts})' \
   "$RESULTS_FILE" > "$tmp"
mv "$tmp" "$RESULTS_FILE"

# commit + push
msg="update $PARTICIPANT/$SUBMISSION_ID @${COMMIT:0:7}"
[[ -n "$ISSUE" ]] && msg="$msg (issue #$ISSUE)"

git -C "$RESULTS_DIR" add results-preview.json
git -C "$RESULTS_DIR" commit -m "$msg"
git -C "$RESULTS_DIR" push origin "$RESULTS_BRANCH"
