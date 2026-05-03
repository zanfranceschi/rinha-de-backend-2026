# Vector search — an introduction

This document walks through, with an example, how a **vector search** works in the context of fraud detection for this edition of Rinha.

## What is a vector search?

A vector search finds what is most similar instead of looking for exact equality. It is a **similarity search**.

Each transaction is represented by a **vector** — a list of numbers describing its characteristics (amount, time of day, cardholder's average, and so on). Similar transactions have similar vectors, and stay "close" to each other in space.

A vector search answers the question:

> *"Given this new transaction, which transactions in my history are closest to it?"*

If the most similar past transactions were classified as fraud, the new one probably is too. If they were legitimate, it is probably legitimate.

### Where else does this appear?

The same technique is behind several everyday applications:

- **Recommendation systems** — Spotify, Netflix, and Amazon suggest songs, movies, and products by finding items whose vectors are similar to those you have already consumed.
- **Semantic search and RAG** — instead of matching exact words, systems like ChatGPT compare the vector (an *embedding*, a numeric representation of text) of your question against document vectors to find the most relevant excerpts.
- **Image search** — Google Images and Pinterest convert photos into vectors and compare them visually.
- **Facial recognition** — each face becomes a vector; identifying a person means finding the closest vector in a database of known faces.
- **Plagiarism and duplicate detection** — comparing a text's vector against a corpus to find similar content, even when it has been rewritten.
- **Fraud and anomaly detection** — the case of this Rinha: comparing the "shape" of a transaction against a classified history.

In all of these cases the idea is the same: represent "things" as vectors and use proximity in space as a measure of similarity.

---

## Step-by-step example

You can follow a transaction from start to finish, from the received payload to the decision.

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

> The `clamp(x)` function restricts a value to the interval `[0.0, 1.0]`. Look at `dim1`: this transaction's `amount` (`12,500`) exceeds the ceiling `max_amount = 10,000`, and the division yields `1.25`, which is outside the interval. `clamp()` caps it at `1.0`. Without this protection, the vector would leave the normalized space and distort the entire vector search.

**Resulting query vector:**

```
[1.0, 0.96, 0.96]
```

### 4. Vector search — compute the distance to each reference

Using Euclidean distance with 3 dimensions, the formula is:

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

### 5. The K=3 nearest neighbors (KNN with K=3)

You then select the `3` nearest neighbors.

```
1st — ref 4 (fraud) — 0.064
2nd — ref 2 (fraud) — 0.425
3rd — ref 5 (fraud) — 0.595
```

### 6. Majority vote

The vote is the ratio between fraud and legitimate references. In this case, the 3 closest transactions are labeled as fraudulent.

```
fraud: 3 votes
legit: 0 votes

fraud_score = 3 / 3 = 1.0
```

### 7. Decision

Not all `K` nearest transactions need to be labeled as fraud for the transaction being processed to also be marked as fraud. You might decide, for example, that if two-thirds of the references are fraud, the transaction is also marked as fraud. This cutoff is called the `threshold`. Using a threshold of `0.6`:

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

The example above shows a transaction whose 3 nearest references were labeled as fraud. This means that the "shape" of the transaction, based on the references, looks most like a fraud. The three nearest neighbors share in common *high amount, late hour, and high-end cardholder*.

The vector search **does not "understand" fraud** — it just finds the most similar past transactions and lets the majority (voting) or weighted proximity decide the label of the new one.

In Machine Learning terms, this is called **non-parametric supervised learning** (also known as *instance-based learning*): there is no trained model, just the memorized dataset and a search at query time.

---

## Distance metrics and search algorithms

The example above uses **Euclidean distance**, but that is only one of the options for measuring "how similar two vectors are". The most common ones are:

- **Euclidean** — $\sqrt{\sum_i (q_i - r_i)^2}$. The "straight-line distance" in space. Intuitive and the most common starting point.
- **Manhattan** (L1) — $\sum_i |q_i - r_i|$. Sum of absolute differences. Cheaper to compute (no square root or multiplication) and penalizes outliers more softly.
- **Cosine** — compares the **angle** between vectors, not their magnitude. Useful when you care about the "direction" of the vector more than its size (texts, embeddings, and similar cases).

### Exact KNN vs ANN (Approximate Nearest Neighbors)

The simplest way to find the K nearest neighbors is **exact KNN via brute force**: iterate through all references, calculate the distance to each one, and sort. It works, but it costs O(N * D) (D = vector dimensions) per query. With 3M references and a tight latency budget, it can become too expensive.

**ANN** is an alternative: data structures that give up a bit of accuracy to respond faster. Some families:

- **HNSW** (Hierarchical Navigable Small World) — a layered graph; this is what pgvector, Qdrant, and most vector databases use by default. Query in **`O(log N)`**.
- **IVF** (Inverted File Index) — partitions the space into "cells" and searches only in the ones closest to the query. Query in **`O(√N)`** with typical partitioning.
- **LSH** (Locality-Sensitive Hashing) — hash functions that collide for similar vectors. Query in **`O(N^ρ)`** with `ρ < 1` (sub-linear; depends on the approximation factor).

There are also other ways to perform **exact search** without brute force:

- **KD-Tree** (K-Dimensional Tree) – Splits the space using orthogonal hyperplanes. At each level, it picks an axis and splits the points by the median.
- **VP-Tree** (Vantage Point Tree) – Splits the space using spherical distances. It picks a fixed point and separates the rest between "inside" or "outside" a circular radius.
- **Ball Tree** – Groups points into nested hyperspheres. Unlike KD-Tree, the regions (balls) can overlap, but they are more efficient in slightly higher dimensions.
- **Cover Tree** – Organizes the data in a hierarchy of levels based on distance scales (usually powers of 2).
- **BK-Tree** (Burkhard-Keller Tree) – Specialized for discrete distances (integer values). Organizes nodes based on the exact distance from each child to the parent.


#### Which approach to use for this Rinha de Backend?

You can use **brute force, KNN, ANN, a vector database, a trained AI model, a pile of IF/ELSE**, or anything else. You will need to find the balance between vector search accuracy and performance — the strategy is up to you.

---

## Next step

This document uses a simplified example (3 dimensions) for didactic purposes. The actual challenge specification uses **14 dimensions** — described in [DETECTION_RULES.md](./DETECTION_RULES.md), where you will also find complete flow examples.
