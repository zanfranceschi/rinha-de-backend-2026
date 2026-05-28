# Load-balancer variants

The root `docker-compose.yml` keeps nginx as the default submission-friendly load balancer. The API replicas expose a stable upstream contract so the `lb` service can be replaced with compose overlays:

- `LISTEN_MODE=tcp|unix`, default `tcp`.
- `BIND_ADDR=0.0.0.0:9999` for TCP upstreams.
- `BIND_SOCKET=/sockets/apiN.sock` for Unix-socket upstreams.

The external client contract does not change. Clients still call `GET /ready` and `POST /fraud-score` on host port `9999`, and k6 can keep using the same `BASE_URL`.

## Variants

Default nginx:

```bash
docker compose up --build
```

HAProxy over TCP:

```bash
docker compose -f docker-compose.yml -f docker-compose.lb-haproxy-tcp.yml up --build
```

HAProxy over Unix sockets:

```bash
./scripts/run-lb-variant.sh haproxy-uds
```

The UDS variant creates `.run/sockets` as a runtime-only bind mount shared by `api1`, `api2`, and `lb`. The directory is ignored by git.

You can use the helper for all variants:

```bash
./scripts/run-lb-variant.sh nginx
./scripts/run-lb-variant.sh haproxy-tcp
./scripts/run-lb-variant.sh haproxy-uds
./scripts/run-lb-variant.sh custom
```

Custom minimal L4 UDS round-robin (Step 2):

```bash
./scripts/run-lb-variant.sh custom
```

This variant builds the tiny custom `lb` binary (src/bin/lb.rs) and uses the `lb-runtime` stage. It performs pure TCP→Unix socket byte forwarding with simple round-robin. No HTTP proxy overhead. APIs must listen on Unix sockets (`LISTEN_MODE=unix`). The `.run/sockets` directory is still used for the shared volume.

## Comparison workflow

Use the same load-test inputs for every variant:

```bash
PROBE_COUNT=6 BUILD_CPU_PROFILE=haswell ./scripts/run-lb-variant.sh nginx
PROBE_COUNT=6 BUILD_CPU_PROFILE=haswell ./scripts/run-lb-variant.sh haproxy-tcp
PROBE_COUNT=6 BUILD_CPU_PROFILE=haswell ./scripts/run-lb-variant.sh haproxy-uds
```

Compare `p99`, `http_errors`, `dropped_iterations`, and the API observability logs for RPS and p99 buckets. The load balancer must remain a distributor only: no fraud rules, payload inspection, conditional fraud responses, or request-body transformations belong in any LB variant.

The "custom" variant demonstrates a minimal hand-written L4 forwarder (no nginx/HAProxy). It is intentionally simple for Step 2 of the LB improvement path. For production submission you may keep the default nginx (or HAProxy UDS) unless the custom LB has been thoroughly validated under load.

[← Implementation C4](./IMPLEMENTATION_C4.md)
