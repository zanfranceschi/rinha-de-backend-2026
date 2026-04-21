# Avaliação e pontuação

Como sua submissão será avaliada.

## Teste de carga

O teste de carga usa o [k6](https://k6.io/) num cenário incremental super simples de requisições. O script para o teste está localizado em [test](/test) junto com sua massa de dados (requisições que serão feitas). É importante notar que o script disponibilizado aqui serve para que você execute seus próprios testes e pode não ser a versão final do teste :)

Siga as [instruções oficiais](https://grafana.com/docs/k6/latest/) para executar os testes.

As instruções para que seu backend seja de fato testado, estão [descritas aqui](/docs/br/SUBMISSAO.md) sob a seção **Execução do Teste**.

> O script disponibilizado aqui serve para você executar seus próprios testes e pode não ser exatamente a versão final do teste oficial para um fator surpresa adicional.

## O que é testado

O teste usa [payloads existentes](/test/test-data.json) previamente rotulados com base nas [referências](/resources/references.json.gz). Essa rotulagem prévia foi feita aplicando **k-NN com k=5 e distância euclidiana** sobre os vetores de 14 dimensões. Isso significa que para cada requisição existe uma resposta correta esperada sobre a transação ser fraude ou legítima. Mas isso não te obriga a usar KNN e distância euclidiana para a busca vetorial, vocẽ pode usar outras métricas de distância (geralmente, ao preço da perda de um pouco de precisão nas detecções).

## Métricas coletadas

Para cada requisição, a resposta `approved: true|false` é comparada e pontuada com o seguinte:

- **TP (True Positive)** — fraude corretamente negada (1 ponto)
- **TN (True Negative)** — transação legítima corretamente aprovada (1 ponto)
- **FP (False Positive)** — legítima incorretamente negada (-1 ponto)
- **FN (False Negative)** — fraude incorretamente aprovada (-3 pontos)
- **Error** — erro HTTP (-5 pontos)

## Fórmula da pontuação

```
precisao               = (TP × 1) + (TN × 1) + (FP × -1) + (FN × -3) + (Error × -5)
multiplicador_latencia = TARGET_P99_MS / max(p99, TARGET_P99_MS)
pontuacao_final        = max(0, precisao) × multiplicador_latencia
```
**TARGET_P99_MS = 10ms.**

## Pesos — por que assim

- **FN vale -3** — deixar passar uma fraude é 3× pior que bloquear um cliente legítimo (impacto financeiro real)
- **Error vale -5** — indisponibilidade é o pior dos mundos
- **Latência multiplica o score** — uma API lenta mas precisa perde para uma rápida e precisa

## Interpretando o resultado dos testes

Se você rodar o teste localmente, um arquivo `results.json` será gerado. Se seu teste foi executado pela Engine da Einha (via abertura de issue), o comentário com o resultado do teste conterá o seguinte JSON:

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

- `breakdown` — contagens de TP, TN, FP, FN e erros http.
- `detection_accuracy` — `(TP + TN) / total_classificadas`. Apenas informativo, não entra na pontuação.
- `latency_multiplier` — `1.0` quando `p99 ≤ 10ms`; cai linearmente a partir daí (`10/p99`).
- `raw_score` — pontos para precisão, antes do multiplicador de latência.
- `final_score` — o número que importa, a pontuação final.


## Estratégias (dicas)

Algumas observações que podem ser úteis.

**O multiplicador de latência é cruel.** `p99 <= 10ms` mantém 100% do `raw_score`. `p99 = 20ms` corta pela metade. `p99 = 100ms` acaba com 90% do esforço. Em geral, perder um pouquinho de precisão para melhorar a latência pode compensar.

**Erros HTTP custam 5×.** Tente nunca retornar HTTP 5xx ou estourar timeout. Se sua busca demorar demais por algum motivo, vale **devolver uma resposta qualquer** rápida (ex.: classificar como `approved: true` com `fraud_score: 0.0`) em vez de errar — `-1` (FP) ou `-3` (FN) dói menos que `-5` (Error).

**Quando ANN vale a pena.** Brute force em 100k vetores × 14 dimensões por consulta pode ficar muito caro computacionalmente. Adotar ANN (HNSW, IVF) ou um banco vetorial pronto pode te ajudar. Mas sempre meça antes de complicar!

**Os arquivos de referência não mudam durante o teste.** Pré-processe à vontade no startup ou no build do container — quanto mais processamento você tira para fora do teste, melhor o `p99`.
