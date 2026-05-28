FROM --platform=linux/amd64 rust:1.95-slim AS builder
WORKDIR /app

ARG BUILD_CPU_PROFILE=generic
ARG ARTIFACT_INPUT=resources/references.json.gz
ARG ARTIFACT_CLUSTERS=2048
ARG ARTIFACT_PROBES=12

COPY Cargo.toml Cargo.lock ./
COPY src ./src
COPY tests ./tests
COPY resources ./resources

RUN cargo build --release --bin build_artifacts
RUN ./target/release/build_artifacts \
    --input ${ARTIFACT_INPUT} \
    --output /artifacts \
    --clusters ${ARTIFACT_CLUSTERS} \
    --probes ${ARTIFACT_PROBES}
RUN set -eux; \
    mkdir -p .cargo; \
    case "${BUILD_CPU_PROFILE}" in \
        generic) \
            printf '[build]\nrustflags = []\n' > .cargo/config.toml; \
            ;; \
        haswell) \
            printf '[build]\ntarget = "x86_64-unknown-linux-gnu"\n\n[target.x86_64-unknown-linux-gnu]\nrustflags = ["-C", "target-cpu=haswell", "-C", "target-feature=+avx2,+fma,+sse4.2,+popcnt"]\n' > .cargo/config.toml; \
            ;; \
        *) \
            echo "unsupported BUILD_CPU_PROFILE=${BUILD_CPU_PROFILE}; expected generic or haswell" >&2; \
            exit 1; \
            ;; \
    esac; \
    cat .cargo/config.toml; \
    cargo build --release --bin server; \
    if [ "${BUILD_CPU_PROFILE}" = "haswell" ]; then \
        cp target/x86_64-unknown-linux-gnu/release/server target/release/server; \
    fi; \
    cargo build --release --bin lb; \
    if [ "${BUILD_CPU_PROFILE}" = "haswell" ]; then \
        cp target/x86_64-unknown-linux-gnu/release/lb target/release/lb; \
    fi

FROM builder AS simd-test
ENV SIMD_REQUIRE_AVX2=1
CMD ["cargo", "test", "--test", "simd_runtime", "--", "--nocapture"]

FROM --platform=linux/amd64 debian:bookworm-slim AS runtime
WORKDIR /app

ARG BUILD_CPU_PROFILE=generic

RUN useradd -r -u 10001 appuser

COPY --from=builder /app/target/release/server /app/server
COPY --from=builder /artifacts /app/artifacts
COPY resources/normalization.json /app/resources/normalization.json
COPY resources/mcc_risk.json /app/resources/mcc_risk.json

RUN chown -R appuser:appuser /app

USER appuser
EXPOSE 9999

ENV LISTEN_MODE=tcp
ENV BIND_ADDR=0.0.0.0:9999
ENV ARTIFACT_DIR=/app/artifacts
ENV NORMALIZATION_PATH=/app/resources/normalization.json
ENV MCC_RISK_PATH=/app/resources/mcc_risk.json
ENV BUILD_CPU_PROFILE=${BUILD_CPU_PROFILE}

CMD ["/app/server"]

# --------------------------------------------------------------------
# Minimal runtime image for the custom L4 UDS round-robin load balancer.
# This stage produces a tiny container that only contains the lb binary.
# Intended for use with API instances listening on Unix sockets
# (LISTEN_MODE=unix + BIND_SOCKET).
#
# The LB does pure TCP→UDS byte piping with round-robin and no HTTP
# awareness / no business logic. It is the Step 2 replacement for nginx.
# --------------------------------------------------------------------
FROM --platform=linux/amd64 debian:bookworm-slim AS lb-runtime
WORKDIR /app

ARG BUILD_CPU_PROFILE=generic

RUN useradd -r -u 10002 lbuser && \
    mkdir -p /sockets && \
    chown -R lbuser:lbuser /sockets /app

COPY --from=builder /app/target/release/lb /app/lb

RUN chown lbuser:lbuser /app/lb && chmod 755 /app/lb

USER lbuser

EXPOSE 9999

# Defaults assume the standard .run/sockets layout used by the HAProxy UDS variant.
# Override via environment when launching.
ENV LB_LISTEN=0.0.0.0:9999
ENV BACKENDS=/sockets/api1.sock,/sockets/api2.sock
ENV BUILD_CPU_PROFILE=${BUILD_CPU_PROFILE}

# The LB itself has no /ready endpoint (it is a pure forwarder).
# Health is implicit: if it can accept TCP and the backends eventually appear, it works.
CMD ["/app/lb"]
