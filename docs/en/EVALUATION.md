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

## Scoring formula

The final score is the sum of two independent components: one for latency (p99) and one for detection quality. Both use a logarithmic function — the idea is to reward each order-of-magnitude improvement equally, rather than absolute differences. The detection component has one additional rule: if the failure rate exceeds 15%, its value is fixed at −3000, which fully offsets the best possible latency score.

### Latency — `score_p99`

```
score_p99 = K · log₁₀(T_max / p99)
```

- `K = 1000`, `T_max = 1000ms`.
- No cap, no floor: low p99 keeps accumulating points; p99 above 1000ms produces a negative score.

In practice, every 10× improvement in latency is worth +1000 points. From 100ms to 10ms: +1000. From 10ms to 1ms: another +1000. Unlike the previous formula, there is no target after which improvements stop counting — the score keeps growing as latency decreases.

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

Reference max: **~6000 points** (3000 + 3000), with p99 near 1ms and `E = 0`. `score_det` has a fixed ceiling of 3000 (imposed by `ε_MIN`); `score_p99` does not — the lower the p99, the more points, without an upper bound.

## Weights and parameters — why

The reasoning behind each choice:

- **FN worth 3, Err worth 5** (inside `E`) — keeps the same order of magnitude as the previous scoring: letting a fraud through is three times worse than blocking a legitimate customer, and returning HTTP 5xx is worse still than any detection error.
- **Log on latency** — rewards each order-of-magnitude improvement equally. Shaving 90ms off a backend at 100ms is worth the same as shaving 9ms off one at 10ms. Unlike the previous formula, it **does not saturate**: p99 below 10ms keeps earning points.
- **Rate term + absolute penalty** — the rate is fair across test sizes of different scales; the absolute penalty reinforces that each error represents a real loss. Together, they reward quality in both proportion **and** volume.
- **15% failure cutoff** — the intent is not to apply a proportional penalty, but to nullify the result. A backend with a failure rate at that level should not score points simply by having low p99.

## Interpreting the test results

If you run the test locally, a `results.json` file will be generated. If your test was executed by Rinha's Engine (via opening an issue), the comment with the test result will contain the following JSON:

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

- `breakdown` — raw counts of TP, TN, FP, FN and HTTP errors.
- `detection_accuracy` — `(TP + TN) / (TP + TN + FP + FN)`. Informational only.
- `failure_rate` — `(FP + FN + Err) / N`. Crosses 15% → cutoff triggers.
- `weighted_errors_E` — `1·FP + 3·FN + 5·Err`. Feeds `ε` and the absolute penalty.
- `error_rate_epsilon` — `E / N`. Weighted rate used in the log term.
- `p99_score` — `K · log₁₀(T_max / p99)`.
- `detection_score.value` — final score_det (after cutoff if it triggered).
- `detection_score.rate_component` — just the `K · log₁₀(1/ε)` term. `null` when the cutoff triggered.
- `detection_score.absolute_penalty` — just the `−β · log₁₀(1 + E)` term. `null` when the cutoff triggered.
- `detection_score.cut_triggered` — `true` if `failure_rate > 15%` and the score dropped to −3000.
- `final_score` — `p99_score + detection_score.value`. The number that counts.


## Strategies (tips)

Some observations that may be useful.

**The log favors very low p99.** Reducing latency from 10ms to 1ms is worth +1000 points in `p99_score`. Every millisecond below that keeps counting — there is no saturation target.

**The 15% cutoff is strict.** If more than 15% of requests fail (summing FP, FN, and HTTP errors), `detection_score` is fixed at −3000 and cancels any p99 gain. Staying away from the cutoff zone matters more than tuning the last decimals of accuracy.

**HTTP 500 has a double impact.** It enters `E` with weight 5 (against FP's 1) and also counts in `failure_rate` (each Err is a raw failure, equivalent to an FP or FN). If something goes wrong in the backend, **returning any quick response** (e.g., `approved: true`, `fraud_score: 0.0`) avoids the HTTP error at the cost of raising FP or FN. In the normal regime, the penalty of `−1` (FP) or `−3` (FN) in the log weight is smaller than `−5` (Err) plus an extra point in `failure_rate`.

**The weighted error rate is N-invariant.** Errors cannot be "diluted" by inflating volume — the same rate produces the same `rate_component`. The `absolute_penalty`, on the other hand, grows logarithmically with the actual error count; backends failing at large scale lose more points than those failing at small scale.

**When ANN is worth it.** Brute force over 100k vectors × 14 dimensions per query can get very expensive computationally. Adopting ANN (HNSW, IVF) or a ready-made vector database can help. But always measure before complicating things!

**Reference files don't change during the test.** Pre-process freely at startup or during the container build — the more processing you move outside of the test, the better the `p99`.
