# Fraud detection rules

This document defines the **rules that translate a transaction into a fraud detection vector**: the vectorization (14 dimensions) and normalization. The vector search uses this vector to find similar transactions in the reference dataset and decide whether the new transaction is fraudulent.

> If you don't even know what a vector search is, read [VECTOR_SEARCH.md](./VECTOR_SEARCH.md) first — that document explains the concept in a didactic way with a super-simplified example.


## Flow overview

```
1. receives the request:
    {
        "amount": 10.00,
        "installments": 12,
        "requested_at": "2026-04-20T12:34:56Z",
        "last_transaction_at": "2026-04-20T23:59:37Z"
    }
          ↓
2. vectorizes/normalizes
    [0.34 1.00 0.50 0.99]
          ↓
3. finds the 5 most similar references via vector search:
    [0.15 0.81 0.83 0.89]: legit
    [0.02 0.38 0.44 0.88]: fraud
    [0.95 0.02 0.20 0.52]: fraud
    [0.74 0.93 0.87 0.27]: legit
    [0.78 0.93 0.87 0.27]: legit
          ↓
4. computes the fraud score with a threshold of 0.6 for fraud:
    score: 2 frauds / 5 records = 0.4
    approved = score >= 0.6 → true
          ↓
5. responds with the result:
    {
        "approved": true,
        "fraud_score": 0.4
    }
```

> The example above is simplified (4 dimensions) to illustrate the flow. The actual specification uses **14 dimensions**, described below.


## The 14 vector dimensions

Transactions ([examples here](/resources/example-payloads.json)) must be transformed into 14-dimensional vectors following the order and normalization rules below.

| index | dimension                | formula                                                                          |
|-----|--------------------------|----------------------------------------------------------------------------------|
| 0   | `amount`                 | `clamp(transaction.amount / max_amount)`                                         |
| 1   | `installments`           | `clamp(transaction.installments / max_installments)`                             |
| 2   | `amount_vs_avg`          | `clamp((transaction.amount / customer.avg_amount) / amount_vs_avg_ratio)`        |
| 3   | `hour_of_day`            | `hour(transaction.requested_at) / 23`  (0-23, UTC)                               |
| 4   | `day_of_week`            | `day_of_week(transaction.requested_at) / 6`    (mon=0, sun=6)                    |
| 5   | `minutes_since_last_tx`  | `clamp(minutes / max_minutes)` or `-1` if `last_transaction: null`               |
| 6   | `km_from_last_tx`        | `clamp(last_transaction.km_from_current / max_km)` or `-1` if `last_transaction: null` |
| 7   | `km_from_home`           | `clamp(terminal.km_from_home / max_km)`                                          |
| 8   | `tx_count_24h`           | `clamp(customer.tx_count_24h / max_tx_count_24h)`                                |
| 9   | `is_online`              | `1` if `terminal.is_online`, else `0`                                            |
| 10  | `card_present`           | `1` if `terminal.card_present`, else `0`                                         |
| 11  | `unknown_merchant`       | `1` if `merchant.id is in customer.known_merchants`, else `0` (inverted: `1` = unknown) |
| 12  | `mcc_risk`               | `mcc_risk.json[merchant.mcc]` (default `0.5`)                                    |
| 13  | `merchant_avg_amount`    | `clamp(merchant.avg_amount / max_merchant_avg_amount)`                           |

The `clamp(x)` function restricts the value to the interval `[0.0, 1.0]`.


## Normalization constants

Some values, like `max_amount` and `max_installments`, are defined in the file [normalization.json](/resources/normalization.json):

```json
{
  "max_amount": 10000,
  "max_installments": 12,
  "amount_vs_avg_ratio": 10,
  "max_minutes": 1440,
  "max_km": 1000,
  "max_tx_count_24h": 20,
  "max_merchant_avg_amount": 10000
}
```

For more details about the reference files (including `mcc_risk.json` and `references.json.gz`), see [DATASET.md](./DATASET.md).

## Practical examples

Four complete examples of the fraud detection flow — from the raw payload to the response.

> Prerequisite: [API.md](./API.md) — payload format.

The search for the 5 nearest neighbors among the references uses **Euclidean distance** over the 14-dimensional vectors, but you can use other distance metric algorithms if you prefer.

### Example 1 — legitimate transaction (score: 0.0)

```
1. receives the request:
    {
      "id": "tx-1329056812",
      "transaction":      { "amount": 41.12, "installments": 2, "requested_at": "2026-03-11T18:45:53Z" },
      "customer":         { "avg_amount": 82.24, "tx_count_24h": 3, "known_merchants": ["MERC-003", "MERC-016"] },
      "merchant":         { "id": "MERC-016", "mcc": "5411", "avg_amount": 60.25 },
      "terminal":         { "is_online": false, "card_present": true, "km_from_home": 29.23 },
      "last_transaction": null
    }
          ↓
2. vectorizes/normalizes (14 dimensions):
    [0.0041, 0.1667, 0.05, 0.7826, 0.3333, -1, -1, 0.0292, 0.15, 0, 1, 0, 0.15, 0.006]
          ↓
3. finds the 5 nearest neighbors (Euclidean distance):
    dist=0.0340  legit
    dist=0.0488  legit
    dist=0.0509  legit
    dist=0.0591  legit
    dist=0.0592  legit
          ↓
4. computes the score (threshold 0.6):
    score = 0 frauds / 5 = 0.0
    approved = score < 0.6 → true
          ↓
5. response:
    {
      "approved": true,
      "fraud_score": 0.0
    }
```

### Example 2 — fraudulent transaction (score: 1.0)

```
1. receives the request:
    {
      "id": "tx-1788243118",
      "transaction":      { "amount": 4368.82, "installments": 8, "requested_at": "2026-03-17T02:04:06Z" },
      "customer":         { "avg_amount": 68.88, "tx_count_24h": 18, "known_merchants": ["MERC-004", "MERC-015", "MERC-017", "MERC-007"] },
      "merchant":         { "id": "MERC-062", "mcc": "7801", "avg_amount": 25.55 },
      "terminal":         { "is_online": true, "card_present": false, "km_from_home": 881.61 },
      "last_transaction": { "timestamp": "2026-03-17T01:58:06Z", "km_from_current": 660.92 }
    }
          ↓
2. vectorizes/normalizes (14 dimensions):
    [0.4369, 0.6667, 1.0, 0.087, 0.1667, 0.0042, 0.6609, 0.8816, 0.9, 1, 0, 1, 0.8, 0.0026]
          ↓
3. finds the 5 nearest neighbors (Euclidean distance):
    dist=0.1139  fraud
    dist=0.1154  fraud
    dist=0.1228  fraud
    dist=0.1260  fraud
    dist=0.1307  fraud
          ↓
4. computes the score (threshold 0.6):
    score = 5 frauds / 5 = 1.0
    approved = score < 0.6 → false
          ↓
5. response:
    {
      "approved": false,
      "fraud_score": 1.0
    }
```

### Example 3 — borderline transaction (score: 0.4)

```
1. receives the request:
    {
      "id": "tx-2174907811",
      "transaction":      { "amount": 1265.15, "installments": 6, "requested_at": "2026-03-25T19:00:34Z" },
      "customer":         { "avg_amount": 349.94, "tx_count_24h": 5, "known_merchants": ["MERC-014", "MERC-001", "MERC-008", "MERC-003"] },
      "merchant":         { "id": "MERC-031", "mcc": "7802", "avg_amount": 107.11 },
      "terminal":         { "is_online": true, "card_present": false, "km_from_home": 136.51 },
      "last_transaction": { "timestamp": "2026-03-25T17:09:34Z", "km_from_current": 131.62 }
    }
          ↓
2. vectorizes/normalizes (14 dimensions):
    [0.1265, 0.5, 0.3615, 0.8261, 0.3333, 0.0771, 0.1316, 0.1365, 0.25, 1, 0, 1, 0.75, 0.0107]
          ↓
3. finds the 5 nearest neighbors (Euclidean distance):
    dist=0.2099  fraud
    dist=0.2370  fraud
    dist=0.2475  legit
    dist=0.2868  legit
    dist=0.3105  legit
          ↓
4. computes the score (threshold 0.6):
    score = 2 frauds / 5 = 0.4
    approved = score < 0.6 → true
          ↓
5. response:
    {
      "approved": true,
      "fraud_score": 0.4
    }
```

### Example 4 — fraudulent transaction without `last_transaction` (score: 1.0)

```
1. receives the request:
    {
      "id": "tx-3330991687",
      "transaction":      { "amount": 9505.97, "installments": 10, "requested_at": "2026-03-14T05:15:12Z" },
      "customer":         { "avg_amount": 81.28, "tx_count_24h": 20, "known_merchants": ["MERC-008", "MERC-007", "MERC-005"] },
      "merchant":         { "id": "MERC-068", "mcc": "7802", "avg_amount": 54.86 },
      "terminal":         { "is_online": false, "card_present": true, "km_from_home": 952.27 },
      "last_transaction": null
    }
          ↓
2. vectorizes/normalizes (14 dimensions — note the `-1` at indices 5 and 6 due to `last_transaction: null`):
    [0.9506, 0.8333, 1.0, 0.2174, 0.8333, -1, -1, 0.9523, 1.0, 0, 1, 1, 0.75, 0.0055]
          ↓
3. finds the 5 nearest neighbors (Euclidean distance):
    dist=0.2315  fraud
    dist=0.2384  fraud
    dist=0.2552  fraud
    dist=0.2667  fraud
    dist=0.2785  fraud
          ↓
4. computes the score (threshold 0.6):
    score = 5 frauds / 5 = 1.0
    approved = score < 0.6 → false
          ↓
5. response:
    {
      "approved": false,
      "fraud_score": 1.0
    }
```
