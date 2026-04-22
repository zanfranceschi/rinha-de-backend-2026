# Pontuação logarítmica dual — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Trocar o scoring atual (multiplicador linear saturado em p99=10ms + soma ponderada linear) pela fórmula logarítmica dual definida em [docs/superpowers/specs/2026-04-22-logarithmic-scoring-design.md](../specs/2026-04-22-logarithmic-scoring-design.md).

**Architecture:** Mudança localizada em `test/test.js` (função `handleSummary`) + atualização das docs correspondentes. Sem módulo separado, sem unit tests — é código de teste, validação é via smoke run do k6.

**Tech Stack:** k6 (load test), ES modules, Markdown (docs). Ferramentas disponíveis via `nix-shell` (`shell.nix` já lista `k6`).

**Spec de referência:** [docs/superpowers/specs/2026-04-22-logarithmic-scoring-design.md](../specs/2026-04-22-logarithmic-scoring-design.md).

---

## File Structure

| Arquivo | Ação | Responsabilidade |
|---------|------|------------------|
| `test/test.js` | Modificar (linhas ~79–132, bloco `handleSummary`) | Cálculo de scoring novo + novo shape do `results.json` |
| `docs/br/AVALIACAO.md` | Reescrever seções de fórmula, pesos, interpretação, estratégias | Documentação BR do novo scoring |
| `docs/en/EVALUATION.md` | Reescrever mesmas seções (espelho) | Documentação EN do novo scoring |
| `SCORING.md` | Deletar | Era rascunho, AVALIACAO.md/EVALUATION.md viram fonte única |

---

## Task 1: Atualizar `test/test.js` com novo scoring

**Files:**
- Modify: `test/test.js` (linhas 79–132, função `handleSummary`)

### Contexto

O bloco atual de `handleSummary` calcula:
```
rawScore = (tp*1) + (tn*1) + (fp*-1) + (fn*-3) + (errs*-5)
latencyMult = TARGET_P99_MS / max(p99, TARGET_P99_MS)
finalScore = max(0, rawScore) * latencyMult
```

Precisa ser substituído pelo scoring logarítmico dual.

### Steps

- [ ] **Step 1: Ler o arquivo atual** para confirmar que as linhas não mudaram:

Run: `sed -n '79,132p' test/test.js`

Espera-se ver o bloco `handleSummary` com o cálculo atual de `rawScore`, `latencyMult`, `finalScore`.

- [ ] **Step 2: Substituir o bloco `handleSummary` inteiro pelo novo.**

Substituir da linha 79 (`export function handleSummary(data) {`) até a linha 132 (fechamento `}`) pelo código abaixo:

```js
export function handleSummary(data) {
    // Constantes do scoring (ver docs/superpowers/specs/2026-04-22-logarithmic-scoring-design.md)
    const K = 1000;
    const T_MAX_MS = 1000;
    const EPSILON_MIN = 0.001;
    const BETA = 300;
    const TX_CORTE = 0.15;
    const SCORE_DET_CORTE = -3000;

    const httpDuration = data.metrics.http_req_duration.values;
    const p99 = httpDuration['p(99)'];

    const tp = data.metrics.tp_count ? data.metrics.tp_count.values.count : 0;
    const tn = data.metrics.tn_count ? data.metrics.tn_count.values.count : 0;
    const fp = data.metrics.fp_count ? data.metrics.fp_count.values.count : 0;
    const fn = data.metrics.fn_count ? data.metrics.fn_count.values.count : 0;
    const errs = data.metrics.error_count ? data.metrics.error_count.values.count : 0;

    const N = tp + tn + fp + fn + errs;
    const classified = tp + tn + fp + fn;
    const accuracy = classified > 0 ? (tp + tn) / classified : 0;

    // Erros ponderados (para a fórmula log) e contagem pura (para o corte)
    const E = (fp * 1) + (fn * 3) + (errs * 5);
    const failures = fp + fn + errs;
    const epsilon = N > 0 ? E / N : 0;
    const failureRate = N > 0 ? failures / N : 0;

    // Score P99 (log, sem piso)
    const p99Score = p99 > 0 ? K * Math.log10(T_MAX_MS / p99) : 0;

    // Score detecção (log com penalidade absoluta, ou corte em -3000 se falhas > 15%)
    let detScore;
    let rateComponent = 0;
    let absolutePenalty = 0;
    let cutTriggered = false;
    if (failureRate > TX_CORTE) {
        detScore = SCORE_DET_CORTE;
        cutTriggered = true;
    } else {
        rateComponent = K * Math.log10(1 / Math.max(epsilon, EPSILON_MIN));
        absolutePenalty = -BETA * Math.log10(1 + E);
        detScore = rateComponent + absolutePenalty;
    }

    const finalScore = p99Score + detScore;

    const result = {
        expected: expectedStats,
        response_times: {
            min: httpDuration.min.toFixed(2) + 'ms',
            max: httpDuration.max.toFixed(2) + 'ms',
            med: httpDuration['med'].toFixed(2) + 'ms',
            p90: httpDuration['p(90)'].toFixed(2) + 'ms',
            p99: httpDuration['p(99)'].toFixed(2) + 'ms',
        },
        scoring: {
            breakdown: {
                false_positive_detections: fp,
                false_negative_detections: fn,
                true_positive_detections: tp,
                true_negative_detections: tn,
                http_errors: errs,
            },
            detection_accuracy: +(accuracy * 100).toFixed(2) + '%',
            failure_rate: +(failureRate * 100).toFixed(2) + '%',
            weighted_errors_E: E,
            error_rate_epsilon: +epsilon.toFixed(6),
            p99_score: +p99Score.toFixed(2),
            detection_score: {
                value: +detScore.toFixed(2),
                rate_component: +rateComponent.toFixed(2),
                absolute_penalty: +absolutePenalty.toFixed(2),
                cut_triggered: cutTriggered,
            },
            final_score: +finalScore.toFixed(2),
        },
    };

    return {
        'test/results.json': JSON.stringify(result, null, 2),
        //stdout: textSummary(data, { indent: ' ', enableColors: true }),
    };
}
```

Notas:
- `N` inclui `errs` (total real de iterações que geraram requisição, OK ou não).
- `classified` e `accuracy` continuam calculados do jeito antigo para retrocompatibilidade informativa.
- `p99Score` trata `p99 === 0` (caso degenerado antes de rodar o teste) retornando 0 — evita `log10(Infinity)`.
- `epsilon` e `failureRate` tratam `N === 0` retornando 0 — evita divisão por zero.
- Comentário de referência ao spec no topo do bloco.

- [ ] **Step 3: Verificar que o arquivo é sintaticamente válido** (sem rodar o k6 ainda, só parse):

Run: `nix-shell -p nodejs --run "node --check test/test.js"`
Expected: exit code 0, sem output de erro. (Se `node --check` reclamar de `import`, usar `node --input-type=module --check < test/test.js` como fallback.)

- [ ] **Step 4: Commit**

```bash
git add test/test.js
git commit -m "refactor(scoring): substituir multiplicador linear por log dual com corte

Usa K·log10(T_max/p99) para latência (sem saturação em p99=10ms) e
K·log10(1/ε) − β·log10(1+E) para detecção, com corte em -3000 quando
a taxa de falhas passa de 15%. Spec em docs/superpowers/specs/."
```

---

## Task 2: Atualizar `docs/br/AVALIACAO.md`

**Files:**
- Modify: `docs/br/AVALIACAO.md` (seções "Fórmula da pontuação", "Pesos — por que assim", "Interpretando o resultado dos testes", "Estratégias (dicas)")

### Steps

- [ ] **Step 1: Substituir seção "Fórmula da pontuação"**

Localizar o bloco que começa em `## Fórmula da pontuação` e vai até o `**TARGET_P99_MS = 10ms.**` (linhas ~29–36 do arquivo atual).

Substituir por:

```markdown
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
```

- [ ] **Step 2: Substituir seção "Pesos — por que assim"**

Localizar o bloco `## Pesos — por que assim` e seus três bullets (linhas ~38–42).

Substituir por:

```markdown
## Pesos e parâmetros — por quê

- **FN vale -3, Err vale -5** (dentro do cálculo de `E`) — mesma ordem que antes: deixar passar uma fraude é 3× pior que bloquear um cliente legítimo; devolver HTTP 5xx é 5× pior.
- **Log no p99** — recompensa cada ordem de grandeza de melhoria igualmente. Diferença entre 100ms e 10ms vale o mesmo que entre 10ms e 1ms. Não satura: sub-10ms continua ganhando pontos.
- **Dois termos no score_det** — a taxa (`K · log₁₀(1/ε)`) é N-invariante (mesma taxa de erro dá mesma pontuação pra qualquer tamanho de teste). A penalidade absoluta (`−β · log₁₀(1+E)`) punie volume real de erros (porque cada fraude não detectada é um prejuízo de verdade), mas cresce log para não explodir com N grande.
- **Corte em 15% de falhas** — acima disso, score_det = −3000, garantindo que um backend quebrado não passa só com p99 bom.
```

- [ ] **Step 3: Substituir seção "Interpretando o resultado dos testes"**

Localizar o bloco JSON de exemplo e os bullets abaixo (linhas ~48–73).

Substituir o JSON de exemplo por:

````markdown
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
````

Substituir os bullets explicativos por:

```markdown
- `breakdown` — contagens brutas de TP, TN, FP, FN e HTTP errors.
- `detection_accuracy` — `(TP + TN) / (TP + TN + FP + FN)`. Informativo, não entra no score.
- `failure_rate` — `(FP + FN + Err) / N`. Se passar de 15%, o corte dispara.
- `weighted_errors_E` — `1·FP + 3·FN + 5·Err`. Entra no cálculo de `ε` e na penalidade absoluta.
- `error_rate_epsilon` — `E / N`. A taxa ponderada que alimenta o termo log.
- `p99_score` — `K · log₁₀(T_max / p99)`.
- `detection_score.value` — o score_det final (depois do corte se disparou).
- `detection_score.rate_component` — só o termo `K · log₁₀(1/ε)`.
- `detection_score.absolute_penalty` — só o termo `−β · log₁₀(1 + E)`.
- `detection_score.cut_triggered` — `true` se `failure_rate > 15%` e o score caiu para −3000.
- `final_score` — `p99_score + detection_score.value`. O número que importa.
```

- [ ] **Step 4: Substituir seção "Estratégias (dicas)"**

Localizar bloco `## Estratégias (dicas)` até o fim do arquivo (linhas ~76–87).

Substituir por:

```markdown
## Estratégias (dicas)

Algumas observações que podem ser úteis.

**Log dá bônus exponencial para p99 baixo.** Cair de 10ms para 1ms vale 1000 pontos extras no `p99_score`. Vale a pena caçar cada millissegundo.

**O corte em 15% é duro.** Se mais de 15% das requisições falham (somando FP, FN e HTTP errors), o `detection_score` vai direto para −3000 e come todo o ganho de p99. Evitar a zona de corte é mais importante que afinar acurácia nas últimas casas.

**HTTP 500 contam pesado em dois lugares.** No `E` (peso 5 vs 1 de FP) e na `failure_rate` (cada Err conta como 1 falha bruta, igual a FP ou FN). Se der algum problema no backend, **devolver qualquer resposta rápida** (ex.: `approved: true`, `fraud_score: 0.0`) reduz erro HTTP, embora suba FP ou FN. Matematicamente, no regime normal, `-1` (FP) ou `-3` (FN) no peso do log ainda dói menos que `-5` (Err) + um ponto a mais na `failure_rate`.

**A taxa de erro ponderada é N-invariante.** Não dá pra "esconder" erros aumentando o volume — se a taxa fica na mesma faixa, o `rate_component` é o mesmo. Mas a `absolute_penalty` cresce log com o volume real de erros, então backends com falhas em larga escala perdem mais pontos que em escala pequena.

**Quando ANN vale a pena.** Brute force em 100k vetores × 14 dimensões por consulta pode ficar muito caro computacionalmente. Adotar ANN (HNSW, IVF) ou um banco vetorial pronto pode te ajudar. Mas sempre meça antes de complicar!

**Os arquivos de referência não mudam durante o teste.** Pré-processe à vontade no startup ou no build do container — quanto mais processamento você tira para fora do teste, melhor o `p99`.
```

- [ ] **Step 5: Commit**

```bash
git add docs/br/AVALIACAO.md
git commit -m "docs(br): atualizar AVALIACAO.md com scoring logarítmico dual

Reescreve fórmula, pesos, exemplo de JSON e estratégias para refletir
o novo scoring. Spec em docs/superpowers/specs/."
```

---

## Task 3: Atualizar `docs/en/EVALUATION.md`

**Files:**
- Modify: `docs/en/EVALUATION.md` (seções "Scoring formula", "Weights — why it's like this", "Interpreting the test results", "Strategies (tips)")

### Steps

- [ ] **Step 1: Substituir seção "Scoring formula"**

Localizar `## Scoring formula` e substituir o bloco até a linha `**TARGET_P99_MS = 10ms.**` por:

```markdown
## Scoring formula

The final score is the sum of two independent logarithmic components — one for latency (p99), one for detection quality.

### Latency component (score_p99)

```
score_p99 = K · log₁₀(T_max / p99)
```

- `K = 1000`, `T_max = 1000ms`.
- No cap, no floor: very low p99 keeps gaining bonus; p99 > 1000ms goes negative.

### Detection component (score_det)

```
E             = 1·FP + 3·FN + 5·Err           (weighted errors)
ε             = E / N                          (weighted error rate)
failures      = FP + FN + Err                  (raw failure count)
failure_rate  = failures / N

If failure_rate > 15%:
    score_det = −3000
Else:
    score_det = K · log₁₀(1 / max(ε, ε_MIN)) − β · log₁₀(1 + E)
```

- `K = 1000`, `ε_MIN = 0.001`, `β = 300`.
- Error weights: `FP = 1`, `FN = 3`, `Err = 5` (HTTP 500 is the worst).
- **Cutoff rule:** if more than 15% of requests fail (counting FP + FN + Err), the detection score drops straight to `−3000`, zeroing the best possible p99 score.

### Final score

```
final_score = score_p99 + score_det
```

Simple sum. Each component can be negative independently. Theoretical max is ~6000 (3000 each), reached with `p99 → 0` and `E = 0`.
```

- [ ] **Step 2: Substituir seção "Weights — why it's like this"**

Substituir os três bullets antigos por:

```markdown
## Weights and parameters — why

- **FN worth -3, Err worth -5** (inside `E`) — same ordering as before: letting a fraud through is 3× worse than blocking a legitimate customer; returning HTTP 5xx is 5× worse.
- **Log over p99** — rewards each order-of-magnitude improvement equally. The gap between 100ms and 10ms is worth the same as 10ms to 1ms. Doesn't saturate: sub-10ms keeps earning points.
- **Two terms in score_det** — the rate term (`K · log₁₀(1/ε)`) is N-invariant (same error rate = same score regardless of test size). The absolute penalty (`−β · log₁₀(1+E)`) punishes real error volume (every missed fraud is a real loss) but grows logarithmically so it doesn't explode with large N.
- **15% failure cutoff** — above that, score_det = −3000, so a broken backend can't slide by on good p99 alone.
```

- [ ] **Step 3: Substituir o JSON de exemplo e bullets**

Substituir o bloco JSON e bullets por:

````markdown
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

- `breakdown` — raw counts of TP, TN, FP, FN and HTTP errors.
- `detection_accuracy` — `(TP + TN) / (TP + TN + FP + FN)`. Informational only.
- `failure_rate` — `(FP + FN + Err) / N`. Crosses 15% → cutoff triggers.
- `weighted_errors_E` — `1·FP + 3·FN + 5·Err`. Feeds `ε` and the absolute penalty.
- `error_rate_epsilon` — `E / N`. Weighted rate used in the log term.
- `p99_score` — `K · log₁₀(T_max / p99)`.
- `detection_score.value` — final score_det (after cutoff if it triggered).
- `detection_score.rate_component` — just the `K · log₁₀(1/ε)` term.
- `detection_score.absolute_penalty` — just the `−β · log₁₀(1 + E)` term.
- `detection_score.cut_triggered` — `true` if `failure_rate > 15%` and the score dropped to −3000.
- `final_score` — `p99_score + detection_score.value`. The number that counts.
````

- [ ] **Step 4: Substituir seção "Strategies (tips)"**

Substituir por:

```markdown
## Strategies (tips)

Some useful observations.

**Log gives exponential bonus for low p99.** Dropping from 10ms to 1ms is worth 1000 extra points in `p99_score`. Chasing each millisecond pays off.

**The 15% cutoff is harsh.** If more than 15% of requests fail (summing FP, FN, and HTTP errors), `detection_score` drops straight to −3000 and wipes out any p99 gain. Avoiding the cutoff zone matters more than tuning accuracy in the last decimals.

**HTTP 500 hit in two places.** In `E` (weight 5 vs FP's 1) and in `failure_rate` (each Err counts as 1 raw failure, equal to FP or FN). If your backend has trouble, **returning any quick response** (e.g., `approved: true`, `fraud_score: 0.0`) lowers HTTP error count, though it raises FP or FN. Mathematically, in the normal regime, `-1` (FP) or `-3` (FN) in the log weight still hurts less than `-5` (Err) plus another point in `failure_rate`.

**Weighted error rate is N-invariant.** You can't "hide" errors by inflating volume — same rate, same `rate_component`. But `absolute_penalty` grows log with the actual error count, so backends failing at large scale lose more points than at small scale.

**When ANN is worth it.** Brute force over 100k vectors × 14 dimensions per query can get very expensive computationally. Adopting ANN (HNSW, IVF) or a ready-made vector database can help. But always measure before complicating things!

**Reference files don't change during the test.** Pre-process freely at startup or during the container build — the more processing you move outside of the test, the better the `p99`.
```

- [ ] **Step 5: Commit**

```bash
git add docs/en/EVALUATION.md
git commit -m "docs(en): update EVALUATION.md with dual logarithmic scoring

Mirrors the Portuguese update. Spec at docs/superpowers/specs/."
```

---

## Task 4: Remover `SCORING.md`

**Files:**
- Delete: `SCORING.md`

O arquivo foi usado como rascunho durante o design. A partir daqui, `docs/br/AVALIACAO.md` e `docs/en/EVALUATION.md` são fonte única.

### Steps

- [ ] **Step 1: Confirmar que nenhum link referencia `SCORING.md`**

Run: `grep -rn "SCORING.md" --exclude-dir=docs/superpowers`
Expected: sem resultados (ou só referências dentro de `docs/superpowers/specs/`, que podem ficar — são histórico de design).

Se aparecer referência em `README.md` ou nas docs principais, atualizar para apontar para `docs/br/AVALIACAO.md` / `docs/en/EVALUATION.md`.

- [ ] **Step 2: Remover o arquivo**

`SCORING.md` é untracked (nunca foi commitado), então `git rm` não funciona. Usar `rm` direto:

Run: `rm SCORING.md`

Verificar: `git status` não deve mais listar `SCORING.md`.

- [ ] **Step 3: Nada a commitar**

Como o arquivo era untracked, não há diff para commit. Pular commit desta task.

---

## Task 5: Smoke test do `test.js` atualizado

**Files:**
- Read: `test/results.json` (gerado pelo k6)

Validar que o scoring novo funciona rodando o k6 contra **algum backend** (o do próprio participante local, um mock simples, ou até um backend propositalmente quebrado para testar o corte).

### Steps

- [ ] **Step 1: Rodar o k6 contra o backend que estiver disponível**

Se o participante tem um backend local rodando em `localhost:9999`:

Run: `cd test && nix-shell --run "k6 run test.js"`

Se não tem backend rodando, suba um mock simples que sempre responda OK (veja opção no próximo step como alternativa).

- [ ] **Step 2 (opcional — se não tem backend): mock HTTP temporário**

```bash
nix-shell -p python3 --run "python3 -m http.server 9999" &
```

O `http.server` responde 501 para POST — vai fazer `error_count` explodir, o que serve justamente para testar o corte em 15%.

Alternativa mais controlada: um mock em Node que sempre responde `{"approved": true}`:

```bash
nix-shell -p nodejs --run 'node -e "
const http=require(\"http\");
http.createServer((req,res)=>{
  let b=\"\";req.on(\"data\",d=>b+=d);req.on(\"end\",()=>{
    res.writeHead(200,{\"Content-Type\":\"application/json\"});
    res.end(JSON.stringify({approved:true,fraud_score:0}));
  });
}).listen(9999);"' &
```

Isso aprova tudo → metade do dataset vira FN (assumindo ~50% fraud rate), o que põe o `failure_rate` acima de 15% e deve disparar o corte.

- [ ] **Step 3: Abrir `test/results.json` e verificar campos novos**

Run: `cat test/results.json`

Checklist:
- [ ] Tem `scoring.failure_rate` como string com `%`.
- [ ] Tem `scoring.weighted_errors_E` como número.
- [ ] Tem `scoring.error_rate_epsilon` como número.
- [ ] Tem `scoring.p99_score` como número.
- [ ] Tem `scoring.detection_score` com sub-campos `value`, `rate_component`, `absolute_penalty`, `cut_triggered`.
- [ ] Tem `scoring.final_score` como número.
- [ ] **Não tem mais** `latency_multiplier`, `raw_score`, `target_p99_ms`.

- [ ] **Step 4: Validação numérica de um cenário**

Pegar os valores de `breakdown`, `response_times.p99` e `failure_rate` do JSON e recalcular à mão usando a fórmula do spec. Comparar com `p99_score`, `detection_score.value` e `final_score`.

Exemplo (com `tp=0, tn=2500, fp=0, fn=2500, errs=0, p99=2ms`):
- `E = 0 + 3·2500 + 0 = 7500`
- `N = 5000`
- `ε = 7500/5000 = 1.5`
- `failure_rate = 2500/5000 = 50%` → > 15% → corte
- `detection_score.value = -3000`
- `p99_score = 1000 · log10(1000/2) = 2699`
- `final_score = 2699 + (-3000) = -301`

Se o JSON bater com esse cálculo (aproximado para p99 real), scoring novo está correto.

- [ ] **Step 5: Se o mock foi iniciado, encerrar processo**

Run: `pkill -f "http.server 9999"` ou `pkill -f "node -e"` (dependendo do mock usado).

- [ ] **Step 6: Não commitar `test/results.json`**

Verificar: `git status`
Se `results.json` aparecer como modificado, descartar: `git checkout -- test/results.json`
(Se não estava commitado antes, só não stage.)

---

## Critérios de sucesso

- `test/test.js` passa em `node --check`.
- K6 roda sem erro, gera `results.json` com os campos novos.
- Cálculos manuais batem com os valores no `results.json`.
- Docs BR e EN atualizados consistentemente.
- `SCORING.md` removido, sem links quebrados.
