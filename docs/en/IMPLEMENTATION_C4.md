# Implementation C4

This document describes the current Rust implementation added in this repository, not the generic competition topology. It focuses on the concrete runtime services, the internal components of the API, and the offline artifact pipeline that prepares the vector-search data.

Relevant source files:

- [`docker-compose.yml`](../../docker-compose.yml)
- [`nginx.conf`](../../nginx.conf)
- [`src/bin/server.rs`](../../src/bin/server.rs)
- [`src/lib.rs`](../../src/lib.rs)
- [`src/search.rs`](../../src/search.rs)
- [`src/bin/build_artifacts.rs`](../../src/bin/build_artifacts.rs)

## Level 1: System Context

```mermaid
flowchart LR
    card[Card authorization system<br/>or load-test client]
    service[Fraud detection backend<br/>Rust + nginx]
    dataset[Static reference data<br/>normalization.json<br/>mcc_risk.json<br/>references.json.gz]

    card -->|GET /ready<br/>POST /fraud-score| service
    dataset -->|offline preprocessing| service
    service -->|JSON: approved + fraud_score| card
```

### Notes

- The backend is an isolated fraud-scoring system. It receives transaction payloads and returns a decision.
- The large reference dataset is not queried as raw JSON at request time. It is transformed into compact artifacts before the API starts serving traffic.

## Level 2: Container Diagram

```mermaid
flowchart LR
    client[Client / k6 / card system]

    subgraph compose[docker-compose topology]
        lb[nginx load balancer<br/>port 9999<br/>round-robin only]
        api1[API instance 1<br/>Rust axum service]
        api2[API instance 2<br/>Rust axum service]
        artifacts[(Packed search artifacts<br/>meta.json<br/>centroids.bin<br/>vectors.bin<br/>labels.bin)]
        config[(Runtime config files<br/>normalization.json<br/>mcc_risk.json)]
    end

    client -->|HTTP| lb
    lb -->|proxy| api1
    lb -->|proxy| api2
    artifacts -->|memory-map on startup| api1
    artifacts -->|memory-map on startup| api2
    config -->|load on startup| api1
    config -->|load on startup| api2
```

### Notes

- `nginx` performs no business logic. It only forwards requests to the two upstream API containers.
- Each API instance loads the same read-only artifact set and answers requests independently.
- The API containers do not depend on an external database, cache, or vector store in the hot path.

## Level 3: API Component Diagram

```mermaid
flowchart TD
    req[HTTP request]
    ready[Readiness handler<br/>GET /ready]
    score[Fraud-score handler<br/>POST /fraud-score]
    parse[Request parser<br/>serde_json]
    vectorize[Vectorization module<br/>14-dim deterministic mapping]
    engine[Search engine<br/>IVF-style clustered scan<br/>SIMD kernel dispatch]
    topk[Top-5 aggregator<br/>fixed-size nearest set]
    decision[Decision module<br/>fraud_count / 5<br/>threshold 0.6]
    fallback[Fallback scorer<br/>always returns HTTP 200]
    resp[HTTP JSON response]

    req --> ready
    req --> score
    score --> parse
    parse -->|valid payload| vectorize
    parse -->|invalid payload| fallback
    vectorize -->|vector ok| engine
    vectorize -->|error| fallback
    engine --> topk
    engine -->|search error| fallback
    topk --> decision
    decision --> resp
    fallback --> resp
```

### Component responsibilities

- **Request parser**: deserializes the incoming JSON body into the Rust DTOs.
- **Vectorization module**: applies the exact 14-dimension mapping from the challenge rules, including UTC hour/day extraction, `-1` sentinels for missing last-transaction fields, clamping, and MCC fallback.
- **Search engine**: pads and quantizes the request vector, ranks coarse centroids, probes a bounded number of inverted lists, and computes squared Euclidean distance over packed vectors.
- **SIMD kernel dispatch**: selects `AVX2` kernels at startup on `x86_64` when available, otherwise uses the scalar implementations.
- **Top-5 aggregator**: maintains the current nearest five candidates without allocating a large sortable structure.
- **Decision module**: converts the five labels into `fraud_score` and `approved`.
- **Fallback scorer**: returns valid JSON on degraded paths so the system avoids non-200 responses during scoring.

## Level 4: Artifact Build Pipeline

```mermaid
flowchart LR
    raw[references.json.gz]
    build[build_artifacts binary]
    stream[Streaming JSON reader]
    quantize[Quantizer<br/>14-dim f32 to 16-lane i8]
    cluster[K-means style coarse clustering]
    assign[Cluster assignment + reorder]
    write[Artifact writer<br/>version 2 metadata]
    out[(meta.json<br/>packed_dimensions=16<br/>centroids.bin<br/>vectors.bin<br/>labels.bin)]

    raw --> build
    build --> stream
    stream --> quantize
    quantize --> cluster
    cluster --> assign
    assign --> write
    write --> out
```

### Notes

- The builder streams the gzipped reference array and does not require a raw expanded JSON file in the runtime image.
- Vectors are quantized from 14 logical dimensions into 16 signed-byte lanes; the last 2 lanes are zero padding for SIMD-friendly loads.
- Centroids are also stored as 16-lane records, with the last 2 `f32` lanes zeroed.
- `meta.json` now carries artifact `version = 2` and `packed_dimensions = 16`, so old artifacts fail fast instead of loading incorrectly.
- Reordered per-cluster storage keeps each inverted list contiguous, which makes probe scans sequential and cache-friendlier.

## Request Lifecycle

```mermaid
sequenceDiagram
    participant C as Client
    participant N as nginx
    participant A as Rust API
    participant S as Search engine

    C->>N: POST /fraud-score
    N->>A: proxied request
    A->>A: parse JSON
    A->>A: vectorize to 14 dims
    A->>A: pad query to 16 lanes
    A->>S: score(vector)
    S->>S: dispatch AVX2 or scalar kernels
    S->>S: rank padded centroids
    S->>S: scan probe lists
    S-->>A: top-5 labels
    A->>A: compute fraud_score
    A-->>N: 200 JSON
    N-->>C: 200 JSON
```

## Load-test observability

The API image has optional stdout observability for local load tests. It does not add routes, sidecars, or services, so the public API remains limited to `GET /ready` and `POST /fraud-score`.

Enable it with environment variables:

```bash
docker compose -f docker-compose.yml -f docker-compose.observability.yml up --build
```

Then run the load test and follow both API instances:

```bash
docker compose logs -f api1 api2
```

Every `OBS_INTERVAL_SECS` seconds, each API instance emits one aggregate log line with request rate, approved/denied counts, parse/vectorization/search errors, heuristic fallback rate, fraud-score buckets, latency buckets, estimated p99 bucket, probe count, target architecture, and selected SIMD kernels.

Useful knobs:

- `OBSERVABILITY=1` enables aggregate logging.
- `OBS_INTERVAL_SECS=5` controls the logging interval and is clamped to at least one second.
- `RUST_LOG=debug` enables per-request degraded-path diagnostics. Keep it at the default `info` during serious p99 measurements.

## Design Intent

- Keep the request path self-contained and read-only after startup.
- Move heavy dataset work into an offline build step.
- Pad vectors and centroids to 16 lanes so the runtime can use straightforward SIMD loads instead of tail handling on 14-dimension records.
- Prefer valid `200` JSON responses over surfacing request-path errors.
- Keep the runtime topology compliant with the competition requirement of one load balancer plus two API instances.

[← English README](./README.md)
