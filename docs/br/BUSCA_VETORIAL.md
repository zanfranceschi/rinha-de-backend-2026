# Busca vetorial — uma introdução

Este documento explica, com um exemplo passo-a-passo, como funciona uma **busca vetorial** no contexto da detecção de fraude desta edição da Rinha.

## O que é uma busca vetorial?

Uma busca vetorial procura o que mais se parece em vez de procurar por igualdade exata. É uma **busca por semelhança**.

Cada transação é representada por um **vetor** — uma lista de números que descreve suas características, como valor, horário e média do portador. Transações parecidas têm vetores parecidos, ou seja, ficam "próximas" umas das outras num espaço.

A busca vetorial responde à pergunta:

> *"Dada esta transação nova, quais transações do histórico mais se aproximam dela?"*

Se as transações mais parecidas foram classificadas como fraude, a nova provavelmente também é. Se foram legítimas, a nova provavelmente é legítima.

### Onde mais isso aparece?

A mesma técnica está por trás de várias aplicações do dia a dia:

- **Sistemas de recomendação** — Spotify, Netflix e Amazon sugerem músicas, filmes e produtos encontrando itens cujos vetores são parecidos com os que você já consumiu.
- **Busca semântica e RAG** — em vez de procurar por palavras exatas, sistemas como o ChatGPT comparam o vetor (*embedding*) da sua pergunta com vetores de documentos para encontrar os trechos mais relevantes.
- **Busca por imagem** — Google Images e Pinterest convertem fotos em vetores e comparam visualmente.
- **Reconhecimento facial** — cada rosto vira um vetor; identificar uma pessoa é encontrar o vetor mais próximo no banco de rostos conhecidos.
- **Detecção de plágio e de duplicatas** — comparar o vetor de um texto contra um corpus para encontrar conteúdo parecido, mesmo que reescrito.
- **Detecção de fraude e de anomalias** — o caso desta Rinha: comparar o "formato" de uma transação contra um histórico classificado.

Em todos esses casos a ideia é a mesma: representar "coisas" como vetores e usar a proximidade no espaço como medida de similaridade.

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
  "amount": 12500.00,
  "hour": 22,
  "customer_avg_amount": 4800.00
}
```

### 3. Normalização — cada campo vira um número entre 0.0 e 1.0

```
dim1 = limitar(amount              / max_amount) = limitar(12500 / 10000) = 1.0   ← limitado, era 1.25
dim2 = limitar(hour                / max_hour)   = limitar(22    / 23)    = 0.96
dim3 = limitar(customer_avg_amount / max_avg)    = limitar(4800  / 5000)  = 0.96
```

> A função `limitar(x)` é um **clamp**: restringe o valor ao intervalo `[0.0, 1.0]`. Veja a `dim1`: o `amount` desta transação (`12.500`) ultrapassa o teto `max_amount = 10.000`, e a divisão dá `1.25` — fora do intervalo. O `limitar()` corta em `1.0`. Sem essa proteção, o vetor sairia do espaço normalizado e distorceria toda a busca vetorial.

**Vetor de consulta resultante:**

```
[1.0, 0.96, 0.96]
```

### 4. Busca vetorial — calcular a distância até cada referência

Aqui, usando a distância euclidiana com 3 dimensões, fica:

```math
\text{dist}(q, r) = \sqrt{(q_1 - r_1)^2 + (q_2 - r_2)^2 + (q_3 - r_3)^2}
```

> A **distância euclidiana** é a "distância em linha reta" entre dois pontos no espaço — o teorema de Pitágoras estendido para mais dimensões. Para cada par de coordenadas você calcula a diferença, eleva ao quadrado, soma tudo e tira a raiz. Quanto menor o resultado, mais parecidos são os dois vetores.

**Dataset de referência:**

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

**Distâncias calculadas e ordenadas (da menor para a maior):**

| # | vetor de referência       | label | distância até `[1.0, 0.96, 0.96]` |
|---|---------------------------|-------|-----------------------------------|
| 4 | [0.9708, 1.0000, 1.00]    | fraud | **0.064** ← mais perto            |
| 2 | [0.5796, 0.9167, 1.00]    | fraud | **0.425**                         |
| 5 | [0.4082, 1.0000, 1.00]    | fraud | **0.595**                         |
| 3 | [0.0035, 0.1667, 0.05]    | legit | 1.565                             |
| 1 | [0.0100, 0.0833, 0.05]    | legit | 1.605                             |
| 6 | [0.0092, 0.0833, 0.05]    | legit | 1.606                             |

### 5. Os K=3 vizinhos mais próximos (KNN de K=3)

Selecionamos os `3` vizinhos mais próximos:

```
1º — ref 4 (fraud) — 0.064
2º — ref 2 (fraud) — 0.425
3º — ref 5 (fraud) — 0.595
```

### 6. Votação por maioria

A votação é, na prática, a razão entre referências de fraude e referências legítimas. Neste caso, as 3 transações mais próximas estão rotuladas como fraudulentas.

```
fraud: 3 votos
legit: 0 votos

fraud_score = 3 / 3 = 1.0
```

### 7. Decisão

Nem todas as `K` transações mais próximas precisam estar rotuladas como fraude para que a nova transação também seja marcada como fraude. Você pode, por exemplo, definir que, se dois terços das referências forem fraude, a transação também é marcada como fraude. Esse valor é o `threshold` (ou limiar). Vamos usar um threshold de `0.6`:

```
fraud_score (1.0) >= threshold (0.6) → transação NEGADA
```

**Resposta:**

```json
{
  "approved": false,
  "fraud_score": 1.0
}
```

---

## A intuição

O exemplo mostra uma transação cujas 3 referências mais próximas estão rotuladas como fraude. Isso significa que o "formato" da transação — baseado nas referências — provavelmente é o de uma fraude. Os três vizinhos mais próximos têm em comum *valor alto, horário tardio e portador de alto padrão*.

A busca vetorial **não "entende" fraude** — ela apenas encontra as transações passadas mais parecidas e deixa a maioria (votação) ou a proximidade ponderada decidir o rótulo da nova.

Em termos de Machine Learning, isso se chama **aprendizado supervisionado não-paramétrico** (ou *instance-based learning*): não existe modelo treinado, apenas o dataset memorizado e a busca feita em tempo de consulta.

---

## Métricas de distância e algoritmos de busca

O exemplo acima usa **distância euclidiana**, mas ela é apenas uma das opções para medir "quão parecidos são dois vetores". As mais comuns são:

- **Euclidiana** — $\sqrt{\sum_i (q_i - r_i)^2}$. A "distância em linha reta" no espaço. Intuitiva e normalmente usada como ponto de partida.
- **Manhattan** (L1) — $\sum_i |q_i - r_i|$. Soma das diferenças absolutas. Mais barata de calcular (sem raiz nem multiplicação) e penaliza outliers de forma mais suave.
- **Cosseno** — compara o **ângulo** entre os vetores, não o tamanho. Útil quando o que importa é a "direção" do vetor, e não a magnitude (textos, embeddings, etc.).

### KNN exato vs ANN (Approximate Nearest Neighbors)

A forma mais simples de encontrar os K vizinhos mais próximos é o **KNN exato por força bruta**: percorrer todas as referências, calcular a distância para cada uma e ordenar. Funciona, mas custa O(N * D) (D = dimensão dos vetores) por consulta — com 3 milhões de referências e um orçamento de latência apertado, pode ser caro demais.

**ANN** é uma alternativa: estruturas de dados que abrem mão de um pouco de precisão para responder mais rápido. Algumas famílias:

- **HNSW** (Hierarchical Navigable Small World) — grafo em camadas. É o que pgvector, Qdrant e a maioria dos bancos vetoriais usam por padrão. Consulta em **`O(log N)`**.
- **IVF** (Inverted File Index) — particiona o espaço em "células" e busca apenas nas mais próximas da consulta. Consulta em **`O(√N)`** com particionamento típico.
- **LSH** (Locality-Sensitive Hashing) — funções de hash que colidem para vetores parecidos. Consulta em **`O(N^ρ)`** com `ρ < 1` (sub-linear; depende do fator de aproximação).

Existem também outras formas de realizar **busca exata** sem usar força bruta:

- **KD-Tree** (K-Dimensional Tree) – Divide o espaço usando hiperplanos ortogonais. Em cada nível, escolhe um eixo e divide os pontos pela mediana.
- **VP-Tree** (Vantage Point Tree) – Divide o espaço usando distâncias esféricas. Escolhe um ponto fixo e separa os demais entre "dentro" ou "fora" de um raio circular.
- **Ball Tree** – Agrupa pontos em hiperesferas aninhadas. Ao contrário da KD-Tree, as regiões (bolas) podem se sobrepor, mas são mais eficientes em dimensões ligeiramente maiores.
- **Cover Tree** – Organiza os dados em uma hierarquia de níveis baseada em escalas de distância (geralmente potências de 2).
- **BK-Tree** (Burkhard-Keller Tree) – Especializada para distâncias discretas (valores inteiros). Organiza os nós com base na distância exata de cada filho para o pai.


#### Qual abordagem usar nesta Rinha de Backend?

Você pode usar **força bruta, KNN exato, ANN, banco vetorial, modelo de IA treinado, uma sequência de IF/ELSE** ou qualquer outra coisa. O que você precisa encontrar é o equilíbrio entre a precisão da busca vetorial e a performance — a estratégia fica a seu critério.

---

## Próximo passo

Este documento usa um exemplo simplificado, com 3 dimensões, apenas para fins didáticos. A especificação real do desafio usa **14 dimensões**, descritas em [REGRAS_DE_DETECCAO.md](./REGRAS_DE_DETECCAO.md), onde também estão exemplos completos do fluxo.
