# API

Endpoints que seu backend precisa implementar.

## `GET /ready`

Health check. Deve retornar `HTTP 2xx` quando a API estiver pronta para receber requisições.


## `POST /fraud-score`

Endpoint para detecção de fraudes. O formato do payload é como o seguinte exemplo:
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

### Campos da Requisição

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


**A resposta deve ser como este exemplo:**
```json
{
  "approved": false,
  "fraud_score": 1.0
}
```

---

# Lógica para Detecção de Fraude

Vamos usar um exemplo simplificado para explicar a lógica de detecção de fraude. As dimensões corretas e a ordem dos valores no vetor são explicadas em detalhes mais adiante.

A do processamento é a seguinte:
1. Transformar a transação no corpo da requisção (payload) que está em formato JSON para um vetor.
1. Realizar uma busca vetorial nas [referências](/resources/references.json.gz) pelas 5 referências mais similares.
1. Classificar a transação como fraudulenta ou legítima junto com seu score de fraude.

Por exemplo:

```
1. recebe a requisção:
{
    "amount": 10.00,
    "installments": 12,
    "requested_at": "2026-04-20T12:34:56Z",
    "last_transaction_at": "2026-04-20T23:59:37Z"
}
          ↓
2. vetoriza/normaliza
[0.34 1.00 0.50 0.99]
          ↓
3. encontra as 5 referências mais similares através de busca vetorial:
[0.15 0.81 0.83 0.89]: legit
[0.02 0.38 0.44 0.88]: fraud
[0.95 0.02 0.20 0.52]: fraud
[0.74 0.93 0.87 0.27]: legit
[0.78 0.93 0.87 0.27]: legit
          ↓
4. computa o score para fraude com threshold de 0.6 para fraudes:
    score: 2 fraudes / 5 registros = 0.4
    approved = score >= 0.6
          ↓
5. responde com o resultado:
{
  "approved": true,
  "fraud_score": 0.4
}
```

Seu endpoint `POST /fraud-score` deve realizar o seguinte processamento e responder se a transação requisição se trata de uma fraude ou não assim como seu `fraud score`. Para isso, você deve seguir estes passos:

### Vetorizar/normalizar a requisição em 14 dimensões

Por exemplo, o seguinte payload:
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

Precisa ser transformaado no seguinte:
```json
[0.0384, 0.2500, 0.0500, 0.8695, 0.3333, 0.2256, 0.0188, 0.0137, 0.1500, 0.0000, 1.0000, 0.0000, 0.2000, 0.0298]

```

Usando a seguinte tabela de conversão/normalização para transformar o payload de entrada num vetor de 14 dimensões com valores de 0 a 1 ou de -1 para casos de valores sentinela. A ordem dos valores dos vetores deve ser a seguinte.

| índice | dimensão                 | fórmula                                                                          |
|-----|--------------------------|----------------------------------------------------------------------------------|
| 0   | `amount`                 | `limitar(transaction.amount / max_amount)`                                         |
| 1   | `installments`           | `limitar(transaction.installments / max_installments)`                             |
| 2   | `amount_vs_avg`          | `limitar((transaction.amount / customer.avg_amount) / amount_vs_avg_ratio)`        |
| 3   | `hour_of_day`            | `hora(transaction.requested_at) / 23`  (0-23, UTC)                               |
| 4   | `day_of_week`            | `dia_da_semana(transaction.requested_at) / 6`    (seg=0, dom=6)                            |
| 5   | `minutes_since_last_tx`  | `limitar(minutos / max_minutes)` ou `-1` se `last_transaction: null`           |
| 6   | `km_from_last_tx`        | `limitar(last_transaction.km_from_current / max_km)` ou `-1` se `last_transaction: null` |
| 7   | `km_from_home`           | `limitar(terminal.km_from_home / max_km)`                                          |
| 8   | `tx_count_24h`           | `limitar(customer.tx_count_24h / max_tx_count_24h)`                                |
| 9   | `is_online`              | `1` se `terminal.is_online`, senão `0`                                           |
| 10  | `card_present`           | `1` se `terminal.card_present`, senão `0`                                        |
| 11  | `unknown_merchant`       | `1` se `merchant.id estiver em customer.known_merchants`, senão `0` (invertido: `1` = desconhecido) |
| 12  | `mcc_risk`               | `mcc_risk.json[merchant.mcc]` (default `0.5`)                                    |
| 13  | `merchant_avg_amount`    | `limitar(merchant.avg_amount / max_merchant_avg_amount)`                           |

Alguns valores, como `max_amount` e `max_installments`, estão definidos no arquivo [normalization.json](/resources/normalization.json) que é uma tabela de normalização:

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

### 2. Buscar nas referências os 5 registros mais próximos do seu vetor

Uma vez que o payload foi normalizado e vetorizado, você agora precisa realizar uma [busca vetorial](./BUSCA_VETORIAL.md) para encontrar os 5 registros mais semelhantes. Vamos supor que encontremos 5 registros e que 3 deles estejam rotulados como `fraud`. Como o threshold para fraude é de 0.6 – ou seja, 60% ou mais dos registros referência similares precisam estar rotulados como fraudes para que classifiquemos uma transação como fraudulenta –, a transação deve também ser marcada como fraude junto com seu score. `3 / 5 = 0,6` – 3 registros referência de 5 rotulados como fraude. A resposta http seria a seguinte para este caso:

```json
{
  "approved": false,
  "fraud_score": 0.6
}
```



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

