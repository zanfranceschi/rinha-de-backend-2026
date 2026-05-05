# Dataset — reference files

In this edition, you need information contained in files distributed in this repository.

| File | Size | Purpose |
|---|---|---|
| [`resources/references.json.gz`](/resources/references.json.gz) | ~16 MB (gzipped) / ~284 MB | 3,000,000 labeled vectors — the reference base that your vector search queries. |
| [`resources/mcc_risk.json`](/resources/mcc_risk.json) | <1 KB | Risk score by MCC (merchant category). |
| [`resources/normalization.json`](/resources/normalization.json) | <1 KB | Constants for normalizing payload fields. |


## `references.json.gz` — labeled reference vectors

This is the main dataset against which your vector search runs. Each record has two fields: `vector` (14 dimensions, in the order defined in [DETECTION_RULES.md](./DETECTION_RULES.md)) and `label` (`"fraud"` or `"legit"`).

```json
[
  { "vector": [0.01, 0.0833, 0.05, 0.8261, 0.1667, -1, -1, 0.0432, 0.25, 0, 1, 0, 0.2, 0.0416], "label": "legit" },
  { "vector": [0.5796, 0.9167, 1.0, 0.0435, 0, 0.0056, 0.4394, 0.4598, 0.4, 1, 0, 1, 0.85, 0.0032], "label": "fraud" }
]
```

**Why is it gzipped?** The uncompressed file is about 284 MB; compressed, it drops to about 16 MB. The `.gz` version is distributed to save space.

**The `-1` sentinel value.** Indices `5` (`minutes_since_last_tx`) and `6` (`km_from_last_tx`) receive `-1` when the transaction arrives with `last_transaction: null` (no previous transaction). Since `-1` sits clearly outside the `0.0–1.0` range, "no history" transactions naturally end up close to other "no history" transactions in the vector space — KNN groups both situations together without any special handling. The dataset vectors follow the same convention, so you **cannot filter or replace** these `-1` values.

**For inspection.** The official file is large and inconvenient to open directly. You can use [`resources/example-references.json`](/resources/example-references.json) — a small uncompressed excerpt that follows the same format.


## `mcc_risk.json` — risk score by MCC

This file maps the MCC (Merchant Category Code, present in `merchant.mcc` of the payload) to a value between `0.0` (safe category) and `1.0` (risky category). The value is consumed directly by index `12` (`mcc_risk`) of the vector.

Full file contents:

```json
{
  "5411": 0.15,
  "5812": 0.30,
  "5912": 0.20,
  "5944": 0.45,
  "7801": 0.80,
  "7802": 0.75,
  "7995": 0.85,
  "4511": 0.35,
  "5311": 0.25,
  "5999": 0.50
}
```

**MCC not listed?** `0.5` can be used as the default. The payload may bring MCCs that are not in the table — this is expected behavior.


## `normalization.json` — normalization constants

These are the constants used in the formulas of [DETECTION_RULES.md](./DETECTION_RULES.md). Full contents:

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

| Constant | Meaning |
|---|---|
| `max_amount` | Ceiling for `transaction.amount`; values above 10,000 are clamped to `1.0`. |
| `max_installments` | Ceiling for `transaction.installments` (12 installments = `1.0`). |
| `amount_vs_avg_ratio` | Extra divisor for the ratio `amount / customer.avg_amount`; 10× the average = `1.0`. |
| `max_minutes` | Time window for `minutes_since_last_tx`; 1,440 min = 24h. |
| `max_km` | Distance ceiling (km) for `km_from_home` and `km_from_last_tx`. |
| `max_tx_count_24h` | Ceiling for `customer.tx_count_24h`; 20 or more transactions in the last 24h are clamped to `1.0`. |
| `max_merchant_avg_amount` | Ceiling for the merchant's average ticket. |


**Important:** The three files do not change during the test or the edition, so they can be pre-processed freely — decompressed, indexed, converted to another format, etc.
