# Dataset — Arquivos de Referência

Três arquivos são fornecidos aos participantes e devem ser incluídos no container da submissão.

## `resources/references.json.gz` — vetores de referência rotulados

> TODO: descrição do formato, número de registros, exemplo.

```json
[
  { "vector": [/* 14 dimensões */], "label": "fraud" },
  { "vector": [/* 14 dimensões */], "label": "legit" }
]
```

## `resources/mcc_risk.json` — scores de risco por MCC

> TODO: descrever o mapeamento, o default (0.5) e como usar.

## `resources/normalization.json` — constantes de normalização

> TODO: listar todas as constantes e o que cada uma representa.

---

## As 14 Dimensões do Vetor

> TODO: mover aqui a tabela completa de dimensões (origem, fórmula, significado de 0.0 e 1.0).

## O valor sentinela `-1`

> TODO: explicar quando aparece, por que existe, e como o KNN lida naturalmente com ele.
