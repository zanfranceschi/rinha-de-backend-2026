#!/usr/bin/env bash
set -euo pipefail

variant="${1:-nginx}"
shift || true

case "${variant}" in
    nginx)
        compose_files=(-f docker-compose.yml)
        ;;
    haproxy-tcp)
        compose_files=(-f docker-compose.yml -f docker-compose.lb-haproxy-tcp.yml)
        ;;
    haproxy-uds)
        mkdir -p .run/sockets
        chmod 755 .run
        chmod 777 .run/sockets
        compose_files=(-f docker-compose.yml -f docker-compose.lb-haproxy-uds.yml)
        ;;
    custom)
        # Step 2: custom minimal L4 round-robin TCP→UDS load balancer
        mkdir -p .run/sockets
        chmod 755 .run
        chmod 777 .run/sockets
        compose_files=(-f docker-compose.yml -f docker-compose.lb-custom.yml)
        ;;
    *)
        echo "usage: $0 nginx|haproxy-tcp|haproxy-uds|custom [docker compose up args...]" >&2
        exit 2
        ;;
esac

exec docker compose "${compose_files[@]}" up --build "$@"
