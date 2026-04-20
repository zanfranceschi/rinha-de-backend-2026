# Requisitos

Esta seção descreve os requisitos funcionais que seu backend deve implementar. Vamos começar com os endpoints que sua API precisa expor – são apenas dois.


## `GET /ready`

Health check. Deve retornar `HTTP 2xx` quando a API estiver pronta para receber requisições e ser testada. Muito simples. 


## `POST /fraud-score`
Esse é o endpoint para detecção de fraudes; é onde você tem que caprichar. O formato do payload é como o seguinte exemplo:
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

*Você pode encontrar [vários exemplos da payloads aqui](/resources/example-payloads.json). Note que o arquivo contém um array de paylioads, mas os payloads no teste são individuais.*

---

# A Lógica para Detecção de Fraude

Vamos usar um exemplo simplificado para explicar a lógica de detecção de fraude. As dimensões corretas e a ordem dos valores no vetor são detalhadas ao final da explicação.

A lógica do processamento é a seguinte:
1. Transformar a transação do corpo da requisção que está em formato JSON para um vetor.
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

*Você pode encontrar [vários exemplos de referências aqui](/resources/example-references.json). O arquivo [oficial de referências](/resources/references.json.gz) é muito grande, está comprimido e pode ser difícil dar uma olhada nele para entendê-lo.*

#### Detalhes para vetorização e normalização

As transações ([exemplos aqui](/resources/example-payloads.json)) precisam ser transformadas em vetores de 14 dimensões obedecendo a seguinte ordem e regras de normalização.


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

#### Exemplos Práticos

Os três exemplos abaixo ilustram fluxos completos. A busca pelos 5 vizinhos mais próximos entre as referências usa **distância euclidiana** sobre os vetores de 14 dimensões, mas você pode usar outros algoritmos de métrica de distância.

##### Exemplo 1 — Transação legítima (score: 0.0)

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

##### Exemplo 2 — Transação fraudulenta (score: 1.0)

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

##### Exemplo 3 — Transação limítrofe (score: 0.4)

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

