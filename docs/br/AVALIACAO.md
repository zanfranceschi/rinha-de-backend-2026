# Avaliação e Pontuação

Como sua submissão será avaliada.

## Teste de Carga

O teste de carga usa o [k6](https://k6.io/) num cenário incremental super simples de requisições. O script para o teste está localizado em [test](/test) junto com sua massa de dados (requisições que serão feitas). É importante notar que o script disponibilizado aqui serve para que você execute seus próprios testes e pode não ser a versão final do teste :)

Siga as [instruções oficiais](https://grafana.com/docs/k6/latest/) para executar os testes.

As instruções para que seu backend seja de fato testado, estão [descritas aqui](/docs/br/SUBMISSAO.md) sob a seção **Execução do Teste**.

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

**Pré-requisitos:** [k6](https://grafana.com/docs/k6/latest/set-up/install-k6/) e `jq` instalados. Se você usa Nix, o [`shell.nix`](/shell.nix) já cobre. Os arquivos [`test/test-data.json`](/test/test-data.json) e [`resources/references.json.gz`](/resources/references.json.gz) já vêm no repositório — não precisa gerar nada.

**Passos:**

1. Suba seu backend de forma que ele responda na porta `9999` (geralmente `docker compose up`).
2. Aguarde o [`GET /ready`](./API.md) retornar `2xx`.
3. Da raiz do repositório, rode:

   ```bash
   ./run.sh
   ```

   O script chama `k6 run test/test.js`, gera o `test/results.json` e imprime no stdout.

**Perfil de carga.** O cenário usa `ramping-arrival-rate` (RPS-based) com 4 estágios — pico de **650 req/s**, duração total de ~60s:

| Estágio | Duração | RPS alvo |
|---|---|---|
| 1 | 10s | 1 → 10 |
| 2 | 10s | 10 → 50 |
| 3 | 20s | 50 → 350 |
| 4 | 20s | 350 → 650 |

> O script disponibilizado aqui serve para você executar seus próprios testes e pode não ser exatamente a versão final do teste oficial.

**Interpretando o `results.json`:**

```json
{
  "expected": { "total": 5000, "fraud_count": 1750, "fraud_rate": 35, ... },
  "response_times": { "min": "0.42ms", "med": "1.15ms", "p90": "2.04ms", "p99": "5.81ms", "max": "..." },
  "scoring": {
    "breakdown": {
      "true_positive_detections":  1735,
      "true_negative_detections":  3210,
      "false_positive_detections":   40,
      "false_negative_detections":   15,
      "http_errors":                  0
    },
    "detection_accuracy": "98.90%",
    "target_p99_ms": 10,
    "latency_multiplier": 1.0,
    "raw_score": 4900,
    "final_score": 4900.00
  }
}
```

- `breakdown` — contagens brutas de TP, TN, FP, FN e erros.
- `detection_accuracy` — `(TP + TN) / total_classificadas`. Apenas informativo, não entra na pontuação.
- `latency_multiplier` — `1.0` quando `p99 ≤ 10ms`; cai linearmente a partir daí (`10/p99`).
- `raw_score` — antes do multiplicador de latência.
- `final_score` — o número que importa.


## Estratégias (dicas)

Algumas observações úteis para tirar nota — mas a estratégia é sua.

**O multiplicador de latência é cruel.** `p99 = 10ms` mantém 100% do `raw_score`. `p99 = 20ms` corta pela metade. `p99 = 100ms` zera 90% do esforço. Em geral, perder um pouquinho de acurácia para ganhar muita latência costuma compensar.

**Erro custa 5×.** Nunca devolva 5xx ou estoure timeout. Se sua busca demorar demais por algum motivo, vale **devolver uma resposta qualquer** rápido (ex.: classificar como `approved: true` com `fraud_score: 0.0`) em vez de errar — `-1` (FP) ou `-3` (FN) machucam menos que `-5` (Error).

**FN vs FP é assimétrico.** Falso negativo vale `-3`, falso positivo vale `-1`. Com a distribuição do dataset (~33% fraude), isso significa que **ser mais agressivo em negar costuma compensar**: se o threshold padrão é `0.6`, valores menores (ex.: `0.5` ou `0.4`) tendem a aumentar o `raw_score` total — mas o ponto ótimo depende do quanto seu classificador erra em cada faixa.

**O threshold é seu, não nosso.** O `0.6` é só uma sugestão — só `approved` é avaliado. Você pode:
- usar threshold diferente,
- ponderar os vizinhos por distância em vez de votação simples,
- mudar `K` (não precisa ser 5),
- aplicar qualquer decisão sobre os vizinhos retornados pela busca vetorial.

**Quando ANN vale a pena.** Brute force exato em 100k vetores × 14 dimensões = 1,4M operações por consulta. Em linguagens compiladas com SIMD (C, Rust, Go com `unsafe`) isso roda em ~100µs e basta. Em runtimes mais lentos (Python puro, Node sem add-on nativo), o p99 explode rápido em 650 RPS — aí ANN (HNSW, IVF) ou um banco vetorial pronto começa a fazer sentido. **Meça antes de complicar.**

**Os arquivos não mudam durante o teste.** Pré-processe à vontade no startup ou no build do container — quanto mais trabalho você empurra para fora do hot path, melhor o `p99`.
