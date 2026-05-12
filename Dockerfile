FROM debian:trixie AS build-binaries

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        build-essential \
        make \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY include ./include
COPY src ./src
COPY tools ./tools
COPY Makefile .

RUN make -j"$(nproc)"

FROM build-binaries AS build-index

ARG RINHA_DATA_REF=d501ddc1e941b24014c3ce5a6b41ccc3054ec1a0
ARG IVF_CLUSTERS=1280
ARG IVF_SAMPLE=65536
ARG IVF_ITERS=6
ARG IVF_MAX_REFS=0
RUN mkdir -p /app/resources /app/out \
    && curl -fsSL "https://raw.githubusercontent.com/zanfranceschi/rinha-de-backend-2026/${RINHA_DATA_REF}/resources/references.json.gz" \
        -o /app/resources/references.json.gz \
    && /app/build/build-index /app/resources/references.json.gz /app/out/index.bin "${IVF_CLUSTERS}" "${IVF_SAMPLE}" "${IVF_ITERS}" "${IVF_MAX_REFS}"

FROM debian:trixie-slim AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build-binaries /app/build/rinha-api /app/rinha-api
COPY --from=build-binaries /app/build/rinha-lb /app/rinha-lb
COPY --from=build-index /app/out/index.bin /app/data/index.bin

ENV IVF_INDEX_PATH=/app/data/index.bin
EXPOSE 9999
