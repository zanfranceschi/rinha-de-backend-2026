# Evaluation and scoring

How your submission will be evaluated.

## Load test

The load test uses [k6](https://k6.io/) in a very simple incremental request scenario. The test script is located in [test](/test) along with its data (requests that will be made). It's important to note that the script provided here is for you to run your own tests and may not be the final version of the test :)

Follow the [official instructions](https://grafana.com/docs/k6/latest/) to run the tests.

The instructions for your backend to actually be tested are [described here](/docs/en/SUBMISSION.md) under the **Test Execution** section.

> The script provided here is for you to run your own tests and may not be exactly the final version of the official test, for an additional surprise factor.

## What is tested

The test uses [existing payloads](/test/test-data.json) previously labeled based on the [references](/resources/references.json.gz). This prior labeling was done by applying **k-NN with k=5 and Euclidean distance** over the 14-dimensional vectors. This means that for each request there is a correct expected answer about whether the transaction is fraud or legitimate. But this doesn't force you to use KNN and Euclidean distance for the vector search — you can use other distance metrics (usually at the cost of losing a bit of detection accuracy).

## Collected metrics

The test dataset comes pre-labeled — for each request, whether the transaction is fraud or legitimate is known ahead of time. The test compares your backend's `approved: true|false` response against the expected label and classifies each request into one of the five categories below. The first four form the classic binary-classification confusion matrix; the last covers cases where the backend fails to respond correctly:

- **TP (True Positive)** — fraud correctly denied
- **TN (True Negative)** — legitimate transaction correctly approved
- **FP (False Positive)** — legitimate incorrectly denied
- **FN (False Negative)** — fraud incorrectly approved
- **Error** — non-200 HTTP response

These five counts, together with the observed latency, feed the formula described in the next section.

## Scoring examples

Sometimes it's easier to understand the scoring by looking at concrete cases than by reading the formula. The table below previews 20 scenarios, all with N = 5000 requests, ordered from the best case down to the worst — including both cutoffs (failures > 15% and p99 > 2000ms) and the extremes where they both trigger together. The details of each column are explained in the next sections; for now, it's enough to know that `final_score` is the final score, the sum of a latency score (`p99_score`) and a detection score (`detection_score`).

| false positive detection | false negative detection | HTTP error | failures (detection + HTTP) / total requests | p99      | p99 score | detection score | final score  |
|--------------------------|--------------------------|------------|----------------------------------------------|----------|-----------|-----------------|--------------|
| 0                        | 0                        | 0          | 0.00%                                        | 1ms      | 3000.00   | 3000.00         | **6000.00**  |
| 2                        | 2                        | 0          | 0.08%                                        | 0.5ms    | 3000.00   | 2509.61         | **5509.61**  |
| 5                        | 5                        | 0          | 0.20%                                        | 3ms      | 2522.88   | 2001.27         | **4524.15**  |
| 10                       | 5                        | 0          | 0.30%                                        | 5ms      | 2301.03   | 1876.54         | **4177.57**  |
| 0                        | 0                        | 0          | 0.00%                                        | 100ms    | 1000.00   | 3000.00         | **4000.00**  |
| 30                       | 20                       | 0          | 1.00%                                        | 10ms     | 2000.00   | 1157.02         | **3157.02**  |
| 20                       | 10                       | 5          | 0.70%                                        | 15ms     | 1823.91   | 1259.66         | **3083.57**  |
| 0                        | 0                        | 0          | 0.00%                                        | 1000ms   | 0.00      | 3000.00         | **3000.00**  |
| 80                       | 50                       | 0          | 2.60%                                        | 50ms     | 1301.03   | 628.16          | **1929.19**  |
| 50                       | 30                       | 20         | 2.00%                                        | 80ms     | 1096.91   | 604.15          | **1701.06**  |
| 100                      | 50                       | 0          | 3.00%                                        | 300ms    | 522.88    | 581.13          | **1104.01**  |
| 200                      | 150                      | 50         | 8.00%                                        | 150ms    | 823.91    | −141.69         | **682.22**   |
| 500                      | 250                      | 0          | 15.00%                                       | 200ms    | 698.97    | −327.12         | **371.85**   |
| 0                        | 0                        | 0          | 0.00%                                        | 3000ms   | −3000.00  | 3000.00         | **0.00**     |
| 0                        | 500                      | 1000       | 30.00%                                       | 5ms      | 2301.03   | −3000.00        | **−698.97**  |
| 500                      | 300                      | 0          | 16.00%                                       | 10ms     | 2000.00   | −3000.00        | **−1000.00** |
| 500                      | 300                      | 0          | 16.00%                                       | 50ms     | 1301.03   | −3000.00        | **−1698.97** |
| 0                        | 0                        | 5000       | 100.00%                                      | 500ms    | 301.03    | −3000.00        | **−2698.97** |
| 800                      | 100                      | 0          | 18.00%                                       | 1000ms   | 0.00      | −3000.00        | **−3000.00** |
| 0                        | 0                        | 5000       | 100.00%                                      | 60000ms  | −3000.00  | −3000.00        | **−6000.00** |

Four quick takeaways:

- `p99_score` has a ceiling of +3000 (p99 ≤ 1ms) and a floor of −3000 (p99 > 2000ms). Between the two limits, it grows logarithmically with latency — every 10× faster is worth +1000 points.
- `detection_score` is unconstrained up to a 15% failure rate. Beyond that, it is pinned at −3000. The discontinuity appears between the 15.00% row and the next row with a failure rate above 15%.
- Excellent latency does not offset the failure cutoff: the row with p99 = 5ms and a 30% failure rate ends at `−698.97`, while the row with p99 = 10ms and a 1.00% failure rate ends at `3157.02`. The first is 2× faster and still loses by nearly 4000 points.
- The two cutoffs cancel each other in some corner cases: the row with p99 = 3000ms and zero failures ends at `0` (the p99 cutoff offsets a perfect detection score). When both cutoffs fire (last row), `final_score` hits the absolute floor of −6000.

## Scoring formula

The final score is the sum of two independent components: one for latency (p99) and one for detection quality. Both use a logarithmic function — the idea is to reward each order-of-magnitude improvement equally, rather than absolute differences. Both components have a ceiling of +3000 and a floor of −3000, applied by specific rules described below.

### Latency — `score_p99`

```
If p99 > p99_MAX:
    score_p99 = −3000                          ← cutoff active
Else:
    score_p99 = K · log₁₀(T_max / max(p99, p99_MIN))
```

- `K = 1000`, `T_max = 1000ms`, `p99_MIN = 1ms`, `p99_MAX = 2000ms`.
- Ceiling of +3000: when `p99 ≤ 1ms`, the score saturates at 3000 — improvements beyond that don't add points.
- Floor of −3000: when `p99 > 2000ms`, the score is fixed at −3000.

In practice, within the non-cutoff region, every 10× improvement in latency is worth +1000 points. From 100ms to 10ms: +1000. From 10ms to 1ms: another +1000. Below 1ms, the score saturates at 3000.

### Detection — `score_det`

```
E             = 1·FP + 3·FN + 5·Err           (weighted errors)
ε             = E / N                          (weighted error rate)
failures      = FP + FN + Err                  (raw failure count)
failure_rate  = failures / N

If failure_rate > 15%:
    score_det = −3000                          ← cutoff active
Else:
    score_det = K · log₁₀(1 / max(ε, ε_MIN)) − β · log₁₀(1 + E)
```

- `K = 1000`, `ε_MIN = 0.001`, `β = 300`.
- Weights: `FP = 1`, `FN = 3`, `Err = 5`. HTTP 500 carries the largest weight — unavailability is worse than any detection error.

Outside the cutoff region, the formula is the sum of two terms:

- **Rate term** (`K · log₁₀(1/ε)`) rewards keeping few errors per request. It is invariant to test size: 10 errors out of 10,000 requests yields the same score as 1 error out of 1,000.
- **Absolute penalty** (`−β · log₁₀(1 + E)`) subtracts a small amount for each real error. Every missed fraud is a concrete loss, and that continues to weigh even when the relative rate is low.

When more than 15% of requests fail (counting FP, FN, and Err together), the formula above is not applied and `score_det` is fixed at −3000. This is a hard floor: a backend with a failure rate at that level should not be able to compensate through low p99 alone.

### Final score

```
final_score = score_p99 + score_det
```

Simple sum, no multiplication. The two dimensions are independent, and either one can be negative on its own.

- **Maximum: +6000 points** (+3000 + +3000), with p99 ≤ 1ms and `E = 0`.
- **Minimum: −6000 points** (−3000 − 3000), with p99 > 2000ms and failure rate > 15%.

Both components have a ceiling of +3000 and a floor of −3000, applied via different mechanisms: on the latency side, via `p99_MIN` and `p99_MAX`; on the detection side, via `ε_MIN` and the failure cutoff. This guarantees each component contributes between −3000 and +3000, and the total stays confined to [−6000, +6000].

## Weights and parameters — why

The reasoning behind each choice:

- **FN worth 3, Err worth 5** (inside `E`) — keeps the same order of magnitude as the previous scoring: letting a fraud through is three times worse than blocking a legitimate customer, and returning HTTP 5xx is worse still than any detection error.
- **Log on latency** — rewards each order-of-magnitude improvement equally. Shaving 90ms off a backend at 100ms is worth the same as shaving 9ms off one at 10ms.
- **Ceiling at p99 = 1ms and floor at p99 = 2000ms** — symmetric with the detection limits. Optimizing below 1ms stops earning points (diminishing returns in that range); p99 above 2s is treated as an unviable backend and cuts the score straight to −3000.
- **Rate term + absolute penalty** — the rate is fair across test sizes of different scales; the absolute penalty reinforces that each error represents a real loss. Together, they reward quality in both proportion **and** volume.
- **15% failure cutoff** — the intent is not to apply a proportional penalty, but to nullify the result. A backend with a failure rate at that level should not score points simply by having low p99.

## Interpreting the test results

If you run the test locally, a `results.json` file will be generated. If your test was executed by Rinha's Engine (via opening an issue), the comment with the test result will contain the following JSON:

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

- `expected` — dataset metadata (informational).
- `p99` — observed 99th-percentile latency, in milliseconds. Feeds the `p99_score` computation.
- `breakdown` — raw counts of TP, TN, FP, FN and HTTP errors.
- `failure_rate` — `(FP + FN + Err) / N`. Crosses 15% → detection cutoff triggers.
- `weighted_errors_E` — `1·FP + 3·FN + 5·Err`. Feeds `ε` and the absolute penalty.
- `error_rate_epsilon` — `E / N`. Weighted rate used in the log term.
- `p99_score.value` — final latency score (after cutoff if it triggered).
- `p99_score.cut_triggered` — `true` if `p99 > 2000ms` and the score dropped to −3000.
- `detection_score.value` — final detection score (after cutoff if it triggered).
- `detection_score.rate_component` — just the `K · log₁₀(1/ε)` term. `null` when the cutoff triggered.
- `detection_score.absolute_penalty` — just the `−β · log₁₀(1 + E)` term. `null` when the cutoff triggered.
- `detection_score.cut_triggered` — `true` if `failure_rate > 15%` and the score dropped to −3000.
- `final_score` — `p99_score.value + detection_score.value`. The number that counts.


## Strategies (tips)

Some observations that may be useful.

**The log favors low p99, down to 1ms.** Reducing latency from 10ms to 1ms is worth +1000 points in `p99_score`. Below 1ms, the score saturates at 3000 — optimizing beyond that point doesn't earn additional points.

**The 15% failure cutoff is strict.** If more than 15% of requests fail (summing FP, FN, and HTTP errors), `detection_score` is fixed at −3000 and cancels any p99 gain. Staying away from the cutoff zone matters more than shaving off the last few detection errors.

**The p99 > 2000ms cutoff rarely fires on its own.** The 2s limit exists as a hard floor for the latency score, but in practice it's hard to reach a p99 that high without first accumulating connection errors — and those errors push `failure_rate` above 15%, triggering the detection cutoff first. Treat the p99 cutoff as a backstop, not something you'll commonly see in isolation.

**HTTP 500 has a double impact.** It enters `E` with weight 5 (against FP's 1) and also counts in `failure_rate` (each Err is a raw failure, equivalent to an FP or FN). If something goes wrong in the backend, **returning any quick response** (e.g., `approved: true`, `fraud_score: 0.0`) avoids the HTTP error at the cost of raising FP or FN. In the normal regime, the penalty of `−1` (FP) or `−3` (FN) in the log weight is smaller than `−5` (Err) plus an extra point in `failure_rate`.

**The weighted error rate is N-invariant.** Errors cannot be "diluted" by inflating volume — the same rate produces the same `rate_component`. The `absolute_penalty`, on the other hand, grows logarithmically with the actual error count; backends failing at large scale lose more points than those failing at small scale.

**When ANN is worth it.** Brute force over 100k vectors × 14 dimensions per query can get very expensive computationally. Adopting ANN (HNSW, IVF) or a ready-made vector database can help. But always measure before complicating things!

**Reference files don't change during the test.** Pre-process freely at startup or during the container build — the more processing you move outside of the test, the better the `p99`.
