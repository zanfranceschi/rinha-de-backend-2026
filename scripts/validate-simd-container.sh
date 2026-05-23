#!/usr/bin/env bash
set -euo pipefail

COMPOSE_FILE="${COMPOSE_FILE:-docker-compose.simd.yml}"
READY_URL="${READY_URL:-http://127.0.0.1:9999/ready}"
HOST_ARCH="$(uname -m)"
BUILD_CPU_PROFILE="${BUILD_CPU_PROFILE:-generic}"
export BUILD_CPU_PROFILE

if [[ -z "${SIMD_REQUIRE_AVX2:-}" ]]; then
  if [[ "$HOST_ARCH" == "x86_64" || "$HOST_ARCH" == "amd64" ]]; then
    SIMD_REQUIRE_AVX2=1
  else
    SIMD_REQUIRE_AVX2=0
  fi
fi

if [[ -z "${SIMD_EXPECT_AVX2:-}" ]]; then
  SIMD_EXPECT_AVX2="$SIMD_REQUIRE_AVX2"
fi

cleanup() {
  docker compose -f "$COMPOSE_FILE" down -v --remove-orphans >/dev/null 2>&1 || true
}

trap cleanup EXIT

echo "Building SIMD validation image with BUILD_CPU_PROFILE=${BUILD_CPU_PROFILE}"
docker compose -f "$COMPOSE_FILE" build simd-tests simd-runtime
docker compose -f "$COMPOSE_FILE" run --rm -e SIMD_REQUIRE_AVX2="$SIMD_REQUIRE_AVX2" simd-tests
docker compose -f "$COMPOSE_FILE" up -d simd-runtime

for _ in $(seq 1 20); do
  if curl -sf -o /dev/null "$READY_URL"; then
    break
  fi
  sleep 2
done

curl -sf -o /dev/null "$READY_URL"

logs="$(docker compose -f "$COMPOSE_FILE" logs simd-runtime)"
printf '%s\n' "$logs"

grep -q 'target_arch=x86_64' <<<"$logs"
grep -q "build_cpu_profile=\"${BUILD_CPU_PROFILE}\"" <<<"$logs"
if [[ "$SIMD_EXPECT_AVX2" == "1" ]]; then
  grep -q 'avx2_detected=true' <<<"$logs"
  grep -q 'candidate_kernel=Avx2' <<<"$logs"
  grep -q 'centroid_kernel=Avx2' <<<"$logs"
else
  grep -Eq 'avx2_detected=(true|false)' <<<"$logs"
  grep -Eq 'candidate_kernel=(Scalar|Avx2)' <<<"$logs"
  grep -Eq 'centroid_kernel=(Scalar|Avx2)' <<<"$logs"
fi

echo "SIMD container validation passed."
