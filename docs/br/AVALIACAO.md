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

A massa de dados do teste já vem rotulada — pra cada requisição a gente sabe de antemão se é fraude ou não. Comparando a resposta do seu backend (`approved: true|false`) com a esperada, cada requisição cai num de cinco baldes. Os quatro primeiros são a matriz de confusão clássica de classificação binária; o quinto é pra quando o backend nem conseguiu responder direito:

- **TP (True Positive)** — fraude corretamente negada
- **TN (True Negative)** — transação legítima corretamente aprovada
- **FP (False Positive)** — legítima incorretamente negada
- **FN (False Negative)** — fraude incorretamente aprovada
- **Error** — erro HTTP não-200

Essas cinco contagens, junto com a latência observada, alimentam a fórmula da próxima seção.

## Fórmula da pontuação

A pontuação final junta dois componentes independentes: um olhando pra latência (p99), outro pra qualidade de detecção. Os dois usam log (pra premiar ordens de grandeza em vez de valores absolutos), mas o de detecção ainda tem um freio de mão — um corte duro quando a taxa de falhas passa de 15%.

### Latência — `score_p99`

```
score_p99 = K · log₁₀(T_max / p99)
```

- `K = 1000`, `T_max = 1000ms`.
- Sem teto e sem piso: p99 muito baixo continua ganhando pontos, p99 acima de 1000ms já vai pro vermelho.

Na prática, cada 10× mais rápido vale +1000 pontos. 100ms → 10ms rende +1000. 10ms → 1ms rende mais +1000. E não tem "cheguei no alvo, parou de valer" — continuar cortando latência continua valendo ponto.

### Detecção — `score_det`

```
E         = 1·FP + 3·FN + 5·Err              (erros ponderados)
ε         = E / N                             (taxa ponderada)
falhas    = FP + FN + Err                     (contagem pura)
tx_falhas = falhas / N

Se tx_falhas > 15%:
    score_det = −3000                         ← corte, já era
Senão:
    score_det = K · log₁₀(1 / max(ε, ε_MIN)) − β · log₁₀(1 + E)
```

- `K = 1000`, `ε_MIN = 0.001`, `β = 300`.
- Pesos: `FP = 1`, `FN = 3`, `Err = 5`. HTTP 500 é o pior cenário — indisponibilidade dói mais que qualquer erro de detecção.

A fórmula tem dois termos somados:

- **Termo da taxa** (`K · log₁₀(1/ε)`) premia quem tem pouco erro por requisição. É invariante ao tamanho do teste: 10 erros em 10.000 requisições vale o mesmo que 1 erro em 1.000.
- **Penalidade absoluta** (`−β · log₁₀(1 + E)`) desce um pouquinho a cada erro real. Porque no fim das contas cada fraude que escapa é um prejuízo de verdade — isso importa mesmo quando a taxa relativa está boa.

E se mais de 15% das requisições falharem? Aí a fórmula para de rodar e `score_det` vai direto pra −3000. É o freio de mão: backend muito quebrado não compensa ser rápido.

### Score final

```
score_final = score_p99 + score_det
```

Soma direta, nada de multiplicar. As duas dimensões são independentes e qualquer uma delas pode puxar pro negativo se as coisas estiverem feias.

Máximo de referência: **~6000 pontos** (3000 + 3000), com p99 perto de 1ms e `E = 0`. O `score_det` tem teto fixo em 3000 (via `ε_MIN`), mas o `score_p99` não — quanto mais rápido, mais pontos, sem limite.

## Pesos e parâmetros — por quê

Um passeio rápido pela motivação das constantes:

- **FN vale 3, Err vale 5** (em `E`) — mesma lógica do scoring anterior: deixar fraude passar é 3× pior que barrar cliente legítimo, e devolver HTTP 5xx é pior ainda.
- **Log no p99** — dá o mesmo peso pra cada ordem de grandeza de melhoria. Tirar 90ms de quem está em 100ms vale o mesmo que tirar 9ms de quem está em 10ms. E, diferente da fórmula antiga, **não satura** — sub-10ms continua rendendo pontos.
- **Termo da taxa + penalidade absoluta** — a taxa é justa entre testes de tamanhos diferentes; a penalidade absoluta lembra que cada erro é um prejuízo real. Juntos, incentivam ser bom em proporção **e** em volume.
- **Corte em 15% de falhas** — a ideia aqui não é penalizar um pouquinho, é zerar o esforço. Backend muito ruim não deveria passar só por ter p99 bom.

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
    "p99_score": 2235.83,
    "detection_score": {
      "value": 1189.20,
      "rate_component": 1769.55,
      "absolute_penalty": -580.35,
      "cut_triggered": false
    },
    "final_score": 3425.03
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
