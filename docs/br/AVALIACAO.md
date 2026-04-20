# Avaliação e Pontuação

Como sua submissão é avaliada.

## Teste de Carga

> TODO: descrever cenário (k6, ramping-arrival-rate, stages, VUs).

## Métricas Coletadas

Para cada requisição, a resposta `approved` é comparada com o gabarito:

- **TP (True Positive)** — fraude corretamente negada
- **TN (True Negative)** — transação legítima corretamente aprovada
- **FP (False Positive)** — legítima incorretamente negada
- **FN (False Negative)** — fraude incorretamente aprovada
- **Error** — HTTP non-200

## Fórmula de Pontuação

```
raw_score    = (TP × 1) + (TN × 1) + (FP × -1) + (FN × -3) + (Error × -5)
latency_mult = TARGET_P99_MS / max(p99, TARGET_P99_MS)
final_score  = max(0, raw_score) × latency_mult
```

**TARGET_P99_MS = 10ms.**

## Pesos — por que assim

- **FN vale -3** — deixar passar uma fraude é 3× pior que bloquear um cliente legítimo (impacto financeiro real)
- **Error vale -5** — indisponibilidade é o pior dos mundos
- **Latência multiplica o score** — uma API lenta mas precisa perde para uma rápida e precisa

## Rodando o teste localmente

> TODO: comando para rodar `test/test.js` com k6, como interpretar `results.json`.

## Estratégias (dicas)

> TODO: observações sobre trade-offs acurácia × latência, quando ANN vale a pena, como o threshold de 0.6 pode ser ajustado.
