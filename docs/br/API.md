# API

Sua API precisa expor exatamente dois endpoints na porta **9999** (veja [ARQUITETURA.md](./ARQUITETURA.md)).


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

### Resposta

A resposta deve ser como este exemplo:

```json
{
  "approved": false,
  "fraud_score": 1.0
}
```

*Você pode encontrar [vários exemplos de payloads aqui](/resources/example-payloads.json). Note que o arquivo contém um array de payloads, mas os payloads no teste são individuais.*

---

## Como decidir `approved` e `fraud_score`

A lógica de detecção (vetorização + busca vetorial + KNN) está descrita em:

- **[BUSCA_VETORIAL.md](./BUSCA_VETORIAL.md)** — explicação didática do conceito.
- **[VETORIZACAO.md](./VETORIZACAO.md)** — especificação exata das 14 dimensões e normalização.
- **[EXEMPLOS.md](./EXEMPLOS.md)** — quatro fluxos completos do payload à resposta.
