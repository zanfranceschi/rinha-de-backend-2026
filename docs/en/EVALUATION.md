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

For each request, the `approved: true|false` response is compared and scored with the following:

- **TP (True Positive)** — fraud correctly denied (1 point)
- **TN (True Negative)** — legitimate transaction correctly approved (1 point)
- **FP (False Positive)** — legitimate incorrectly denied (-1 point)
- **FN (False Negative)** — fraud incorrectly approved (-3 points)
- **Error** — HTTP error (-5 points)

## Scoring formula

```
accuracy             = (TP × 1) + (TN × 1) + (FP × -1) + (FN × -3) + (Error × -5)
latency_multiplier   = TARGET_P99_MS / max(p99, TARGET_P99_MS)
final_score          = max(0, accuracy) × latency_multiplier
```
**TARGET_P99_MS = 10ms.**

## Weights — why it's like this

- **FN is worth -3** — letting a fraud through is 3× worse than blocking a legitimate customer (real financial impact)
- **Error is worth -5** — unavailability is the worst of all worlds
- **Latency multiplies the score** — a slow but accurate API loses to a fast and accurate one

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
    "target_p99_ms": 10,
    "latency_multiplier": 1.0,
    "raw_score": 4900,
    "final_score": 4900.00
  }
}
```

- `breakdown` — counts of TP, TN, FP, FN, and HTTP errors.
- `detection_accuracy` — `(TP + TN) / total_classified`. Informational only, not part of the score.
- `latency_multiplier` — `1.0` when `p99 ≤ 10ms`; drops linearly from there (`10/p99`).
- `raw_score` — points for accuracy, before the latency multiplier.
- `final_score` — the number that matters, the final score.


## Strategies (tips)

Some observations that may be useful.

**The latency multiplier is cruel.** `p99 <= 10ms` keeps 100% of the `raw_score`. `p99 = 20ms` cuts it in half. `p99 = 100ms` wipes out 90% of the effort. In general, losing a bit of accuracy to improve latency can pay off.

**HTTP errors cost 5×.** Try never to return HTTP 5xx or blow past timeouts. If your search takes too long for some reason, it's worth **returning any quick response** (e.g., classify as `approved: true` with `fraud_score: 0.0`) rather than erroring — `-1` (FP) or `-3` (FN) hurt less than `-5` (Error).

**When ANN is worth it.** Brute force over 100k vectors × 14 dimensions per query can get very expensive computationally. Adopting ANN (HNSW, IVF) or a ready-made vector database can help. But always measure before complicating things!

**The reference files don't change during the test.** Pre-process freely at startup or during the container build — the more processing you move outside of the test, the better the `p99`.
