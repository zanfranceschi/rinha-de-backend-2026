# API — Contrato

Endpoints que sua submissão precisa implementar.

## `POST /fraud-score`

Recebe uma transação a ser autorizada e devolve a decisão.

### Request

Exemplo de payload (transação com histórico):

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

Exemplo com `last_transaction: null` (sem histórico de transação anterior):

```json
{
  "id": "tx-1329056812",
  "transaction": {
    "amount": 41.12,
    "installments": 2,
    "requested_at": "2026-03-11T18:45:53Z"
  },
  "customer": {
    "avg_amount": 82.24,
    "tx_count_24h": 3,
    "known_merchants": ["MERC-003", "MERC-016"]
  },
  "merchant": {
    "id": "MERC-016",
    "mcc": "5411",
    "avg_amount": 60.25
  },
  "terminal": {
    "is_online": false,
    "card_present": true,
    "km_from_home": 29.2331036248
  },
  "last_transaction": null
}
```

### Campos

| Campo                           | Tipo       | Descrição |
|---------------------------------|------------|-----------|
| `id`                            | string     | Identificador da transação (ex.: `tx-1329056812`) |
| `transaction.amount`            | number     | Valor da transação |
| `transaction.installments`      | integer    | Número de parcelas |
| `transaction.requested_at`      | string ISO | Timestamp UTC da requisição |
| `customer.avg_amount`           | number     | Média histórica de gasto do portador |
| `customer.tx_count_24h`         | integer    | Transações do portador nas últimas 24h |
| `customer.known_merchants`      | string[]   | Comerciantes já utilizados pelo portador |
| `merchant.id`                   | string     | Identificador do comerciante |
| `merchant.mcc`                  | string     | MCC (Merchant Category Code) |
| `merchant.avg_amount`           | number     | Ticket médio do comerciante |
| `terminal.is_online`            | boolean    | Transação online (`true`) ou presencial (`false`) |
| `terminal.card_present`         | boolean    | Cartão presente no terminal |
| `terminal.km_from_home`         | number     | Distância (km) do endereço do portador |
| `last_transaction`              | object \| `null` | Dados da transação anterior (pode ser `null`) |
| `last_transaction.timestamp`    | string ISO | Timestamp UTC da transação anterior |
| `last_transaction.km_from_current` | number  | Distância (km) entre a transação anterior e a atual |

> **Atenção:** `last_transaction` pode ser `null`. Nesse caso, as dimensões do vetor que dependem desses dados usam o valor sentinela `-1`. Veja [BUSCA_VETORIAL.md](./BUSCA_VETORIAL.md) e [DATASET.md](./DATASET.md).

### Vetor de 14 Dimensões

O payload é transformado em um vetor de 14 dimensões, **nesta ordem exata**. Valores são normalizados para `[0.0, 1.0]` usando `clamp`, exceto as dimensões `5` e `6` que podem ser `-1` (sentinela, quando `last_transaction: null`).

> **O que é `clamp`?** "Grampear" um valor dentro de um intervalo. Se passar do teto, vira o teto; se ficar abaixo do piso, vira o piso. Aqui o intervalo é sempre `[0, 1]`:
>
> ```
> clamp(x) = min(max(x, 0), 1)
> ```
>
> Exemplo: para `amount` com `max_amount = 10000`:
> - `R$ 8.200 → 8200/10000 = 0.82` → fica `0.82`
> - `R$ 15.000 → 15000/10000 = 1.50` → grampeado para `1.0`
>
> Sem `clamp`, uma transação muito alta dominaria o cálculo de distância — o `clamp` mantém todas as dimensões na mesma escala.

Constantes usadas (arquivo `resources/normalization.json`):

```json
{
  "max_amount":              10000,
  "max_installments":        12,
  "amount_vs_avg_ratio":     10,
  "max_minutes":             1440,
  "max_km":                  1000,
  "max_tx_count_24h":        20,
  "max_merchant_avg_amount": 10000
}
```

| idx | dimensão                 | fórmula                                                                          |
|-----|--------------------------|----------------------------------------------------------------------------------|
| 0   | `amount`                 | `clamp(transaction.amount / max_amount)`                                         |
| 1   | `installments`           | `clamp(transaction.installments / max_installments)`                             |
| 2   | `amount_vs_avg`          | `clamp((transaction.amount / customer.avg_amount) / amount_vs_avg_ratio)`        |
| 3   | `hour_of_day`            | `hour(transaction.requested_at) / 23`  (0-23, UTC)                               |
| 4   | `day_of_week`            | `dow(transaction.requested_at) / 6`    (seg=0, dom=6)                            |
| 5   | `minutes_since_last_tx`  | `clamp(minutos / max_minutes)` ou **`-1`** se `last_transaction: null`           |
| 6   | `km_from_last_tx`        | `clamp(last_transaction.km_from_current / max_km)` ou **`-1`** se `last_transaction: null` |
| 7   | `km_from_home`           | `clamp(terminal.km_from_home / max_km)`                                          |
| 8   | `tx_count_24h`           | `clamp(customer.tx_count_24h / max_tx_count_24h)`                                |
| 9   | `is_online`              | `1` se `terminal.is_online`, senão `0`                                           |
| 10  | `card_present`           | `1` se `terminal.card_present`, senão `0`                                        |
| 11  | `unknown_merchant`       | **`1` se `merchant.id ∉ customer.known_merchants`**, senão `0` (invertido: `1` = desconhecido) |
| 12  | `mcc_risk`               | `mcc_risk.json[merchant.mcc]` (default `0.5`)                                    |
| 13  | `merchant_avg_amount`    | `clamp(merchant.avg_amount / max_merchant_avg_amount)`                           |

**Exemplo concreto** (do `tx-1329056812` mostrado acima):

```
amount=41.12, installments=2, avg_amount=82.24, requested_at=2026-03-11T18:45:53Z (qua),
last_transaction=null, km_from_home=29.23, tx_count_24h=3,
is_online=false, card_present=true, merchant.id="MERC-016" ∈ known,
mcc="5411" (risco 0.15), merchant.avg_amount=60.25

vetor = [0.0041, 0.1667, 0.05, 0.7826, 0.3333, -1, -1, 0.0292, 0.15, 0, 1, 0, 0.15, 0.006]
```

### Scores de Risco por MCC

O arquivo `resources/mcc_risk.json` mapeia códigos MCC (Merchant Category Code) para um score de risco entre `0.0` e `1.0`:

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

Se o MCC da requisição **não estiver** no arquivo, use `0.5` como default.

### Response

```json
{
  "approved": true,
  "fraud_score": 0.0
}
```

| Campo         | Tipo    | Descrição |
|---------------|---------|-----------|
| `approved`    | boolean | `true` se a transação deve ser aprovada, `false` caso contrário |
| `fraud_score` | number  | Score de fraude entre `0.0` (legítima) e `1.0` (fraude). Informativo — a avaliação considera apenas `approved` |

O HTTP status é sempre `200` para requisições válidas.

---

## `GET /ready`

Health check. Deve retornar `200` quando a API estiver pronta para receber requisições (dataset carregado, dependências inicializadas).
