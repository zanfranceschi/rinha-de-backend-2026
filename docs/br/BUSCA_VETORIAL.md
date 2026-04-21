# Busca Vetorial — Uma Introdução

Este documento explica, passo-a-passo, como funciona uma **busca vetorial** no contexto da detecção de fraude desta edição da Rinha.

## O que é uma busca vetorial?

Uma busca vetorial é simplesmente achar o que mais se parece em vez de buscar por igualdade exata. É uma **busca por semelhança**.

Cada transação é representada por um **vetor** — uma lista de números que descreve suas características (valor, horário, média do portador, etc.). Transações parecidas têm vetores parecidos — ficam "próximas" umas das outras no espaço.

A busca vetorial responde à pergunta:

> *"Dada essa transação nova, quais transações do meu histórico mais se parecem com ela?"*

Se as transações mais parecidas foram classificadas como fraude, provavelmente a nova também é. Se foram legítimas, provavelmente é legítima.

---

## Exemplo passo-a-passo

Vamos acompanhar uma transação do início ao fim — do payload recebido até a decisão.

### 1. Constantes de normalização

Valores de referência que definem o "teto" de cada dimensão:

```json
{
  "max_amount": 10000,
  "max_hour":   23,
  "max_avg":    5000
}
```

### 2. Chega uma nova transação

```json
{
  "amount": 8200.00,
  "hour": 22,
  "customer_avg_amount": 4800.00
}
```

### 3. Normalização — cada campo vira um número entre 0.0 e 1.0

```
dim1 = amount              / max_amount = 8200 / 10000 = 0.82
dim2 = hour                / max_hour   = 22   / 23    = 0.96
dim3 = customer_avg_amount / max_avg    = 4800 / 5000  = 0.96
```

Vetor de consulta resultante:

```
[0.82, 0.96, 0.96]
```

### 4. Busca vetorial — calcular a distância até cada referência

Usando distância euclidiana (quanto menor, mais parecido):

```
dist(q, ref) = √( (q1-r1)² + (q2-r2)² + (q3-r3)² )
```

Dataset de referência (exemplo reduzido):

```json
[
  { "vector": [0.0100, 0.0833, 0.05], "label": "legit" },
  { "vector": [0.5796, 0.9167, 1.00], "label": "fraud" },
  { "vector": [0.0035, 0.1667, 0.05], "label": "legit" },
  { "vector": [0.9708, 1.0000, 1.00], "label": "fraud" },
  { "vector": [0.4082, 1.0000, 1.00], "label": "fraud" },
  { "vector": [0.0092, 0.0833, 0.05], "label": "legit" }
]
```

Distâncias calculadas:

| # | vetor de referência       | label | distância até `[0.82, 0.96, 0.96]` |
|---|---------------------------|-------|-------------------------------------|
| 4 | [0.9708, 1.0000, 1.00]    | fraud | **0.161** ← mais perto              |
| 2 | [0.5796, 0.9167, 1.00]    | fraud | **0.247**                           |
| 5 | [0.4082, 1.0000, 1.00]    | fraud | **0.416**                           |
| 3 | [0.0035, 0.1667, 0.05]    | legit | 1.458                               |
| 1 | [0.0100, 0.0833, 0.05]    | legit | 1.501                               |
| 6 | [0.0092, 0.0833, 0.05]    | legit | 1.501                               |

### 5. Os K=3 vizinhos mais próximos

```
1º — ref 4 (fraud) — 0.161
2º — ref 2 (fraud) — 0.247
3º — ref 5 (fraud) — 0.416
```

### 6. Votação por maioria

```
fraud: 3 votos
legit: 0 votos

fraud_score = 3 / 3 = 1.0
```

### 7. Decisão

Com threshold `0.6`:

```
fraud_score (1.0) >= threshold (0.6) → transação NEGADA
```

Resposta:

```json
{
  "approved": false,
  "fraud_score": 1.0
}
```

---

## A intuição

Os três vizinhos mais próximos têm em comum *valor alto, horário tardio e portador de alto padrão* — exatamente o "formato" de transações marcadas como fraude no dataset.

A busca vetorial **não "entende" fraude** — ela apenas encontra as transações passadas mais parecidas e deixa a maioria decidir o rótulo da nova.

---

## Métricas de distância e algoritmos de busca

O exemplo acima usa **distância euclidiana**, mas ela é apenas uma das opções para medir "quão parecidos são dois vetores". As mais comuns:

- **Euclidiana** — `√Σ(qᵢ - rᵢ)²`. A "distância em linha reta" no espaço. Intuitiva e a mais usada como ponto de partida.
- **Manhattan** (L1) — `Σ|qᵢ - rᵢ|`. Soma das diferenças absolutas. Mais barata de calcular (sem raiz nem multiplicação) e penaliza outliers de forma mais suave.
- **Cosseno** — compara o **ângulo** entre os vetores, não o tamanho. Útil quando você se importa com a "direção" do vetor mais do que com a magnitude (textos, embeddings, etc.).

Para o problema desta Rinha, qualquer uma funciona — o que importa é a consistência: o vetor de consulta e os vetores de referência precisam estar no mesmo espaço normalizado.

### KNN exato vs ANN (Approximate Nearest Neighbors)

A forma mais simples de achar os K vizinhos mais próximos é o **KNN exato**: percorrer todas as referências, calcular a distância para cada uma e ordenar. Funciona, mas custa O(N) por consulta — com 100k referências e um orçamento de latência apertado, pode ser caro demais.

**ANN** é a alternativa: estruturas de dados que sacrificam um pouquinho de precisão para responder em tempo sub-linear. Algumas famílias:

- **HNSW** (Hierarchical Navigable Small World) — grafo em camadas, é o que pgvector, Qdrant e a maioria dos bancos vetoriais usam por padrão.
- **IVF** (Inverted File Index) — particiona o espaço em "células" e busca apenas nas mais próximas da consulta.
- **LSH** (Locality-Sensitive Hashing) — funções de hash que colidem para vetores parecidos.

Você pode usar **brute force em memória, KNN exato com índice espacial, ANN, banco vetorial pronto** ou qualquer combinação. O desafio avalia a qualidade da resposta (`approved`) e a latência — a estratégia é com você.

---

## Próximo passo

Este documento usa um exemplo simplificado (3 dimensões) só para fins didáticos. A especificação real do desafio usa **14 dimensões**, descritas em [VETORIZACAO.md](./VETORIZACAO.md), e exemplos completos estão em [EXEMPLOS.md](./EXEMPLOS.md).