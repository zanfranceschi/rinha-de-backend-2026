# API

Your API must expose exactly two endpoints on port **9999** (see [ARCHITECTURE.md](./ARCHITECTURE.md)).


## `GET /ready`

Health check. Your API must return `HTTP 2xx` once it is ready to receive requests and be tested.


## `POST /fraud-score`

This is the fraud detection endpoint, the core of your submission. The payload format must match the example below:

```json
{
  "id": "tx-3576980410",
  "transaction": {
    "amount": 384.88,
    "installments": 3,
    "requested_at": "2026-03-11T20:23:35Z"
  },
  "customer": {
    "avg_amount": 769.76,
    "tx_count_24h": 3,
    "known_merchants": ["MERC-009", "MERC-001", "MERC-001"]
  },
  "merchant": {
    "id": "MERC-001",
    "mcc": "5912",
    "avg_amount": 298.95
  },
  "terminal": {
    "is_online": false,
    "card_present": true,
    "km_from_home": 13.7090520965
  },
  "last_transaction": {
    "timestamp": "2026-03-11T14:58:35Z",
    "km_from_current": 18.8626479774
  }
}
```

### Request fields

| Field                           | Type       | Description |
|---------------------------------|------------|-------------|
| `id`                            | string     | Transaction identifier (e.g., `tx-1329056812`) |
| `transaction.amount`            | number     | Transaction value |
| `transaction.installments`      | integer    | Number of installments |
| `transaction.requested_at`      | string ISO | UTC timestamp of the request |
| `customer.avg_amount`           | number     | Cardholder's historical spending average |
| `customer.tx_count_24h`         | integer    | Cardholder's transactions in the last 24h |
| `customer.known_merchants`      | string[]   | Merchants already used by the cardholder |
| `merchant.id`                   | string     | Merchant identifier |
| `merchant.mcc`                  | string     | MCC (Merchant Category Code), a code that identifies the merchant's line of business |
| `merchant.avg_amount`           | number     | Merchant's average ticket |
| `terminal.is_online`            | boolean    | Online transaction (`true`) or in-person (`false`) |
| `terminal.card_present`         | boolean    | Whether the physical card is present at the terminal |
| `terminal.km_from_home`         | number     | Distance (km) from the cardholder's address |
| `last_transaction`              | object \| `null` | Previous transaction data (may be `null`) |
| `last_transaction.timestamp`    | string ISO | UTC timestamp of the previous transaction |
| `last_transaction.km_from_current` | number  | Distance (km) between the previous transaction and the current one |

### Response

Your API's response must follow this format:

```json
{
  "approved": false,
  "fraud_score": 1.0
}
```

*Additional payload examples are available in [/resources/example-payloads.json](/resources/example-payloads.json). The file contains an array of payloads, but each payload is sent individually during the test.*

---

## How to decide `approved` and `fraud_score`

The detection logic (vectorization and vector search) is described in the following documents:

- **[DETECTION_RULES.md](./DETECTION_RULES.md)** — full specification of the 14 dimensions, normalization, and end-to-end flow examples.

- **[VECTOR_SEARCH.md](./VECTOR_SEARCH.md)** — an approachable explanation of the concept.
