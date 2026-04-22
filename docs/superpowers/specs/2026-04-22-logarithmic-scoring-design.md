# Pontuação logarítmica dual — design

**Data:** 2026-04-22
**Escopo:** `test/test.js`, `docs/br/AVALIACAO.md`, `docs/en/AVALIACAO.md` (se existir), `SCORING.md`

## Contexto e motivação

A pontuação atual da Rinha de Backend 2026 combina precisão de detecção de fraude com latência via multiplicador linear:

```
raw_score        = (TP × 1) + (TN × 1) + (FP × −1) + (FN × −3) + (Err × −5)
latency_mult     = TARGET_P99_MS / max(p99, TARGET_P99_MS)
final_score      = max(0, raw_score) × latency_mult
```

Com `TARGET_P99_MS = 10ms`.

Problemas:

1. **Multiplicador satura em 1.0** quando `p99 ≤ 10ms` — não há incentivo para melhorar latência abaixo do alvo.
2. **Relação linear** com latência acima do alvo (`10/p99`) — diferença entre 20ms e 100ms é pequena relativamente à entre 10ms e 20ms.
3. **Score de precisão cresce linearmente com N** — dificulta comparar resultados entre rodadas de tamanhos diferentes.

A esboço em `SCORING.md` sugere trocar por pontuação logarítmica estilo `K · log₁₀(T_max / P99)`, que resolve (1) e (2). Este documento estende a mesma ideia para a detecção de fraude e define como as duas componentes se combinam.

## Fórmula proposta

### Componente de latência (P99)

```
score_p99 = K · log₁₀(T_max / P99)
```

- `K = 1000`
- `T_max = 1000ms`
- Sem piso: `P99 > T_max` produz score negativo.

### Componente de detecção

```
E         = 1·FP + 3·FN + 5·Err              (erros ponderados)
ε         = E / N                             (taxa ponderada)
falhas    = FP + FN + Err                     (contagem pura de falhas)
tx_falhas = falhas / N                        (taxa pura)

Se tx_falhas > TX_CORTE:
    score_det = −3000                         (regra de corte: zerou)
Senão:
    score_det = K · log₁₀(1 / max(ε, ε_MIN))  ← componente por taxa (N-invariante)
              − β · log₁₀(1 + E)              ← penalidade por volume absoluto
```

- `K = 1000`
- `ε_MIN = 0.001` (piso da taxa: 0,1% de erro ponderado é o "excelente")
- `β = 300`
- `TX_CORTE = 0.15` (15% de falhas brutas — acima disso, score_det = −3000)
- Pesos: `FP = 1`, `FN = 3`, `Err = 5` (HTTP 500 é o pior, mantém a ordem atual).

Racional dos três mecanismos:

- **Taxa** (`K · log₁₀(1/ε)`) é invariante ao tamanho do teste — mesma taxa de erro dá mesma pontuação independente de N.
- **Volume absoluto** (`−β · log₁₀(1 + E)`) penaliza contagens reais de erro, porque cada fraude não detectada é um prejuízo real. Cresce logaritmicamente, então não explode com volumes altos.
- **Regra de corte** (`tx_falhas > 15%`) garante que backends muito ruins caem a −3000, zerando o máximo teórico de `score_p99` (+3000). É um cliff explícito — não tem gradiente na fronteira. O threshold de 15% é ajustável.
- **Máximo preservado em E=0**: quando `E = 0`, a penalidade é zero e não há falhas, então `score_det` alcança o teto de 3000 pontos — N-invariante no máximo teórico.

### Score final

```
score_final = score_p99 + score_det
```

- Aditivo simples, sem `max(0, ...)`.
- Cada componente pode ser negativo independentemente.
- Duas dimensões ortogonais: latência e precisão.

## Propriedades

- **Máximo teórico ≈ 6000** pontos (3000 cada), com `P99 → 0` e `E = 0`. Sem cap rígido no P99.
- **Ordem de grandeza igual** entre as duas componentes.
- **N-invariância no máximo**: tamanho do teste não afeta o teto.
- **Penalização por volume absoluto**: mesma taxa com `N` maior gera mais penalidade (volume maior = mais prejuízo real).
- **Não satura**: melhorias abaixo do alvo continuam ganhando pontos.
- **Corte duro em 15% de falhas**: acima disso, score_det = −3000 independente de quão baixo seja p99, o total é zerado ou negativo.
- **Negativos possíveis**: `p99 > 1000ms`, `ε > 1`, ou `tx_falhas > 15%` produzem scores negativos.

## Cenários de exemplo

| Cenário                                        | Falhas | Tx falhas | score_p99 | score_det | Total  |
|------------------------------------------------|--------|-----------|-----------|-----------|--------|
| Perfeito (E=0, p99=1ms)                        | 0      | 0%        | 3000      | 3000      | 6000   |
| Excelente (N=5000, E=5, p99=5ms)               | 5      | 0,1%      | 2301      | 2767      | 5068   |
| Bom (N=5000, E=50, p99=20ms)                   | 50     | 1%        | 1699      | 1488      | 3187   |
| Mediano (N=5000, E=100, p99=100ms)             | 100    | 2%        | 1000      | 1098      | 2098   |
| Ruim (N=5000, E=500, p99=500ms)                | 500    | 10%       | 301       | 190       | 491    |
| No limite (N=5000, 750 FP, p99=500ms)          | 750    | 15%       | 301       | −39       | 262    |
| Passou do corte (N=5000, 751 FP, p99=500ms)    | 751    | 15,02%    | 301       | **−3000** | −2699  |
| Catastrófico (5000 HTTP 500, p99=2000ms)       | 5000   | 100%      | −301      | **−3000** | −3301  |

## Mudanças necessárias

### `test/test.js`

Substituir o bloco atual de `handleSummary` (linhas ~88–98) que calcula `rawScore`, `latencyMult`, `finalScore`:

- Calcular `E = 1·fp + 3·fn + 5·errs` (ponderado) e `falhas = fp + fn + errs` (contagem pura).
- Calcular `N = tp + tn + fp + fn + errs` (total processado com sucesso ou não).
- Computar `score_p99 = K · log₁₀(T_max / p99)`.
- Computar `tx_falhas = falhas / N`.
- Se `tx_falhas > TX_CORTE`: `score_det = −3000`; senão: `score_det = K · log₁₀(1 / max(E/N, ε_MIN)) − β · log₁₀(1 + E)`.
- Computar `score_final = score_p99 + score_det`.

Constantes no topo do arquivo:
```js
const K = 1000;
const T_MAX_MS = 1000;
const EPSILON_MIN = 0.001;
const BETA = 300;
const TX_CORTE = 0.15;
const SCORE_DET_CORTE = -3000;
```

Campos a expor no JSON de resultado (substituindo `scoring.latency_multiplier`, `raw_score`, `final_score`, mantendo `breakdown` e `detection_accuracy`):
- `p99_score`
- `detection_score` com sub-campos:
  - `rate_component` (o termo log da taxa)
  - `absolute_penalty` (o termo `−β·log₁₀(1+E)`)
  - `weighted_errors_E`
  - `error_rate_epsilon`
  - `failure_rate` (contagem pura) e `cut_triggered` (bool, se caiu no corte)
- `final_score`

### `docs/br/AVALIACAO.md`

Reescrever a seção "Fórmula da pontuação" e "Pesos — por que assim" para refletir as novas fórmulas. Atualizar o JSON de exemplo em "Interpretando o resultado dos testes" para os novos campos. Seção "Estratégias (dicas)" precisa ser refeita — as observações atuais sobre multiplicador e corte em p99=10ms não se aplicam mais.

### `docs/en/AVALIACAO.md`

Verificar se existe versão em inglês espelho e aplicar a mesma atualização.

### `SCORING.md`

O arquivo atual é um rascunho. Duas opções:
1. Remover (documento viveu como draft; AVALIACAO.md fica como fonte única).
2. Manter apenas a primeira seção (tabela de P99) como referência auxiliar, remover o resto.

Recomendação: opção 1. Menos documentos para manter em sincronia.

## Fora de escopo

- Não mexer em `test-data.json` nem no dataset de referências.
- Não alterar os thresholds/conceitos de TP/TN/FP/FN (continua por `approved: true|false`).
- Não introduzir nova métrica (ex.: F1-score, AUC) — manter a decomposição atual.
- Não expor novo formato de request/response do backend dos participantes.

## Validação

- Testes unitários puros da função de scoring (inputs: combinações de TP/TN/FP/FN/Err/p99; outputs esperados calculados manualmente a partir da tabela de cenários).
- Rodar o k6 contra um backend mock que dá respostas conhecidas, verificar que o `results.json` bate com os valores esperados.
