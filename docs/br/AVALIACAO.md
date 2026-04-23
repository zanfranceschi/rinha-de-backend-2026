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

A massa de dados do teste já vem rotulada — para cada requisição, sabe-se de antemão se a transação é fraude ou legítima. O teste compara a resposta do seu backend (`approved: true|false`) com o rótulo esperado e classifica cada requisição em uma das cinco categorias abaixo. As quatro primeiras formam a matriz de confusão clássica para classificação binária; a última cobre o caso em que o backend não chega a responder corretamente:

- **TP (True Positive)** — fraude corretamente negada
- **TN (True Negative)** — transação legítima corretamente aprovada
- **FP (False Positive)** — legítima incorretamente negada
- **FN (False Negative)** — fraude incorretamente aprovada
- **Error** — erro HTTP não-200

Essas cinco contagens, junto com a latência observada, alimentam a fórmula descrita na próxima seção.

## Exemplos de pontuação

Às vezes é mais fácil entender a pontuação observando casos concretos do que a fórmula. A tabela abaixo antecipa nove cenários, todos com N = 5000 requisições, indo do melhor caso ao pior — incluindo o ponto em que o corte de 15% passa a valer. Os detalhes de cada coluna são explicados nas próximas seções; por enquanto, basta saber que `final_score` é a pontuação final, soma de um score de latência (`p99_score`) e um score de detecção (`detection_score`).

| detecção falsa positiva | detecção falsa negativa | erro HTTP | p99   | taxa de falhas | score p99 | score detecção | score final  |
|-------------------------|-------------------------|-----------|-------|----------------|-----------|----------------|--------------|
| 0                       | 0                       | 0         | 1ms   | 0,00%          | 3000,00   | 3000,00        | **6000,00**  |
| 5                       | 5                       | 0         | 3ms   | 0,20%          | 2522,88   | 2001,27        | **4524,15**  |
| 30                      | 20                      | 0         | 10ms  | 1,00%          | 2000,00   | 1157,02        | **3157,02**  |
| 80                      | 50                      | 0         | 50ms  | 2,60%          | 1301,03   | 628,16         | **1929,19**  |
| 200                     | 150                     | 50        | 150ms | 8,00%          | 823,91    | −141,69        | **682,22**   |
| 500                     | 250                     | 0         | 200ms | 15,00%         | 698,97    | −327,12        | **371,85**   |
| 500                     | 300                     | 0         | 50ms  | 16,00%         | 1301,03   | −3000,00       | **−1698,97** |
| 0                       | 500                     | 1000      | 5ms   | 30,00%         | 2301,03   | −3000,00       | **−698,97**  |
| 0                       | 0                       | 5000      | 500ms | 100,00%        | 301,03    | −3000,00       | **−2698,97** |

Três leituras rápidas da tabela:

- O `p99_score` acompanha a latência de forma independente. Ao longo das linhas, vai de 3000 a 301 sem saltos, conforme o p99 cresce.
- O `detection_score` acompanha a qualidade de detecção até 15% de falhas. A partir daí, é substituído por −3000 — a descontinuidade aparece entre a linha de 15,00% e a de 16,00%.
- Mesmo um p99 excelente não compensa o corte: a linha com p99=5ms e 30% de falhas termina em `−698,97`, abaixo da linha com p99=10ms e taxa baixa (`3157,02`).

## Fórmula da pontuação

A pontuação final é a soma de dois componentes independentes: um para latência (p99) e outro para qualidade de detecção. Ambos usam função logarítmica — a ideia é recompensar cada ordem de grandeza de melhoria na mesma medida, em vez de diferenças absolutas. O componente de detecção tem, adicionalmente, uma regra de corte: se a taxa de falhas ultrapassar 15%, seu valor é fixado em −3000, o que anula por completo o score máximo de latência.

### Latência — `score_p99`

```
score_p99 = K · log₁₀(T_max / p99)
```

- `K = 1000`, `T_max = 1000ms`.
- Sem teto e sem piso: p99 baixo continua acumulando pontos; p99 acima de 1000ms resulta em pontuação negativa.

Na prática, cada 10× de melhoria na latência vale +1000 pontos. De 100ms para 10ms: +1000. De 10ms para 1ms: outros +1000. Diferente da fórmula anterior, não existe um alvo após o qual melhorias deixem de contar — a pontuação continua crescendo enquanto a latência diminui.

### Detecção — `score_det`

```
E         = 1·FP + 3·FN + 5·Err              (erros ponderados)
ε         = E / N                             (taxa ponderada)
falhas    = FP + FN + Err                     (contagem pura)
tx_falhas = falhas / N

Se tx_falhas > 15%:
    score_det = −3000                         ← corte ativo
Senão:
    score_det = K · log₁₀(1 / max(ε, ε_MIN)) − β · log₁₀(1 + E)
```

- `K = 1000`, `ε_MIN = 0.001`, `β = 300`.
- Pesos: `FP = 1`, `FN = 3`, `Err = 5`. HTTP 500 tem o maior peso — indisponibilidade é pior do que qualquer erro de detecção.

Fora da região de corte, a fórmula é a soma de dois termos:

- **Termo da taxa** (`K · log₁₀(1/ε)`) recompensa quem mantém poucos erros por requisição. É invariante ao tamanho do teste: 10 erros em 10.000 requisições resulta na mesma pontuação que 1 erro em 1.000.
- **Penalidade absoluta** (`−β · log₁₀(1 + E)`) subtrai uma pequena quantidade por erro real. Cada fraude que escapa representa um prejuízo concreto, e isso continua pesando mesmo quando a taxa relativa está baixa.

Quando mais de 15% das requisições falham (somando FP, FN e Err), a fórmula acima não é aplicada e `score_det` é fixado em −3000. É um piso rígido: um backend com taxa de falhas acima desse limite não deveria compensar o resultado apenas com p99 baixo.

### Score final

```
score_final = score_p99 + score_det
```

Soma direta, sem multiplicação. As duas dimensões são independentes, e qualquer uma delas pode ser negativa isoladamente.

Máximo de referência: **~6000 pontos** (3000 + 3000), com p99 próximo de 1ms e `E = 0`. O `score_det` tem teto fixo em 3000 (imposto por `ε_MIN`); o `score_p99`, não — quanto menor o p99, mais pontos, sem limite superior.

## Pesos e parâmetros — por quê

A motivação por trás de cada escolha:

- **FN vale 3, Err vale 5** (em `E`) — mantém a mesma ordem de magnitude do scoring anterior: deixar uma fraude passar é três vezes pior do que bloquear um cliente legítimo, e devolver HTTP 5xx é ainda pior do que qualquer erro de detecção.
- **Log na latência** — recompensa cada ordem de grandeza de melhoria na mesma medida. Reduzir 90ms em um backend que está em 100ms vale o mesmo que reduzir 9ms em um que está em 10ms. Diferente da fórmula anterior, **não satura**: p99 abaixo de 10ms continua gerando pontos.
- **Termo da taxa + penalidade absoluta** — a taxa é justa entre testes de tamanhos diferentes; a penalidade absoluta reforça que cada erro representa um prejuízo real. Juntos, incentivam qualidade em proporção **e** em volume.
- **Corte em 15% de falhas** — o objetivo não é aplicar uma penalidade proporcional, e sim anular o resultado. Um backend com taxa de falhas nesse patamar não deveria pontuar apenas por ter p99 baixo.

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

**O log favorece p99 muito baixo.** Reduzir a latência de 10ms para 1ms rende +1000 pontos no `p99_score`. Cada milissegundo a menos continua valendo — não há alvo de saturação.

**O corte em 15% é rígido.** Se mais de 15% das requisições falham (somando FP, FN e erros HTTP), o `detection_score` é fixado em −3000 e anula qualquer ganho obtido no p99. Ficar longe da zona de corte é mais importante do que ganhar as últimas casas decimais de acurácia.

**HTTP 500 tem impacto duplo.** Entra no `E` com peso 5 (contra 1 de um FP) e também conta na `failure_rate` (cada Err equivale a uma falha bruta, como FP ou FN). Se algo der errado no backend, **devolver uma resposta rápida qualquer** (por exemplo, `approved: true`, `fraud_score: 0.0`) evita o erro HTTP ao custo de subir FP ou FN. No regime normal, a penalidade de `−1` (FP) ou `−3` (FN) no peso logarítmico é menor do que `−5` (Err) acrescido de mais um ponto na `failure_rate`.

**A taxa de erro ponderada é N-invariante.** Não é possível "diluir" erros aumentando o volume — a taxa na mesma faixa resulta no mesmo `rate_component`. A `absolute_penalty`, por outro lado, cresce logaritmicamente com o volume real de erros; backends que falham em larga escala perdem mais pontos do que os que falham em escala pequena.

**Quando ANN vale a pena.** Brute force em 100k vetores × 14 dimensões por consulta pode ficar muito caro computacionalmente. Adotar ANN (HNSW, IVF) ou um banco vetorial pronto pode te ajudar. Mas sempre meça antes de complicar!

**Os arquivos de referência não mudam durante o teste.** Pré-processe à vontade no startup ou no build do container — quanto mais processamento você tira para fora do teste, melhor o `p99`.
