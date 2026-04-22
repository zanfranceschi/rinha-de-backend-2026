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

The test dataset comes pre-labeled — for each request, whether it's fraud or not is known ahead of time. By comparing your backend's `approved: true|false` response against the expected one, each request lands in one of five buckets. The first four are the classic binary-classification confusion matrix; the fifth catches cases where your backend didn't even manage to respond properly:

- **TP (True Positive)** — fraud correctly denied
- **TN (True Negative)** — legitimate transaction correctly approved
- **FP (False Positive)** — legitimate incorrectly denied
- **FN (False Negative)** — fraud incorrectly approved
- **Error** — non-200 HTTP response

These five counts, along with the observed latency, feed the formula in the next section.

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

Simple sum. Each component can be negative independently. Reference max is ~6000: `p99_score = 3000` (at p99=1ms) + `detection_score = 3000` (at E=0). `detection_score` has a true ceiling of 3000 (via `ε_MIN`), but `p99_score` does not — p99 below 1ms keeps earning points.

## Weights and parameters — why

- **FN worth -3, Err worth -5** (inside `E`) — same ordering as before: letting a fraud through is 3× worse than blocking a legitimate customer; returning HTTP 5xx is 5× worse.
- **Log over p99** — rewards each order-of-magnitude improvement equally. The gap between 100ms and 10ms is worth the same as 10ms to 1ms. Doesn't saturate: sub-10ms keeps earning points.
- **Two terms in score_det** — the rate term (`K · log₁₀(1/ε)`) is N-invariant (same error rate = same score regardless of test size). The absolute penalty (`−β · log₁₀(1+E)`) punishes real error volume (every missed fraud is a real loss) but grows logarithmically so it doesn't explode with large N.
- **15% failure cutoff** — above that, score_det = −3000, so a broken backend can't slide by on good p99 alone.

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

Some useful observations.

**Log gives exponential bonus for low p99.** Dropping from 10ms to 1ms is worth 1000 extra points in `p99_score`. Chasing each millisecond pays off.

**The 15% cutoff is harsh.** If more than 15% of requests fail (summing FP, FN, and HTTP errors), `detection_score` drops straight to −3000 and wipes out any p99 gain. Avoiding the cutoff zone matters more than tuning accuracy in the last decimals.

**HTTP 500 hit in two places.** In `E` (weight 5 vs FP's 1) and in `failure_rate` (each Err counts as 1 raw failure, equal to FP or FN). If your backend has trouble, **returning any quick response** (e.g., `approved: true`, `fraud_score: 0.0`) lowers HTTP error count, though it raises FP or FN. Mathematically, in the normal regime, `-1` (FP) or `-3` (FN) in the log weight still hurts less than `-5` (Err) plus another point in `failure_rate`.

**Weighted error rate is N-invariant.** You can't "hide" errors by inflating volume — same rate, same `rate_component`. But `absolute_penalty` grows log with the actual error count, so backends failing at large scale lose more points than at small scale.

**When ANN is worth it.** Brute force over 100k vectors × 14 dimensions per query can get very expensive computationally. Adopting ANN (HNSW, IVF) or a ready-made vector database can help. But always measure before complicating things!

**Reference files don't change during the test.** Pre-process freely at startup or during the container build — the more processing you move outside of the test, the better the `p99`.
