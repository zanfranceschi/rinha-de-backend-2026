# Exemplos Práticos

Quatro exemplos completos do fluxo de detecção de fraude — do payload bruto até a resposta.

> Pré-requisitos: [API.md](./API.md) (formato do payload) e [VETORIZACAO.md](./VETORIZACAO.md) (as 14 dimensões e normalização).

A busca pelos 5 vizinhos mais próximos entre as referências usa **distância euclidiana** sobre os vetores de 14 dimensões, mas você pode usar outros algoritmos de métrica de distância se preferir.


## Exemplo 1 — Transação legítima (score: 0.0)

```
1. recebe a requisição:
    {
      "id": "tx-1329056812",
      "transaction":      { "amount": 41.12, "installments": 2, "requested_at": "2026-03-11T18:45:53Z" },
      "customer":         { "avg_amount": 82.24, "tx_count_24h": 3, "known_merchants": ["MERC-003", "MERC-016"] },
      "merchant":         { "id": "MERC-016", "mcc": "5411", "avg_amount": 60.25 },
      "terminal":         { "is_online": false, "card_present": true, "km_from_home": 29.23 },
      "last_transaction": null
    }
          ↓
2. vetoriza/normaliza (14 dimensões):
    [0.0041, 0.1667, 0.05, 0.7826, 0.3333, -1, -1, 0.0292, 0.15, 0, 1, 0, 0.15, 0.006]
          ↓
3. encontra os 5 vizinhos mais próximos (distância euclidiana):
    dist=0.0340  legit
    dist=0.0488  legit
    dist=0.0509  legit
    dist=0.0591  legit
    dist=0.0592  legit
          ↓
4. computa o score (threshold 0.6):
    score = 0 fraudes / 5 = 0.0
    approved = score < 0.6 → true
          ↓
5. resposta:
    {
      "approved": true,
      "fraud_score": 0.0
    }
```


## Exemplo 2 — Transação fraudulenta (score: 1.0)

```
1. recebe a requisição:
    {
      "id": "tx-1788243118",
      "transaction":      { "amount": 4368.82, "installments": 8, "requested_at": "2026-03-17T02:04:06Z" },
      "customer":         { "avg_amount": 68.88, "tx_count_24h": 18, "known_merchants": ["MERC-004", "MERC-015", "MERC-017", "MERC-007"] },
      "merchant":         { "id": "MERC-062", "mcc": "7801", "avg_amount": 25.55 },
      "terminal":         { "is_online": true, "card_present": false, "km_from_home": 881.61 },
      "last_transaction": { "timestamp": "2026-03-17T01:58:06Z", "km_from_current": 660.92 }
    }
          ↓
2. vetoriza/normaliza (14 dimensões):
    [0.4369, 0.6667, 1.0, 0.087, 0.1667, 0.0042, 0.6609, 0.8816, 0.9, 1, 0, 1, 0.8, 0.0026]
          ↓
3. encontra os 5 vizinhos mais próximos (distância euclidiana):
    dist=0.1139  fraud
    dist=0.1154  fraud
    dist=0.1228  fraud
    dist=0.1260  fraud
    dist=0.1307  fraud
          ↓
4. computa o score (threshold 0.6):
    score = 5 fraudes / 5 = 1.0
    approved = score < 0.6 → false
          ↓
5. resposta:
    {
      "approved": false,
      "fraud_score": 1.0
    }
```


## Exemplo 3 — Transação limítrofe (score: 0.4)

```
1. recebe a requisição:
    {
      "id": "tx-2174907811",
      "transaction":      { "amount": 1265.15, "installments": 6, "requested_at": "2026-03-25T19:00:34Z" },
      "customer":         { "avg_amount": 349.94, "tx_count_24h": 5, "known_merchants": ["MERC-014", "MERC-001", "MERC-008", "MERC-003"] },
      "merchant":         { "id": "MERC-031", "mcc": "7802", "avg_amount": 107.11 },
      "terminal":         { "is_online": true, "card_present": false, "km_from_home": 136.51 },
      "last_transaction": { "timestamp": "2026-03-25T17:09:34Z", "km_from_current": 131.62 }
    }
          ↓
2. vetoriza/normaliza (14 dimensões):
    [0.1265, 0.5, 0.3615, 0.8261, 0.3333, 0.0771, 0.1316, 0.1365, 0.25, 1, 0, 1, 0.75, 0.0107]
          ↓
3. encontra os 5 vizinhos mais próximos (distância euclidiana):
    dist=0.2099  fraud
    dist=0.2370  fraud
    dist=0.2475  legit
    dist=0.2868  legit
    dist=0.3105  legit
          ↓
4. computa o score (threshold 0.6):
    score = 2 fraudes / 5 = 0.4
    approved = score < 0.6 → true
          ↓
5. resposta:
    {
      "approved": true,
      "fraud_score": 0.4
    }
```


## Exemplo 4 — Transação fraudulenta sem `last_transaction` (score: 1.0)

```
1. recebe a requisição:
    {
      "id": "tx-3330991687",
      "transaction":      { "amount": 9505.97, "installments": 10, "requested_at": "2026-03-14T05:15:12Z" },
      "customer":         { "avg_amount": 81.28, "tx_count_24h": 20, "known_merchants": ["MERC-008", "MERC-007", "MERC-005"] },
      "merchant":         { "id": "MERC-068", "mcc": "7802", "avg_amount": 54.86 },
      "terminal":         { "is_online": false, "card_present": true, "km_from_home": 952.27 },
      "last_transaction": null
    }
          ↓
2. vetoriza/normaliza (14 dimensões — note os `-1` nos índices 5 e 6 por causa do `last_transaction: null`):
    [0.9506, 0.8333, 1.0, 0.2174, 0.8333, -1, -1, 0.9523, 1.0, 0, 1, 1, 0.75, 0.0055]
          ↓
3. encontra os 5 vizinhos mais próximos (distância euclidiana):
    dist=0.2315  fraud
    dist=0.2384  fraud
    dist=0.2552  fraud
    dist=0.2667  fraud
    dist=0.2785  fraud
          ↓
4. computa o score (threshold 0.6):
    score = 5 fraudes / 5 = 1.0
    approved = score < 0.6 → false
          ↓
5. resposta:
    {
      "approved": false,
      "fraud_score": 1.0
    }
```
