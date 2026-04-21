# Vector search — an introduction

This document explains, step-by-step through an example, how a **vector search** works in the context of fraud detection for this edition of Rinha.

## What is a vector search?

A vector search is simply finding what is most similar instead of searching for exact equality. It's a **similarity search**.

Each transaction is represented by a **vector** — a list of numbers describing its characteristics (value, time, cardholder's average, etc.). Similar transactions have similar vectors — they stay "close" to each other in space.

The vector search answers the question:

> *"Given this new transaction, which transactions in my history look most like it?"*

If the most similar transactions were classified as fraud, the new one probably is too. If they were legitimate, it's probably legitimate.

### Where else does this appear?

The same technique is behind several everyday applications:

- **Recommendation systems** — Spotify, Netflix, and Amazon suggest songs, movies, and products by finding items whose vectors are similar to those you've already consumed.
- **Semantic search and RAG** — instead of searching for exact words, systems like ChatGPT compare the vector (*embedding*) of your question with document vectors to find the most relevant excerpts.
- **Image search** — Google Images and Pinterest convert photos into vectors and compare them visually.
- **Facial recognition** — each face becomes a vector; identifying a person means finding the closest vector in the database of known faces.
- **Plagiarism and duplicate detection** — comparing a text's vector against a corpus to find similar content (even when rewritten).
- **Fraud and anomaly detection** — the case of this Rinha: comparing the "shape" of a transaction against a classified history.

In all these cases the idea is the same — represent "things" as vectors and use proximity in space as a measure of similarity.

---

## Step-by-step example

Let's follow a transaction from start to finish — from the received payload to the decision.

### 1. Normalization constants

Reference values that define the "ceiling" for each dimension:

```json
{
  "max_amount": 10000,
  "max_hour":   23,
  "max_avg":    5000
}
```

### 2. A new transaction arrives

```json
{
  "amount": 12500.00,
  "hour": 22,
  "customer_avg_amount": 4800.00
}
```

### 3. Normalization — each field becomes a number between 0.0 and 1.0

```
dim1 = clamp(amount              / max_amount) = clamp(12500 / 10000) = 1.0   ← clamped, it was 1.25
dim2 = clamp(hour                / max_hour)   = clamp(22    / 23)    = 0.96
dim3 = clamp(customer_avg_amount / max_avg)    = clamp(4800  / 5000)  = 0.96
```

> The `clamp(x)` function restricts the value to the interval `[0.0, 1.0]`. See `dim1`: this transaction's `amount` (`12,500`) exceeds the ceiling `max_amount = 10,000`, and the division yields `1.25` — outside the interval. `clamp()` caps it at `1.0`. Without this protection, the vector would leave the normalized space and distort the entire vector search.

**Resulting query vector:**

```
[1.0, 0.96, 0.96]
```

### 4. Vector search — compute the distance to each reference

Here, using Euclidean distance calculation, with 3 dimensions, it would be:

```math
\text{dist}(q, r) = \sqrt{(q_1 - r_1)^2 + (q_2 - r_2)^2 + (q_3 - r_3)^2}
```

> The **Euclidean distance** is the "straight-line distance" between two points in space — the Pythagorean theorem extended to more dimensions. For each pair of coordinates you compute the difference, square it, sum everything, and take the square root. The smaller the result, the more similar the two vectors.


**Reference dataset:**

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

**Distances calculated and sorted (from smallest to largest):**

| # | reference vector          | label | distance to `[1.0, 0.96, 0.96]`   |
|---|---------------------------|-------|-----------------------------------|
| 4 | [0.9708, 1.0000, 1.00]    | fraud | **0.064** ← closest               |
| 2 | [0.5796, 0.9167, 1.00]    | fraud | **0.425**                         |
| 5 | [0.4082, 1.0000, 1.00]    | fraud | **0.595**                         |
| 3 | [0.0035, 0.1667, 0.05]    | legit | 1.565                             |
| 1 | [0.0100, 0.0833, 0.05]    | legit | 1.605                             |
| 6 | [0.0092, 0.0833, 0.05]    | legit | 1.606                             |

### 5. The K=3 nearest neighbors
We then select the `3` nearest neighbors.
```
1st — ref 4 (fraud) — 0.064
2nd — ref 2 (fraud) — 0.425
3rd — ref 5 (fraud) — 0.595
```

### 6. Majority vote
The vote is actually the ratio between fraud and legitimate references. In this case, the 3 closest transactions are labeled as fraudulent.

```
fraud: 3 votes
legit: 0 votes

fraud_score = 3 / 3 = 1.0
```

### 7. Decision
Not all `K` nearest transactions need to be labeled as fraud for the transaction being processed to also be marked as fraud. We can, for example, say that if two-thirds of the references are fraud, we also mark the transaction as fraud. This is what we call the `threshold`. Let's use a threshold of `0.6`:

```
fraud_score (1.0) >= threshold (0.6) → transaction DENIED
```

**Response:**

```json
{
  "approved": false,
  "fraud_score": 1.0
}
```

---

## The intuition

The example here shows a transaction whose 3 nearest references were labeled as fraud. This means that the "shape" of the transaction – based on the references – is most likely that of a fraud. The three nearest neighbors share in common *high value, late hour, and high-end cardholder*.

The vector search **doesn't "understand" fraud** — it just finds the most similar past transactions and lets the majority decide the label of the new one.

In Machine Learning terms, this is called **non-parametric supervised learning** (or *instance-based learning*) — there's no trained model: just the memorized dataset and search at query time. Rinha is also on the AI hype. 😎

---

## Distance metrics and search algorithms

The example above uses **Euclidean distance**, but it's just one of the options for measuring "how similar two vectors are". The most common ones are:

- **Euclidean** — $\sqrt{\sum_i (q_i - r_i)^2}$. The "straight-line distance" in space. Intuitive and the most commonly used starting point.
- **Manhattan** (L1) — $\sum_i |q_i - r_i|$. Sum of absolute differences. Cheaper to compute (no square root or multiplication) and penalizes outliers more softly.
- **Cosine** — compares the **angle** between vectors, not the size. Useful when you care about the "direction" of the vector more than the magnitude (texts, embeddings, etc.).

### Exact KNN vs ANN (Approximate Nearest Neighbors)

The simplest way to find the K nearest neighbors is **exact KNN**: iterate through all references, calculate the distance to each one, and sort. It works, but costs O(N) per query — with 100k references and a tight latency budget, it can be too expensive.

**ANN** can be an alternative: data structures that sacrifice a bit of accuracy to respond faster. Some families:

- **HNSW** (Hierarchical Navigable Small World) — layered graph, this is what pgvector, Qdrant, and most vector databases use by default. Query in **`O(log N)`**.
- **IVF** (Inverted File Index) — partitions the space into "cells" and searches only in those closest to the query. Query in **`O(√N)`** with typical partitioning.
- **LSH** (Locality-Sensitive Hashing) — hash functions that collide for similar vectors. Query in **`O(N^ρ)`** with `ρ < 1` (sub-linear; depends on the approximation factor).

#### Which distance metric to use for this Rinha de Backend???

You can use **brute force, KNN, ANN, a vector database, a trained AI model, a bunch of IF/ELSE**, or anything else. You'll need to find the balance between vector search accuracy and performance — the strategy is up to you.

---

## Next step

This document uses a simplified example (3 dimensions) just for didactic purposes. The actual challenge specification uses **14 dimensions** — described in [DETECTION_RULES.md](./DETECTION_RULES.md), where you'll also find complete flow examples.
