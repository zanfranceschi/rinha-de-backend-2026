# Rinha de Backend 2026

## O Desafio

Construir uma API de detecção de fraude em transações financeiras.

A API recebe requisições de autorização de transações e deve responder se a transação é fraudulenta ou não, junto com um score de fraude.

## API

### `POST /fraud-score`

**Request:**
```json
{
    "id": "tx-1899203618",
    "transaction": {
        "amount": 26.27,
        "installments": 1,
        "requested_at": "2026-03-13T12:01:12Z"
    },
    "customer": {
        "avg_amount": 52.54,
        "tx_count_24h": 4,
        "known_merchants": ["MERC-016", "MERC-018", "MERC-013"]
    },
    "merchant": {
        "id": "MERC-018",
        "mcc": "5912",
        "avg_amount": 320.38
    },
    "terminal": {
        "is_online": false,
        "card_present": true,
        "km_from_home": 32.04
    },
    "last_transaction": {
        "timestamp": "2026-03-14T04:29:53Z",
        "km_from_current": 3.81
    }
}
```

> `last_transaction` pode ser `null`.

**Response:**
```json
{
    "approved": true,
    "fraud_score": 0.0
}
```

- `approved`: `true` se a transação deve ser aprovada, `false` caso contrário.
- `fraud_score`: score de fraude entre `0.0` (legítima) e `1.0` (fraude).

### `GET /ready`

Health check. Deve retornar `200` quando a API estiver pronta para receber requisições.

## Desenvolvimento

### Prerequisites

Using Nix:
```bash
nix-shell
```

Otherwise, install manually: `gcc`, `make`, `k6`, `jq`.

### Build the data generator

```bash
cd data-generator
make
cd ..
```

### Generate test data

```bash
# defaults: 200 references, 1000 payloads, 30% fraud ratio
./data-generator/generate

# custom sizes
./data-generator/generate --refs 500 --payloads 5000

# custom fraud ratio (50% fraud)
./data-generator/generate --fraud-ratio 0.50

# custom output paths
./data-generator/generate --refs-out /tmp/refs.json --payloads-out /tmp/data.json

# all options
./data-generator/generate --refs 500 --payloads 5000 --fraud-ratio 0.40 \
    --norm-cfg resources/normalization.json --mcc-cfg resources/mcc_risk.json \
    --refs-out resources/references.json --payloads-out test/test-data.json

# see all options
./data-generator/generate --help
```

Generated files:
- `resources/references.json` — labeled vectors for the fraud model
- `test/test-data.json` — test payloads with expected responses

## Reference Data

`resources/references.json` contains labeled vectors (`fraud` / `legit`) that can be used to build the fraud detection model.

## Tests

Load tests use [k6](https://k6.io/) and send the transactions defined in `test/test-data.json`.

Each entry contains:
- `request`: the payload sent to the API.
- `info.expected_response`: the expected response (`approved` and `fraud_score`).
- `info.vector`: the corresponding feature vector.

## Submission

Add a JSON file under `participants/` with your identifier (e.g. `participants/your-name.json`):

```json
[
    {
        "id": "my-submission",
        "repo": "https://github.com/your-user/your-repo"
    }
]
```

Your repo must contain a `docker-compose.yml` that exposes the API on port `9999`.

### Example

- [Clojure](https://github.com/zanfranceschi/rinha-de-backend-2026-exemplo-clojure) — see [`participants/zanfranceschi.json`](participants/zanfranceschi.json)
