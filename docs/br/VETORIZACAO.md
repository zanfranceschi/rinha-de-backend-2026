# Vetorização e Normalização

Esta é a **especificação exata** de como transformar o payload do `POST /fraud-score` em um vetor de 14 dimensões pronto para a busca vetorial.

> Se você nunca trabalhou com vetores ou KNN, leia primeiro [BUSCA_VETORIAL.md](./BUSCA_VETORIAL.md) — ele explica o conceito de forma didática com um exemplo simplificado.


## Visão geral do fluxo

```
1. recebe a requisição:
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
    approved = score >= 0.6 → true
          ↓
5. responde com o resultado:
    {
        "approved": true,
        "fraud_score": 0.4
    }
```

> O exemplo acima é simplificado (4 dimensões) para ilustrar o fluxo. A especificação real usa **14 dimensões**, descritas abaixo.


## As 14 dimensões do vetor

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

A função `limitar(x)` restringe o valor ao intervalo `[0.0, 1.0]` (clamp).


## Constantes de normalização

Alguns valores, como `max_amount` e `max_installments`, estão definidos no arquivo [normalization.json](/resources/normalization.json):

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

Para mais detalhes sobre os arquivos de referência (incluindo `mcc_risk.json` e `references.json.gz`), veja [DATASET.md](./DATASET.md).


## Próximo passo

Veja [EXEMPLOS.md](./EXEMPLOS.md) para quatro fluxos completos — do payload bruto até a resposta — usando essas regras.
