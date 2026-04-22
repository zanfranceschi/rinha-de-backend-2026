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

A pontuação final é a soma de duas componentes logarítmicas independentes — uma por latência (p99), outra por qualidade de detecção.

### Componente de latência (score_p99)

```
score_p99 = K · log₁₀(T_max / p99)
```

- `K = 1000`, `T_max = 1000ms`.
- Sem teto nem piso: p99 muito baixo dá bônus crescente, p99 > 1000ms fica negativo.

### Componente de detecção (score_det)

```
E         = 1·FP + 3·FN + 5·Err              (erros ponderados)
ε         = E / N                             (taxa ponderada)
falhas    = FP + FN + Err                     (contagem pura de falhas)
tx_falhas = falhas / N

Se tx_falhas > 15%:
    score_det = −3000
Senão:
    score_det = K · log₁₀(1 / max(ε, ε_MIN)) − β · log₁₀(1 + E)
```

- `K = 1000`, `ε_MIN = 0.001`, `β = 300`.
- Pesos de erro: `FP = 1`, `FN = 3`, `Err = 5` (HTTP 500 é o pior).
- **Regra de corte:** se mais de 15% das requisições falham (contando FP + FN + Err), o score de detecção vai direto para `−3000`, zerando o score máximo de p99.

### Score final

```
score_final = score_p99 + score_det
```

Aditivo simples. Cada componente pode ser negativo independentemente. O máximo teórico é ~6000 pontos (3000 cada), alcançado com `p99 → 0` e `E = 0`.

## Pesos e parâmetros — por quê

- **FN vale -3, Err vale -5** (dentro do cálculo de `E`) — mesma ordem que antes: deixar passar uma fraude é 3× pior que bloquear um cliente legítimo; devolver HTTP 5xx é 5× pior.
- **Log no p99** — recompensa cada ordem de grandeza de melhoria igualmente. Diferença entre 100ms e 10ms vale o mesmo que entre 10ms e 1ms. Não satura: sub-10ms continua ganhando pontos.
- **Dois termos no score_det** — a taxa (`K · log₁₀(1/ε)`) é N-invariante (mesma taxa de erro dá mesma pontuação pra qualquer tamanho de teste). A penalidade absoluta (`−β · log₁₀(1+E)`) punie volume real de erros (porque cada fraude não detectada é um prejuízo de verdade), mas cresce log para não explodir com N grande.
- **Corte em 15% de falhas** — acima disso, score_det = −3000, garantindo que um backend quebrado não passa só com p99 bom.

## Interpretando o resultado dos testes

Se você rodar o teste localmente, um arquivo `results.json` será gerado. Se seu teste foi executado pela Engine da Rinha (via abertura de issue), o comentário com o resultado do teste conterá o seguinte JSON:

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
    "failure_rate": "1.10%",
    "weighted_errors_E": 85,
    "error_rate_epsilon": 0.017,
    "p99_score": 2236.57,
    "detection_score": {
      "value": 1193.06,
      "rate_component": 1769.55,
      "absolute_penalty": -576.49,
      "cut_triggered": false
    },
    "final_score": 3429.63
  }
}
```

- `breakdown` — contagens brutas de TP, TN, FP, FN e HTTP errors.
- `detection_accuracy` — `(TP + TN) / (TP + TN + FP + FN)`. Informativo, não entra no score.
- `failure_rate` — `(FP + FN + Err) / N`. Se passar de 15%, o corte dispara.
- `weighted_errors_E` — `1·FP + 3·FN + 5·Err`. Entra no cálculo de `ε` e na penalidade absoluta.
- `error_rate_epsilon` — `E / N`. A taxa ponderada que alimenta o termo log.
- `p99_score` — `K · log₁₀(T_max / p99)`.
- `detection_score.value` — o score_det final (depois do corte se disparou).
- `detection_score.rate_component` — só o termo `K · log₁₀(1/ε)`. É `null` quando o corte dispara.
- `detection_score.absolute_penalty` — só o termo `−β · log₁₀(1 + E)`. É `null` quando o corte dispara.
- `detection_score.cut_triggered` — `true` se `failure_rate > 15%` e o score caiu para −3000.
- `final_score` — `p99_score + detection_score.value`. O número que importa.


## Estratégias (dicas)

Algumas observações que podem ser úteis.

**Log dá bônus exponencial para p99 baixo.** Cair de 10ms para 1ms vale 1000 pontos extras no `p99_score`. Vale a pena caçar cada millissegundo.

**O corte em 15% é duro.** Se mais de 15% das requisições falham (somando FP, FN e HTTP errors), o `detection_score` vai direto para −3000 e come todo o ganho de p99. Evitar a zona de corte é mais importante que afinar acurácia nas últimas casas.

**HTTP 500 contam pesado em dois lugares.** No `E` (peso 5 vs 1 de FP) e na `failure_rate` (cada Err conta como 1 falha bruta, igual a FP ou FN). Se der algum problema no backend, **devolver qualquer resposta rápida** (ex.: `approved: true`, `fraud_score: 0.0`) reduz erro HTTP, embora suba FP ou FN. Matematicamente, no regime normal, `-1` (FP) ou `-3` (FN) no peso do log ainda dói menos que `-5` (Err) + um ponto a mais na `failure_rate`.

**A taxa de erro ponderada é N-invariante.** Não dá pra "esconder" erros aumentando o volume — se a taxa fica na mesma faixa, o `rate_component` é o mesmo. Mas a `absolute_penalty` cresce log com o volume real de erros, então backends com falhas em larga escala perdem mais pontos que em escala pequena.

**Quando ANN vale a pena.** Brute force em 100k vetores × 14 dimensões por consulta pode ficar muito caro computacionalmente. Adotar ANN (HNSW, IVF) ou um banco vetorial pronto pode te ajudar. Mas sempre meça antes de complicar!

**Os arquivos de referência não mudam durante o teste.** Pré-processe à vontade no startup ou no build do container — quanto mais processamento você tira para fora do teste, melhor o `p99`.
