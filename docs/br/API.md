# API

A sua API deve expor exatamente dois endpoints na porta **9999** (veja [ARQUITETURA.md](./ARQUITETURA.md)).


## `GET /ready`

Verificação de prontidão. A sua API deve responder com `HTTP 2xx` quando estiver pronta para receber requisições e ser testada.


## `POST /fraud-score`

Este é o endpoint responsável pela detecção de fraudes. O formato do payload é como o seguinte exemplo:

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

### Campos da requisição

| Campo                           | Tipo       | Descrição |
|---------------------------------|------------|-----------|
| `id`                            | string     | Identificador da transação (ex.: `tx-1329056812`) |
| `transaction.amount`            | number     | Valor da transação |
| `transaction.installments`      | integer    | Número de parcelas |
| `transaction.requested_at`      | string ISO | Timestamp UTC da requisição |
| `customer.avg_amount`           | number     | Média histórica de gasto do portador do cartão |
| `customer.tx_count_24h`         | integer    | Quantidade de transações do portador nas últimas 24h |
| `customer.known_merchants`      | string[]   | Comerciantes já utilizados pelo portador |
| `merchant.id`                   | string     | Identificador do comerciante |
| `merchant.mcc`                  | string     | MCC (Merchant Category Code), código da categoria do comerciante |
| `merchant.avg_amount`           | number     | Ticket médio do comerciante |
| `terminal.is_online`            | boolean    | Indica se a transação é online (`true`) ou presencial (`false`) |
| `terminal.card_present`         | boolean    | Indica se o cartão está presente no terminal |
| `terminal.km_from_home`         | number     | Distância, em km, do endereço do portador |
| `last_transaction`              | object \| `null` | Dados da transação anterior (pode ser `null` quando não houver transação anterior) |
| `last_transaction.timestamp`    | string ISO | Timestamp UTC da transação anterior |
| `last_transaction.km_from_current` | number  | Distância, em km, entre a transação anterior e a atual |

### Resposta

A sua API deve responder no formato deste exemplo:

```json
{
  "approved": false,
  "fraud_score": 1.0
}
```

Você pode consultar [vários exemplos de payloads aqui](/resources/example-payloads.json). O arquivo contém um array de payloads apenas para facilitar a leitura; no teste, cada requisição envia um payload individual.

---

## Como decidir `approved` e `fraud_score`

A lógica de detecção (vetorização e busca vetorial) está descrita em:

- **[REGRAS_DE_DETECCAO.md](./REGRAS_DE_DETECCAO.md)** — especificação das 14 dimensões, da normalização e exemplos completos do fluxo.

- **[BUSCA_VETORIAL.md](./BUSCA_VETORIAL.md)** — explicação didática do conceito de busca vetorial.
