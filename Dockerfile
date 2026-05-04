# Multi-stage build para Rinha de Backend 2026
# Stage 1: Builder — compila servidor C e gera index.bin
# Stage 2: Runtime — imagem minima com binario + index

# ============================================================================
# STAGE 1: BUILDER
# ============================================================================
FROM gcc:13-bookworm AS builder

# Instala ferramentas necessarias
RUN apt-get update && apt-get install -y \
    gzip \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copia codigo fonte
COPY src/ src/
COPY tools/ tools/
COPY resources/references.json.gz resources/

# Compila servidor HTTP (mongoose + yyjson) — clean first para forcar rebuild
RUN make -C src clean && make -C src

# Compila ferramenta de build do index
RUN make -C tools clean && make -C tools build-index

# Cria diretorio de saida
RUN mkdir -p /output

# Descomprime dataset (284MB JSON)
RUN cd resources && gzip -d references.json.gz && ls -lh references.json

# Gera index.bin (~219MB)
# Saida esperada: index.bin com header 64B + vectors 168MB + labels 3MB + vptree 48MB
RUN ./tools/build_index resources/references.json /output/index.bin \
    && ls -lh /output/index.bin

# Remove dados intermediarios para nao poluir a imagem
RUN rm -f resources/references.json

# ============================================================================
# STAGE 2: RUNTIME
# ============================================================================
FROM alpine:3.20

# Instala apenas dependencias minimas (libc, libm)
# mongoose usa POSIX, yyjson usa apenas libc
RUN apk add --no-cache \
    libc6-compat \
    libgcc

# Cria usuario nao-root
RUN adduser -u 1000 -D appuser

WORKDIR /app

# Copia binario compilado do builder (~200KB)
COPY --from=builder /build/src/server /app/server

# Copia index.bin do builder (~219MB)
COPY --from=builder /output/index.bin /data/index.bin

# Garante permissao de execucao
RUN chmod +x /app/server

# Troca para usuario nao-root
USER appuser

# Porta do servidor
EXPOSE 8080

# Healthcheck
HEALTHCHECK --interval=2s --timeout=2s --retries=20 --start-period=10s \
    CMD wget -q --spider http://127.0.0.1:8080/ready || exit 1

# Inicia servidor na porta 8080, lendo index.bin da /data/
CMD ["/app/server", "--port", "8080", "--index", "/data/index.bin"]
