# Local Docker Setup

This repository includes a minimal production-like local stack:

- 2 API containers (`api-1` and `api-2`)
- 1 Nginx load balancer exposed on host port `9999`
- Round-robin balancing across both API instances
- Healthchecks on API containers and Nginx startup gated until both APIs are healthy

## Requirements

- Docker Engine + Docker Compose plugin
- Node.js 20+ (for local non-container test execution)
- Local Docker limits are set to share a total cap of 1 CPU and 350MB RAM across all services.

## Run

```bash
docker compose up --build
```

The API will be available at:

- `http://localhost:9999/ready`

## Resource limits (shared cap)

Docker Compose does not support a single global CPU or memory limit across multiple services. To stay within the shared target, per-service limits are set so the totals sum to 1 CPU and 350MB RAM:

- api-1: 0.4 CPU, 150MB RAM
- api-2: 0.4 CPU, 150MB RAM
- nginx: 0.2 CPU, 50MB RAM

This is the closest safe behavior for local Compose. If you change service counts or container needs, adjust the per-service limits so the totals stay within the shared cap.

## Validate balancing

Run multiple requests and check the `instance` field is distributed between `api-1` and `api-2` over time.

```bash
for i in $(seq 1 6); do curl -s http://localhost:9999/ready; echo; done
```

Expected shape:

```json
{"status":"ok","instance":"api-1"}
{"status":"ok","instance":"api-2"}
```

Note: round-robin in Nginx distributes requests across upstreams, but strict one-by-one alternation is not guaranteed in every run.

## Local test

```bash
npm test
```

## Compose syntax validation

```bash
docker compose config
```