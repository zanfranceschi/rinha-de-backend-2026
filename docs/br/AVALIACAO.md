# Avaliação e pontuação

Como a sua submissão será avaliada.

## Teste de carga

O teste de carga usa o [k6](https://k6.io/) num cenário incremental simples de requisições. O script do teste está em [test](/test) junto com a massa de dados (as requisições que serão feitas). O script disponibilizado aqui serve para você rodar os seus próprios testes e pode não ser idêntico à versão final usada na avaliação oficial.

Para rodar o teste, siga as [instruções oficiais do k6](https://grafana.com/docs/k6/latest/).

As instruções para que o seu backend seja testado de fato estão [descritas aqui](/docs/br/SUBMISSAO.md) na seção **Execução do Teste**.

## O que é testado

O teste usa [payloads](/test/test-data.json) já rotulados a partir das [referências](/resources/references.json.gz). A rotulagem foi feita aplicando **k-NN com k=5 e distância euclidiana com brute force** – busca exata – sobre os vetores de 14 dimensões. Ou seja, para cada requisição existe uma resposta esperada (fraude ou legítima). Isso não obriga você a usar mesma técnica – usar brute force provavelmente terá uma performance muito ruim (*O(N * 14)*) para esse desafio.

## Métricas coletadas

A massa de dados já vem rotulada — para cada requisição, sabe-se de antemão se a transação é fraude ou legítima. O teste compara a resposta do seu backend (`approved: true|false`) com o rótulo esperado e classifica cada resposta em uma das cinco categorias abaixo. As quatro primeiras formam a matriz de confusão clássica para classificação binária; a última cobre o caso em que o backend não responde com sucesso:

- **TP (True Positive)** — fraude corretamente negada.
- **TN (True Negative)** — transação legítima corretamente aprovada.
- **FP (False Positive)** — transação legítima incorretamente negada.
- **FN (False Negative)** — fraude incorretamente aprovada.
- **Error** — erro HTTP (resposta diferente de 200).

Essas cinco contagens, junto com a latência observada, alimentam a fórmula descrita na próxima seção.

## Exemplos de pontuação

Em alguns casos, é mais fácil entender a pontuação olhando para exemplos do que para a fórmula em si. A tabela abaixo traz alguns cenários representativos, todos com N = 5000 requisições, ordenados do melhor para o pior — incluindo os dois cortes (mais de 15% de falhas e p99 acima de 2000ms) e o extremo em que os dois disparam juntos. Os detalhes de cada coluna são explicados nas seções seguintes; por ora, basta saber que `final_score` é a pontuação final, soma de um score de latência (`p99_score`) e um score de detecção (`detection_score`).

| detecção falsa positiva | detecção falsa negativa | erro HTTP | falhas (detecção + HTTP) / total de requisições | p99      | score p99 | score detecção | score final  |
|-------------------------|-------------------------|-----------|-------------------------------------------------|----------|-----------|----------------|--------------|
| 0                       | 0                       | 0         | 0,00%                                           | 1ms      | 3000,00   | 3000,00        | **6000,00**  |
| 5                       | 5                       | 0         | 0,20%                                           | 3ms      | 2522,88   | 2001,27        | **4524,15**  |
| 0                       | 0                       | 0         | 0,00%                                           | 100ms    | 1000,00   | 3000,00        | **4000,00**  |
| 30                      | 20                      | 0         | 1,00%                                           | 10ms     | 2000,00   | 1157,02        | **3157,02**  |
| 100                     | 50                      | 0         | 3,00%                                           | 300ms    | 522,88    | 581,13         | **1104,01**  |
| 500                     | 250                     | 0         | 15,00%                                          | 200ms    | 698,97    | −327,12        | **371,85**   |
| 500                     | 300                     | 0         | 16,00%                                          | 10ms     | 2000,00   | −3000,00       | **−1000,00** |
| 0                       | 0                       | 5000      | 100,00%                                         | 60000ms  | −3000,00  | −3000,00       | **−6000,00** |

Algumas leituras que a tabela deixa clara:

- O `p99_score` tem teto em 3000 (quando p99 é menor ou igual a 1ms) e piso em −3000 (quando p99 passa de 2000ms). Entre os dois limites, cresce em escala logarítmica — cada 10× mais rápido rende mais 1000 pontos.
- O `detection_score` é livre enquanto a taxa de falhas fica até 15%. Acima disso, é fixado em −3000.
- Mesmo um p99 muito bom não compensa o corte de falhas: com p99 de 10ms e 16% de falhas, o score final é −1000; já com p99 de 10ms e 1% de falhas, o score final é 3157,02.
- Quando os dois cortes disparam juntos (última linha), o `final_score` atinge o piso absoluto de −6000.

## Fórmula da pontuação

A pontuação final é a soma de dois componentes independentes: um para latência (p99) e outro para qualidade de detecção. Os dois usam função logarítmica — a ideia é recompensar cada ordem de grandeza de melhoria na mesma medida, em vez de olhar diferenças absolutas. Os dois componentes têm teto em +3000 e piso em −3000, aplicados pelas regras específicas descritas abaixo.

### Latência — `score_p99`

```
Se p99 > p99_MAX:
    score_p99 = −3000                         ← corte ativo
Senão:
    score_p99 = K · log₁₀(T_max / max(p99, p99_MIN))
```

- `K = 1000`, `T_max = 1000ms`, `p99_MIN = 1ms`, `p99_MAX = 2000ms`.
- Teto em +3000: quando `p99 ≤ 1ms`, o score satura em 3000 — melhorias abaixo disso não somam pontos.
- Piso em −3000: quando `p99 > 2000ms`, o score fica fixado em −3000.

*Obs.: As requisições http do teste têm timeout de 2001ms.*

Na prática, dentro da faixa sem corte, cada 10× de melhoria na latência vale mais 1000 pontos. De 100ms para 10ms: mais 1000. De 10ms para 1ms: outros 1000. Abaixo de 1ms, a pontuação satura em 3000.

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

- **Termo da taxa** (`K · log₁₀(1/ε)`) — recompensa quem mantém poucos erros por requisição. Não depende do tamanho do teste: 10 erros em 10.000 requisições dá a mesma pontuação que 1 erro em 1.000.
- **Penalidade absoluta** (`−β · log₁₀(1 + E)`) — subtrai uma pequena quantidade por erro real. Cada fraude que escapa representa um prejuízo concreto, e isso continua pesando mesmo quando a taxa relativa está baixa.

Quando mais de 15% das requisições falham (somando FP, FN e Err), a fórmula acima não é aplicada e `score_det` fica fixado em −3000. É um piso rígido: um backend com taxa de falhas acima desse limite não pode compensar o resultado apenas com p99 baixo.

### Score final

```
score_final = score_p99 + score_det
```

Soma direta, sem multiplicação. As duas dimensões são independentes, e qualquer uma delas pode ser negativa isoladamente.

- **Máximo: +6000 pontos** (+3000 + +3000), com p99 menor ou igual a 1ms e `E = 0`.
- **Mínimo: −6000 pontos** (−3000 − 3000), com p99 acima de 2000ms e taxa de falhas acima de 15%.

Os dois componentes têm teto em +3000 e piso em −3000, aplicados por mecanismos diferentes: no lado da latência, via `p99_MIN` e `p99_MAX`; no lado da detecção, via `ε_MIN` e corte de falhas. Assim, cada componente contribui entre −3000 e +3000, e o total fica no intervalo [−6000, +6000].

## Por que esses pesos e parâmetros

A motivação por trás de cada escolha:

- **FN vale 3 e Err vale 5** (em `E`) — mantém a mesma ordem de magnitude do scoring anterior. Deixar uma fraude passar é três vezes pior do que bloquear um cliente legítimo, e devolver HTTP 5xx é ainda pior do que qualquer erro de detecção.
- **Log na latência** — recompensa cada ordem de grandeza de melhoria na mesma medida. Reduzir 90ms num backend que está em 100ms vale o mesmo que reduzir 9ms num que está em 10ms.
- **Teto em p99 = 1ms e piso em p99 = 2000ms** — simetria com os limites da detecção. Otimizar abaixo de 1ms deixa de render pontos (retornos decrescentes nessa faixa); p99 acima de 2s é tratado como backend inviável e corta o score direto para −3000.
- **Termo da taxa + penalidade absoluta** — a taxa é justa entre testes de tamanhos diferentes; a penalidade absoluta reforça que cada erro representa um prejuízo real. Juntos, incentivam qualidade em proporção **e** em volume.
- **Corte em 15% de falhas** — o objetivo não é aplicar uma penalidade proporcional, e sim anular o resultado. Um backend com taxa de falhas nesse patamar não pode pontuar apenas por ter p99 baixo.

## Interpretando o resultado dos testes

Se você rodar o teste localmente, um arquivo `results.json` será gerado. Se o seu teste foi executado pela Engine da Rinha (via abertura de issue), o comentário com o resultado do teste vai conter o seguinte JSON:

```json
{
  "expected": { "total": 5000, "fraud_count": 1750, "fraud_rate": 35, ... },
  "p99": "5.81ms",
  "scoring": {
    "breakdown": {
      "true_positive_detections":  1735,
      "true_negative_detections":  3210,
      "false_positive_detections":   40,
      "false_negative_detections":   15,
      "http_errors":                  0
    },
    "failure_rate": "1.10%",
    "weighted_errors_E": 85,
    "error_rate_epsilon": 0.017,
    "p99_score": {
      "value": 2235.83,
      "cut_triggered": false
    },
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

- `expected` — metadados do dataset (informativo).
- `p99` — latência observada no percentil 99, em milissegundos. Alimenta o cálculo de `p99_score`.
- `breakdown` — contagens brutas de TP, TN, FP, FN e erros HTTP.
- `failure_rate` — `(FP + FN + Err) / N`. Se passar de 15%, o corte de detecção dispara.
- `weighted_errors_E` — `1·FP + 3·FN + 5·Err`. Entra no cálculo de `ε` e na penalidade absoluta.
- `error_rate_epsilon` — `E / N`. A taxa ponderada que alimenta o termo logarítmico.
- `p99_score.value` — score de latência final (depois do corte, se disparou).
- `p99_score.cut_triggered` — `true` se `p99 > 2000ms` e o score caiu para −3000.
- `detection_score.value` — score de detecção final (depois do corte, se disparou).
- `detection_score.rate_component` — só o termo `K · log₁₀(1/ε)`. Fica `null` quando o corte dispara.
- `detection_score.absolute_penalty` — só o termo `−β · log₁₀(1 + E)`. Fica `null` quando o corte dispara.
- `detection_score.cut_triggered` — `true` se `failure_rate > 15%` e o score caiu para −3000.
- `final_score` — `p99_score.value + detection_score.value`. É a pontuação final do seu backend.


## Estratégias e dicas

Algumas observações que podem ser úteis.

**O log favorece p99 baixo, até 1ms.** Reduzir a latência de 10ms para 1ms rende mais 1000 pontos no `p99_score`. Abaixo de 1ms, a pontuação satura em 3000 — otimizar além desse ponto não rende pontos adicionais.

**O corte em 15% de falhas é rígido.** Se mais de 15% das requisições falham (somando FP, FN e erros HTTP), o `detection_score` fica fixado em −3000 e anula qualquer ganho obtido no p99. Ficar longe da zona de corte tende a ser mais importante do que minimizar os últimos erros de detecção.

**O corte em p99 acima de 2000ms dificilmente dispara sozinho.** O limite de 2s existe como piso rígido para o score de latência, mas na prática é difícil chegar num p99 desse tamanho sem acumular antes erros de conexão — e esses erros já empurram a `failure_rate` acima de 15%, disparando primeiro o corte de detecção. Considere o corte de p99 como uma rede de segurança, não como algo comum de ver isolado.

**HTTP 500 tem impacto duplo.** Entra no `E` com peso 5 (contra 1 de um FP) e também conta na `failure_rate` (cada Err equivale a uma falha bruta, como FP ou FN). Se algo der errado no backend, devolver uma resposta rápida qualquer (por exemplo, `approved: true`, `fraud_score: 0.0`) evita o erro HTTP ao custo de subir FP ou FN. No regime normal, a penalidade de −1 (FP) ou −3 (FN) no peso logarítmico é menor do que −5 (Err) somado a mais um ponto na `failure_rate`.

**A taxa de erro ponderada não depende do tamanho do teste.** Não dá para "diluir" erros aumentando o volume — a taxa na mesma faixa resulta no mesmo `rate_component`. A `absolute_penalty`, por outro lado, cresce em escala logarítmica com o volume real de erros; backends que falham em larga escala perdem mais pontos do que os que falham em escala pequena.

**Quando ANN vale a pena.** Força bruta em 3 milhões de vetores com 14 dimensões por consulta pode ficar caro computacionalmente. Adotar ANN (HNSW, IVF) ou até mesmo VP Tree que é uma busca exata que não usa força bruta pode ajudar. Mas sempre meça antes de complicar.

**Os arquivos de referência não mudam durante o teste.** Você pode (e provavelmente deveria) pré-processar à vontade o arquivo de referência com os 3 milhões de vetores no startup ou no build do container — quanto mais processamento sair de dentro do runtime, melhor tende a ficar o `p99`.
