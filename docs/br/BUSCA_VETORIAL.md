# Busca Vetorial — Explicação Didática

Este documento explica, passo-a-passo, como funciona uma **busca vetorial** no contexto da detecção de fraude desta edição da Rinha.

## O que é uma busca vetorial?

Uma busca vetorial é simplesmente achar o que mais se parece em vez de buscar por igualdade exata. É uma **busca por semelhança**.

Cada transação é representada por um **vetor** — uma lista de números que descreve suas características (valor, horário, média do portador, etc.). Transações parecidas têm vetores parecidos — ficam "próximas" umas das outras no espaço.

A busca vetorial responde à pergunta:

> *"Dada essa transação nova, quais transações do meu histórico mais se parecem com ela?"*

Se as transações mais parecidas foram classificadas como fraude, provavelmente a nova também é. Se foram legítimas, provavelmente é legítima.

---

## Exemplo passo-a-passo

Vamos acompanhar uma transação do início ao fim — da chegada dos dados brutos até a decisão.

### 1. Constantes de normalização

Valores de referência que definem o "teto" de cada dimensão:

```json
{
  "max_amount": 10000,
  "max_hour":   23,
  "max_avg":    5000
}
```

### 2. Chega uma nova transação (dados brutos)

```
Valor da transação:            R$ 8.200,00
Hora do dia:                   22h
Média histórica do portador:   R$ 4.800,00
```

### 3. Normalização — cada campo vira um número entre 0.0 e 1.0

```
dim1 = 8200 / 10000 = 0.82
dim2 = 22   / 23    = 0.96
dim3 = 4800 / 5000  = 0.96
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

Não, é impreciso. Há duas coisas distintas acontecendo:

1. Geração dos payloads (o request em si):
Feita por gen_request() usando PRNG (PCG32) + regras por perfil (LEGIT/FRAUD/BORDERLINE) em main.c:272-391. Cada campo (amount, mcc, km_home, etc.) é sorteado dentro de faixas determinadas pelo perfil. KNN não participa disso.

2. Rotulagem dos payloads (expected_response):
Aí sim — knn_classify() usa KNN (k=5) com euclidiana para atribuir approved e fraud_score comparando o vetor normalizado do payload contra as referências.

Formulação tecnicamente correta:

Os payloads são gerados sinteticamente via PRNG com perfis rotulados (legit/fraud/borderline), e suas respostas esperadas são rotuladas por KNN (k=5) com distância euclidiana contra um conjunto de vetores de referência de 14 dimensões.

A diferença importa porque:

Trocar o PRNG/perfis muda a distribuição dos dados.
Trocar KNN/euclidiana muda apenas a função de rotulagem (o ground truth).
São eixos independentes.