# Regras de detecção de fraude

Este documento define como a sua API deve transformar uma transação em um vetor de detecção de fraude. Ele cobre a vetorização (as 14 dimensões) e as regras de normalização. A busca vetorial usa esse vetor para encontrar, no dataset de referência, as 5 transações mais parecidas com a que acabou de chegar e, a partir daí, decidir se a nova transação é fraudulenta.

Se você ainda não conhece o conceito de busca vetorial, vale a pena começar por [BUSCA_VETORIAL.md](./BUSCA_VETORIAL.md) — lá o assunto é apresentado de forma didática, com um exemplo bem simplificado.


## Visão geral do fluxo

O fluxo abaixo mostra, com um exemplo real da Rinha de Backend de transação legítima, o passo a passo que a sua API deve fazer para decidir sobre uma transação. Neste caso, um cliente faz uma compra de baixo valor em um comerciante que ele já conhece, perto de casa.

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
2. vetoriza e normaliza (14 dimensões):
    [0.0041, 0.1667, 0.05, 0.7826, 0.3333, -1, -1, 0.0292, 0.15, 0, 1, 0, 0.15, 0.006]
          ↓
3. busca os 5 vizinhos mais próximos (ex.: distância euclidiana):
    dist=0.0340  legit
    dist=0.0488  legit
    dist=0.0509  legit
    dist=0.0591  legit
    dist=0.0592  legit
          ↓
4. calcula o score (threshold 0.6):
    score = 0 fraudes / 5 = 0.0
    approved = score < 0.6 → true
          ↓
5. resposta:
    {
      "approved": true,
      "fraud_score": 0.0
    }
```

Repare nos `-1` nas posições 5 e 6: como `last_transaction` veio como `null`, não há "minutos desde a última transação" nem "km desde a última transação" para normalizar.

## As 14 dimensões do vetor

As transações ([exemplos realistas aqui](/resources/example-payloads.json)) precisam ser transformadas em vetores de 14 posições, seguindo a ordem e as regras de normalização abaixo.

| índice | dimensão                 | fórmula                                                                          |
|-----|--------------------------|----------------------------------------------------------------------------------|
| 0   | `amount`                 | `limitar(transaction.amount / max_amount)`                                         |
| 1   | `installments`           | `limitar(transaction.installments / max_installments)`                             |
| 2   | `amount_vs_avg`          | `limitar((transaction.amount / customer.avg_amount) / amount_vs_avg_ratio)`        |
| 3   | `hour_of_day`            | `hora(transaction.requested_at) / 23`  (0-23, UTC)                               |
| 4   | `day_of_week`            | `dia_da_semana(transaction.requested_at) / 6`    (seg=0, dom=6)                  |
| 5   | `minutes_since_last_tx`  | `limitar(minutos / max_minutes)` ou `-1` se `last_transaction: null`             |
| 6   | `km_from_last_tx`        | `limitar(last_transaction.km_from_current / max_km)` ou `-1` se `last_transaction: null` |
| 7   | `km_from_home`           | `limitar(terminal.km_from_home / max_km)`                                          |
| 8   | `tx_count_24h`           | `limitar(customer.tx_count_24h / max_tx_count_24h)`                                |
| 9   | `is_online`              | `1` se `terminal.is_online`, senão `0`                                           |
| 10  | `card_present`           | `1` se `terminal.card_present`, senão `0`                                        |
| 11  | `unknown_merchant`       | `1` se `merchant.id` não estiver em `customer.known_merchants`, senão `0` (invertido: `1` = desconhecido) |
| 12  | `mcc_risk`               | `mcc_risk.json[merchant.mcc]` (valor padrão `0.5`)                               |
| 13  | `merchant_avg_amount`    | `limitar(merchant.avg_amount / max_merchant_avg_amount)`                           |

A função `limitar(x)` mantém o valor dentro do intervalo `[0.0, 1.0]` — é o que se costuma chamar de *clamp*: tudo que fica abaixo de `0.0` vira `0.0`, e tudo que passa de `1.0` vira `1.0`.

### O caso especial do `last_transaction: null`

Os índices 5 e 6 dependem da transação anterior do cliente. Quando a transação atual é a primeira do cliente (ou seja, `last_transaction` vem como `null` no payload), não existe valor a normalizar. Nesses casos, a sua API deve usar o valor sentinela `-1` nessas duas posições. Esse `-1` é o único caso em que o vetor pode conter um valor fora do intervalo `[0.0, 1.0]`, e serve justamente para distinguir "ausência de dado" de um valor normalizado próximo de zero.


## Constantes de normalização

Alguns valores que aparecem nas fórmulas, como `max_amount` e `max_installments`, estão definidos no arquivo [normalization.json](/resources/normalization.json):

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


## Como a decisão é tomada

Depois que o vetor está pronto, a sua API deve:

1. Buscar, no dataset de referência, os 5 vetores mais próximos do vetor da transação que acabou de chegar.
2. Calcular `fraud_score` como a fração de fraudes entre essas 5 referências — ou seja, `número_de_fraudes / 5`.
3. Responder `approved = fraud_score < 0.6`. O threshold de `0.6` é fixo.

Para medir a proximidade dos vetores, os exemplos deste documento usam **distância euclidiana** com *brute force* sobre as 14 dimensões. Note que você é livre pra escolher qualquer algoritmo/técnica de busca vetorial.

> **Importante!** Não é permitido usar os payloads do teste como referência ou para fazer lookup de fraudes! Os testes finais vão usar outros payloads, e fazer isso nas prévias distroce o resultado e desanima outros participantes.


## Exemplo de transação fraudulenta

Para contrastar com o caso legítimo da visão geral, veja como fica uma transação fraudulenta: valor alto, longe de casa, em um comerciante desconhecido, sem histórico de transação anterior. Para o formato completo do payload, veja [API.md](./API.md).

```
1. recebe a requisição:
    {
      "id": "tx-3330991687",
      "transaction":      { "amount": 9505.97, "installments": 10, "requested_at": "2026-03-14T05:15:12Z" },
      "customer":         { "avg_amount": 81.28, "tx_count_24h": 20, "known_merchants": ["MERC-008", "MERC-007", "MERC-005"] },
      "merchant":         { "id": "MERC-068", "mcc": "7802", "avg_amount": 54.86 },
      "terminal":         { "is_online": false, "card_present": true, "km_from_home": 952.27 },
      "last_transaction": null
    }
          ↓
2. vetoriza e normaliza (14 dimensões — note os `-1` nos índices 5 e 6 por conta do `last_transaction: null`):
    [0.9506, 0.8333, 1.0, 0.2174, 0.8333, -1, -1, 0.9523, 1.0, 0, 1, 1, 0.75, 0.0055]
          ↓
3. busca os 5 vizinhos mais próximos:
    dist=0.2315  fraud
    dist=0.2384  fraud
    dist=0.2552  fraud
    dist=0.2667  fraud
    dist=0.2785  fraud
          ↓
4. calcula o score (threshold 0.6):
    score = 5 fraudes / 5 = 1.0
    approved = score < 0.6 → false
          ↓
5. resposta:
    {
      "approved": false,
      "fraud_score": 1.0
    }
```
