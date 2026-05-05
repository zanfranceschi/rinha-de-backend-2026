# Evaluation and scoring

How your submission will be evaluated.

## Load test

The load test uses [k6](https://k6.io/) in a simple incremental request scenario. The test script is in [test](/test), along with the test data (the requests that will be made). The script published here is for you to run your own tests and may not be identical to the final version used in the official evaluation.

To run the test, follow the [official k6 instructions](https://grafana.com/docs/k6/latest/).

The instructions for actually having your backend tested are [described here](/docs/en/SUBMISSION.md) in the **Test Execution** section.

## What is tested

The test uses [payloads](/test/test-data.json) already labeled based on the [references](/resources/references.json.gz). The labeling was done by applying **k-NN with k=5 and Euclidean distance with brute force** – exact search – over the 14-dimensional vectors. That is, for each request there is an expected answer (fraud or legitimate). This does not force you to use the same technique – using brute force will likely have very poor performance (*O(N * 14)*) for this challenge.

## Collected metrics

The test dataset comes pre-labeled — for each request, whether the transaction is fraud or legitimate is known in advance. The test compares your backend's response (`approved: true|false`) with the expected label and classifies each response into one of the five categories below. The first four form the classic binary-classification confusion matrix; the last covers the case where your backend does not respond successfully:

- **TP (True Positive)** — fraud correctly denied.
- **TN (True Negative)** — legitimate transaction correctly approved.
- **FP (False Positive)** — legitimate transaction incorrectly denied.
- **FN (False Negative)** — fraud incorrectly approved.
- **Error** — HTTP error (response other than 200).

These five counts, together with the observed latency, feed the formula described in the next section.

## Scoring examples

In some cases it is easier to understand the scoring by looking at examples than at the formula itself. The table below shows a few representative scenarios, all with N = 5000 requests, ordered from best to worst — including the two cutoffs (more than 15% failures and p99 above 2000ms) and the extreme where both trigger together. The details of each column are explained in the following sections; for now, it is enough to know that `final_score` is the final score, the sum of a latency score (`p99_score`) and a detection score (`detection_score`).

| false positive detection | false negative detection | HTTP error | failures (detection + HTTP) / total requests | p99     | p99 score | detection score | final score  |
|--------------------------|--------------------------|------------|----------------------------------------------|---------|-----------|-----------------|--------------|
| 0                        | 0                        | 0          | 0.00%                                        | 1ms     | 3000.00   | 3000.00         | **6000.00**  |
| 5                        | 5                        | 0          | 0.20%                                        | 3ms     | 2522.88   | 2001.27         | **4524.15**  |
| 0                        | 0                        | 0          | 0.00%                                        | 100ms   | 1000.00   | 3000.00         | **4000.00**  |
| 30                       | 20                       | 0          | 1.00%                                        | 10ms    | 2000.00   | 1157.02         | **3157.02**  |
| 100                      | 50                       | 0          | 3.00%                                        | 300ms   | 522.88    | 581.13          | **1104.01**  |
| 500                      | 250                      | 0          | 15.00%                                       | 200ms   | 698.97    | −327.12         | **371.85**   |
| 500                      | 300                      | 0          | 16.00%                                       | 10ms    | 2000.00   | −3000.00        | **−1000.00** |
| 0                        | 0                        | 5000       | 100.00%                                      | 60000ms | −3000.00  | −3000.00        | **−6000.00** |

A few readings the table makes clear:

- `p99_score` has a ceiling of 3000 (when p99 is less than or equal to 1ms) and a floor of −3000 (when p99 goes above 2000ms). Between the two limits, it grows on a logarithmic scale — every 10× faster earns another 1000 points.
- `detection_score` is free while the failure rate stays at 15% or below. Above that, it is fixed at −3000.
- Even a very good p99 does not offset the failure cutoff: with p99 of 10ms and 16% failures, the final score is −1000; but with p99 of 10ms and 1% failures, the final score is 3157.02.
- When both cutoffs trigger together (last row), `final_score` hits the absolute floor of −6000.

## Scoring formula

The final score is the sum of two independent components: one for latency (p99) and one for detection quality. Both use a logarithmic function — the idea is to reward each order of magnitude of improvement in the same measure, instead of looking at absolute differences. Both components have a ceiling of +3000 and a floor of −3000, applied by the specific rules described below.

### Latency — `score_p99`

```
If p99 > p99_MAX:
    score_p99 = −3000                          ← cutoff active
Else:
    score_p99 = K · log₁₀(T_max / max(p99, p99_MIN))
```

- `K = 1000`, `T_max = 1000ms`, `p99_MIN = 1ms`, `p99_MAX = 2000ms`.
- Ceiling of +3000: when `p99 ≤ 1ms`, the score saturates at 3000 — improvements below that do not add points.
- Floor of −3000: when `p99 > 2000ms`, the score is fixed at −3000.

*Note: The HTTP requests in the test have a timeout of 2001ms.*

In practice, within the non-cutoff range, every 10× improvement in latency is worth another 1000 points. From 100ms to 10ms: another 1000. From 10ms to 1ms: another 1000. Below 1ms, the score saturates at 3000.

### Detection — `score_det`

```
E             = 1·FP + 3·FN + 5·Err            (weighted errors)
ε             = E / N                           (weighted rate)
failures      = FP + FN + Err                   (raw count)
failure_rate  = failures / N

If failure_rate > 15%:
    score_det = −3000                          ← cutoff active
Else:
    score_det = K · log₁₀(1 / max(ε, ε_MIN)) − β · log₁₀(1 + E)
```

- `K = 1000`, `ε_MIN = 0.001`, `β = 300`.
- Weights: `FP = 1`, `FN = 3`, `Err = 5`. HTTP 500 has the largest weight — unavailability is worse than any detection error.

Outside the cutoff region, the formula is the sum of two terms:

- **Rate term** (`K · log₁₀(1/ε)`) — rewards keeping few errors per request. It does not depend on the size of the test: 10 errors out of 10,000 requests yields the same score as 1 error out of 1,000.
- **Absolute penalty** (`−β · log₁₀(1 + E)`) — subtracts a small amount for each real error. Each fraud that gets through is a concrete loss, and that continues to weigh even when the relative rate is low.

When more than 15% of requests fail (summing FP, FN, and Err), the formula above is not applied and `score_det` is fixed at −3000. It is a hard floor: a backend with a failure rate above that threshold cannot offset the result with low p99 alone.

### Final score

```
final_score = score_p99 + score_det
```

Direct sum, no multiplication. The two dimensions are independent, and either one can be negative on its own.

- **Maximum: +6000 points** (+3000 + +3000), with p99 less than or equal to 1ms and `E = 0`.
- **Minimum: −6000 points** (−3000 − 3000), with p99 above 2000ms and failure rate above 15%.

Both components have a ceiling of +3000 and a floor of −3000, applied by different mechanisms: on the latency side, via `p99_MIN` and `p99_MAX`; on the detection side, via `ε_MIN` and the failure cutoff. This way, each component contributes between −3000 and +3000, and the total stays in the [−6000, +6000] range.

## Why these weights and parameters

The reasoning behind each choice:

- **FN worth 3 and Err worth 5** (in `E`) — keeps the same order of magnitude as the previous scoring. Letting a fraud through is three times worse than blocking a legitimate customer, and returning HTTP 5xx is worse still than any detection error.
- **Log on latency** — rewards each order of magnitude of improvement in the same measure. Shaving 90ms off a backend at 100ms is worth the same as shaving 9ms off one at 10ms.
- **Ceiling at p99 = 1ms and floor at p99 = 2000ms** — symmetric with the detection limits. Optimizing below 1ms stops earning points (diminishing returns in that range); p99 above 2s is treated as an unviable backend and cuts the score straight to −3000.
- **Rate term + absolute penalty** — the rate is fair across tests of different sizes; the absolute penalty reinforces that each error represents a real loss. Together, they reward quality in proportion **and** in volume.
- **15% failure cutoff** — the goal is not to apply a proportional penalty, but to nullify the result. A backend with a failure rate at that level cannot score points simply by having low p99.

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
- `breakdown` — raw counts of TP, TN, FP, FN, and HTTP errors.
- `failure_rate` — `(FP + FN + Err) / N`. If it goes above 15%, the detection cutoff triggers.
- `weighted_errors_E` — `1·FP + 3·FN + 5·Err`. Feeds the `ε` calculation and the absolute penalty.
- `error_rate_epsilon` — `E / N`. The weighted rate that feeds the logarithmic term.
- `p99_score.value` — final latency score (after the cutoff, if it triggered).
- `p99_score.cut_triggered` — `true` if `p99 > 2000ms` and the score dropped to −3000.
- `detection_score.value` — final detection score (after the cutoff, if it triggered).
- `detection_score.rate_component` — just the `K · log₁₀(1/ε)` term. Becomes `null` when the cutoff triggers.
- `detection_score.absolute_penalty` — just the `−β · log₁₀(1 + E)` term. Becomes `null` when the cutoff triggers.
- `detection_score.cut_triggered` — `true` if `failure_rate > 15%` and the score dropped to −3000.
- `final_score` — `p99_score.value + detection_score.value`. Your backend's final score.


## Strategies and tips

A few observations that may be useful.

**The log favors low p99, down to 1ms.** Reducing latency from 10ms to 1ms earns another 1000 points in `p99_score`. Below 1ms, the score saturates at 3000 — optimizing past that point does not earn additional points.

**The 15% failure cutoff is strict.** If more than 15% of requests fail (summing FP, FN, and HTTP errors), `detection_score` is fixed at −3000 and cancels any gain obtained on p99. Staying away from the cutoff zone tends to be more important than minimizing the last few detection errors.

**The p99 > 2000ms cutoff rarely triggers on its own.** The 2s limit exists as a hard floor for the latency score, but in practice it is hard to reach a p99 that high without first accumulating connection errors — and those errors already push `failure_rate` above 15%, triggering the detection cutoff first. Think of the p99 cutoff as a safety net, not as something you will commonly see in isolation.

**HTTP 500 has a double impact.** It enters `E` with weight 5 (against 1 for an FP) and also counts in `failure_rate` (each Err is a raw failure, like an FP or FN). If something goes wrong in your backend, returning any quick response (for example, `approved: true`, `fraud_score: 0.0`) avoids the HTTP error at the cost of raising FP or FN. Under the normal regime, a penalty of −1 (FP) or −3 (FN) in the logarithmic weight is smaller than −5 (Err) plus one more point in `failure_rate`.

**The weighted error rate does not depend on the test size.** You cannot "dilute" errors by increasing the volume — the same rate results in the same `rate_component`. The `absolute_penalty`, on the other hand, grows on a logarithmic scale with the actual error volume; backends that fail at large scale lose more points than those that fail at small scale.

**When ANN is worth it.** Brute force over 3,000,000 vectors with 14 dimensions per query can get computationally expensive. Adopting ANN (HNSW, IVF) or even VP Tree, which is an exact search that does not use brute force, can help. But always measure before complicating things.

**The reference files do not change during the test.** You can (and probably should) pre-process the reference file with the 3 million vectors freely at startup or during the container build — the more processing you move outside of runtime, the better your `p99` tends to be.
