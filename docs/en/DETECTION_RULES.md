# Fraud detection rules

This document defines how your API should turn a transaction into a fraud detection vector. It covers the vectorization (the 14 dimensions) and the normalization rules. The vector search uses that vector to find, in the reference dataset, the 5 transactions most similar to the one that just arrived, and from there decide whether the new transaction is fraudulent.

If you are not yet familiar with the concept of vector search, it is worth starting with [VECTOR_SEARCH.md](./VECTOR_SEARCH.md) — there the topic is introduced in a didactic way, with a very simplified example.


## Flow overview

The flow below shows, with a real Rinha de Backend example of a legitimate transaction, the step by step your API should follow to decide on a transaction. In this case, a customer makes a low-value purchase at a merchant they already know, close to home.

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
2. vectorizes and normalizes (14 dimensions):
    [0.0041, 0.1667, 0.05, 0.7826, 0.3333, -1, -1, 0.0292, 0.15, 0, 1, 0, 0.15, 0.006]
          ↓
3. searches for the 5 nearest neighbors (e.g., Euclidean distance):
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

Notice the `-1` at positions 5 and 6: since `last_transaction` came as `null`, there are no "minutes since the last transaction" nor "km from the last transaction" to normalize.

## The 14 vector dimensions

Transactions ([realistic examples here](/resources/example-payloads.json)) need to be transformed into 14-position vectors, following the order and normalization rules below.

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
| 11  | `unknown_merchant`       | `1` if `merchant.id` is not in `customer.known_merchants`, else `0` (inverted: `1` = unknown) |
| 12  | `mcc_risk`               | `mcc_risk.json[merchant.mcc]` (default value `0.5`)                              |
| 13  | `merchant_avg_amount`    | `clamp(merchant.avg_amount / max_merchant_avg_amount)`                           |

The `clamp(x)` function keeps the value within the interval `[0.0, 1.0]` — anything below `0.0` becomes `0.0`, and anything above `1.0` becomes `1.0`.

### The special case of `last_transaction: null`

Indices 5 and 6 depend on the customer's previous transaction. When the current transaction is the customer's first (that is, `last_transaction` arrives as `null` in the payload), there is no value to normalize. In those cases, your API should use the sentinel value `-1` in those two positions. This `-1` is the only case in which the vector may contain a value outside the interval `[0.0, 1.0]`, and it serves precisely to distinguish "missing data" from a normalized value close to zero.


## Normalization constants

Some values that appear in the formulas, such as `max_amount` and `max_installments`, are defined in the file [normalization.json](/resources/normalization.json):

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


## How the decision is made

Once the vector is ready, your API should:

1. Find, in the reference dataset, the 5 vectors closest to the vector of the transaction that just arrived.
2. Compute `fraud_score` as the fraction of frauds among those 5 references — that is, `number_of_frauds / 5`.
3. Respond with `approved = fraud_score < 0.6`. The threshold of `0.6` is fixed.

To measure vector proximity, the examples in this document use **Euclidean distance** with *brute force* over the 14 dimensions. Note that you are free to choose any vector search algorithm/technique.

> **Important!** Using the test payloads as a reference or for fraud lookup is not allowed! The final tests will use different payloads, and doing this in the previews distorts the results and discourages other participants.


## Example of a fraudulent transaction

To contrast with the legitimate case in the overview, see how a fraudulent transaction looks: a high value, far from home, at an unknown merchant, with no previous transaction history. For the full payload format, see [API.md](./API.md).

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
2. vectorizes and normalizes (14 dimensions — note the `-1` at indices 5 and 6 due to `last_transaction: null`):
    [0.9506, 0.8333, 1.0, 0.2174, 0.8333, -1, -1, 0.9523, 1.0, 0, 1, 1, 0.75, 0.0055]
          ↓
3. searches for the 5 nearest neighbors:
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
