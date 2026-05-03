# Dataset — arquivos de referência

Nessa edição, você precisa de informações que estão em arquivos distribuídos nesse repositório.

| Arquivo | Tamanho | Para quê |
|---|---|---|
| [`resources/references.json.gz`](/resources/references.json.gz) | ~16 MB (gzipado) / ~284 MB | 3.000.000 vetores rotulados que sua busca vetorial consulta. |
| [`resources/mcc_risk.json`](/resources/mcc_risk.json) | <1 KB | Score de risco por MCC (categoria do comerciante). |
| [`resources/normalization.json`](/resources/normalization.json) | <1 KB | Constantes para normalizar os campos do payload. |


## `references.json.gz` — vetores de referência rotulados

Esse é o dataset principal contra o qual sua busca vetorial é executada. Cada registro tem dois campos: `vector` (14 dimensões, na ordem definida em [REGRAS_DE_DETECCAO.md](./REGRAS_DE_DETECCAO.md)) e `label` (`"fraud"` ou `"legit"`).

```json
[
  { "vector": [0.01, 0.0833, 0.05, 0.8261, 0.1667, -1, -1, 0.0432, 0.25, 0, 1, 0, 0.2, 0.0416], "label": "legit" },
  { "vector": [0.5796, 0.9167, 1.0, 0.0435, 0, 0.0056, 0.4394, 0.4598, 0.4, 1, 0, 1, 0.85, 0.0032], "label": "fraud" }
]
```

**Por que vem gzipado.** Descomprimido, o arquivo tem ~284 MB; comprimido, ~16 MB. A distribuição é feita em `.gz` para economizar tamanho.

**O valor sentinela `-1`.** Os índices `5` (`minutes_since_last_tx`) e `6` (`km_from_last_tx`) recebem `-1` quando a transação chega com `last_transaction: null` (ou seja, não existe transação anterior). Como `-1` está fora do intervalo `0.0–1.0`, transações sem histórico ficam naturalmente próximas de outras sem histórico no espaço vetorial — o KNN (k-vizinhos mais próximos) agrupa as duas situações sem precisar de tratamento especial. Os vetores do dataset seguem a mesma convenção, portanto **não filtre nem substitua** esses `-1`.

**Para inspecionar o formato.** O arquivo oficial é grande. Você pode usar [`resources/example-references.json`](/resources/example-references.json) — um recorte pequeno e já descomprimido, com o mesmo formato.


## `mcc_risk.json` — score de risco por MCC

Esse arquivo mapeia o MCC (Merchant Category Code, presente em `merchant.mcc` do payload) para um valor entre `0.0` (categoria segura) e `1.0` (categoria arriscada). Ele alimenta diretamente o índice `12` (`mcc_risk`) do vetor.

Conteúdo completo:

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

**Quando o MCC não está na tabela.** Use `0.5` como valor padrão. O payload pode trazer MCCs que não aparecem na tabela — isso é esperado.


## `normalization.json` — constantes de normalização

Essas são as constantes usadas nas fórmulas de [REGRAS_DE_DETECCAO.md](./REGRAS_DE_DETECCAO.md). Conteúdo completo:

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

| Constante | Significado |
|---|---|
| `max_amount` | Teto para `transaction.amount`; valores acima de 10.000 são limitados a `1.0`. |
| `max_installments` | Teto para `transaction.installments` (12 parcelas equivalem a `1.0`). |
| `amount_vs_avg_ratio` | Divisor da razão `amount / customer.avg_amount`; `10×` a média equivale a `1.0`. |
| `max_minutes` | Janela de tempo para `minutes_since_last_tx`; 1.440 min correspondem a 24h. |
| `max_km` | Teto de distância (km) para `km_from_home` e `km_from_last_tx`. |
| `max_tx_count_24h` | Teto para `customer.tx_count_24h`; 20 transações ou mais nas últimas 24h são limitadas a `1.0`. |
| `max_merchant_avg_amount` | Teto para o ticket médio do comerciante. |


**Importante.** Os três arquivos não mudam durante o teste, então você pode pré-processá-los à vontade — descomprimir, indexar, converter para outro formato, etc.
