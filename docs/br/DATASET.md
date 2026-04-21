# Dataset — Arquivos de Referência

Três arquivos são fornecidos aos participantes e são necessários para decidir se as transações são ou não fraudulentas.

| Arquivo | Tamanho | Para quê |
|---|---|---|
| [`resources/references.json.gz`](/resources/references.json.gz) | ~1,6 MB (gzipado) / ~10 MB | 100.000 vetores rotulados — o "dicionário" que sua busca vetorial consulta. |
| [`resources/mcc_risk.json`](/resources/mcc_risk.json) | <1 KB | Score de risco por MCC (categoria do comerciante). |
| [`resources/normalization.json`](/resources/normalization.json) | <1 KB | Constantes para normalizar os campos do payload. |


## `references.json.gz` — vetores de referência rotulados

O dataset principal contra o qual sua busca vetorial é executada. Cada registro tem dois campos: `vector` (14 dimensões na ordem definida em [VETORIZACAO.md](./VETORIZACAO.md)) e `label` (`"fraud"` ou `"legit"`).

```json
[
  { "vector": [0.01, 0.0833, 0.05, 0.8261, 0.1667, -1, -1, 0.0432, 0.25, 0, 1, 0, 0.2, 0.0416], "label": "legit" },
  { "vector": [0.5796, 0.9167, 1.0, 0.0435, 0, 0.0056, 0.4394, 0.4598, 0.4, 1, 0, 1, 0.85, 0.0032], "label": "fraud" }
]
```

**Por que vem gzipado?** O arquivo descomprimido tem ~10 MB; comprimido cai para ~1,6 MB. Distribuímos o `.gz` por economia de tamanho.

**O valor sentinela `-1`.** Os índices `5` (`minutes_since_last_tx`) e `6` (`km_from_last_tx`) recebem `-1` quando a transação chega com `last_transaction: null` (não há transação anterior). Como `-1` está claramente fora do intervalo `0.0–1.0`, transações "sem histórico" naturalmente ficam próximas de outras "sem histórico" no espaço vetorial — o KNN agrupa as duas situações sem precisar de tratamento especial. Os vetores do dataset usam a mesma convenção, então **não filtre nem substitua** esses `-1`.

**Para inspecionar.** O arquivo oficial é grande e desconfortável de abrir. Use [`resources/example-references.json`](/resources/example-references.json) — um recorte pequeno e descomprimido com o mesmo formato.


## `mcc_risk.json` — score de risco por MCC

Mapeia o MCC (Merchant Category Code, presente em `merchant.mcc` do payload) para um valor entre `0.0` (categoria segura) e `1.0` (categoria arriscada). É consumido diretamente pelo índice `12` (`mcc_risk`) do vetor.

Conteúdo completo do arquivo:

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

**MCC não listado?** Use `0.5` como default. O payload pode trazer MCCs que não estão na tabela — isso é esperado.


## `normalization.json` — constantes de normalização

As constantes usadas nas fórmulas de [VETORIZACAO.md](./VETORIZACAO.md). Conteúdo completo:

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
| `max_installments` | Teto para `transaction.installments` (12 parcelas = `1.0`). |
| `amount_vs_avg_ratio` | Divisor extra para a razão `amount / customer.avg_amount`; `10×` a média = `1.0`. |
| `max_minutes` | Janela de tempo para `minutes_since_last_tx`; 1.440 min = 24h. |
| `max_km` | Teto de distância (km) para `km_from_home` e `km_from_last_tx`. |
| `max_tx_count_24h` | Teto para `customer.tx_count_24h`; 20 transações ou mais nas últimas 24h são limitadas a `1.0`. |
| `max_merchant_avg_amount` | Teto para o ticket médio do comerciante. |


**Importante**: Os três arquivos não mudam durante o teste/edição — pode pré-processar à vontade (descomprimir, indexar, construir estruturas de busca (ex.: HNSW), converter para outro formato, etc.).
