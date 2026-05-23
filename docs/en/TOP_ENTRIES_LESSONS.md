# Lessons from Top Rust Entries

Reviewed repositories:

- <https://github.com/gabrielrauch/rinha-2026>
- <https://github.com/jairoblatt/rinha-2026-rust>
- <https://github.com/psavelis/rinha-backend-2026>

These entries use similar Rust/vector-search stacks, but are much more specialized than this repository's current Axum/nginx implementation. The notes below capture reusable lessons and a practical order for applying them here.

## 1. Keep Each API Instance CPU-Budget Aware

Top entries run each API instance as a mostly single-threaded worker. This matches the contest budget: each API gets less than half a CPU, so extra runtime worker threads can add scheduling overhead without adding true parallel capacity.

Current gap:

- Our server uses `#[tokio::main]`, which defaults to Tokio's multi-thread runtime.
- Under load, our handler p99 is low while k6 p99 is much higher, which suggests queueing/scheduling outside the handler.

Candidate change:

- Cap Tokio to one worker thread per API instance.
- Retest with the current CPU split: `lb=0.10`, `api1=0.45`, `api2=0.45`.

## 2. Avoid Full Cluster Sorting Per Request

Top entries avoid sorting large candidate lists in the hot path. They use fixed-size top-N selection, partition pruning, bounding boxes, or exact tree traversal.

Current gap:

- Our search computes a `Vec<(distance, cluster)>` for every cluster and sorts it on every request.
- We only need the best `PROBE_COUNT` clusters, so a full sort is unnecessary work.

Candidate change:

- Replace full cluster sort with fixed-size top-N cluster selection.
- Keep the current artifact format initially; this is an incremental search-path improvement.

## 3. Warm and Pin the Mapped Index

Top entries treat cold pages as p99 risk. They `mmap` the index, use `MAP_POPULATE`/`madvise`/`mlock` where possible, touch hot regions on startup, and run warmup queries.

Current gap:

- Our artifacts are memory-mapped, but we do not explicitly populate, lock, or warm them.

Candidate change:

- Add startup page touching for `vectors.bin`, `labels.bin`, and centroids.
- Consider Linux-only `madvise`/`mlock` behind env flags so local/dev environments can opt out.

## 4. Minimize the Load-Balancer Hop

Top entries avoid generic byte proxying. Gabriel and psavelis use a custom round-robin load balancer that accepts TCP on port `9999` and passes the accepted client file descriptor to an API process via `SCM_RIGHTS`; the API serves the socket directly. Jairo uses Unix-socket upstreams with a custom LB image.

Current gap:

- We use nginx as a normal HTTP proxy.
- Tuning nginx and shifting CPU from API containers to nginx materially improved p99, proving the proxy layer matters.

Candidate changes:

- Short term: keep nginx tuned and the `0.10/0.45/0.45` CPU split.
- Medium term: test HAProxy with Unix-socket upstreams.
- Long term: consider a tiny custom `SCM_RIGHTS` round-robin LB.

## 5. Build for the Official CPU Profile

Top entries compile for the contest machine's x86_64/Haswell-era CPU. Examples include `target-cpu=haswell`, explicit `+avx2,+fma,+sse4.2,+popcnt`, and PGO.

Current gap:

- Our image is `linux/amd64`, but the main build is generic and uses runtime AVX2 dispatch.
- Local ARM/emulated Docker runs report `avx2_detected=false`, so local k6 results are not representative of official SIMD behavior.

Candidate changes:

- Keep a generic build path for portability.
- Add a submission-oriented build profile using explicit AVX2/FMA flags or `target-cpu=haswell`. Implemented as `BUILD_CPU_PROFILE=haswell`.
- Validate on real x86_64 with `avx2_detected=true`.
- A mid-2014 MacBook Pro with an i5-4278U can be used for AVX2 validation if `sysctl -a | grep -i avx2` shows `hw.optional.avx2_0: 1` and Docker can expose AVX2 inside a `linux/amd64` container. Big Sur may require an older Docker Desktop release or Linux installed on the machine.

## 6. Improve the Index Algorithm, Not Just Probe Count

Top entries get both speed and accuracy from better indexing:

- Gabriel uses a partitioned KD-tree with bounding-box pruning and early termination.
- Psavelis uses IVF with 4096 centroids, `nprobe=1`, bounding-box repair, int16/SoA layout, and AVX2/FMA kernels.
- Jairo uses IVF with a fast probe pass and only widens the pass when the top-5 fraud count is ambiguous.

Current gap:

- Our IVF-style search scans a fixed number of clusters and does not prove whether skipped clusters can improve the top-5.
- Lowering `PROBE_COUNT` only modestly improved latency and quickly hurt detection, so probe count alone is not the main long-term lever.

Candidate changes:

- Add bounding boxes per cluster and use lower-bound pruning.
- Add an ambiguity-aware wider pass: fast small probe first, wider probe only when fraud count is near the decision boundary.
- Longer term, consider a partitioned KD-tree or bbox-repair IVF format.

## Suggested Implementation Order

1. Cap Tokio worker threads to one per API instance.
2. Replace full cluster sorting with fixed-size top-N cluster selection.
3. Add mmap/index warmup.
4. Test HAProxy or Unix-socket upstreams.
5. Add a Haswell/AVX2 submission build profile.
6. Redesign the index around bounding-box pruning or repair.
