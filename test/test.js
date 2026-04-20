import http from 'k6/http';
import { check } from 'k6';
import { SharedArray } from 'k6/data';
import { Counter } from 'k6/metrics';
import { textSummary } from 'https://jslib.k6.io/k6-summary/0.0.1/index.js';
import exec from 'k6/execution';

const testFile = JSON.parse(open('./test-data.json'));
const expectedStats = testFile.stats;

const testData = new SharedArray('test-data', function () {
    return testFile.entries;
});

const tpCount = new Counter('tp_count');
const tnCount = new Counter('tn_count');
const fpCount = new Counter('fp_count');
const fnCount = new Counter('fn_count');
const errorCount = new Counter('error_count');

export const options = {
    summaryTrendStats: ['min', 'med', 'max', 'p(90)', 'p(99)'],
    scenarios: {
        default: {
            executor: 'ramping-arrival-rate',
            startRate: 1,
            timeUnit: '1s',
            preAllocatedVUs: 5,
            maxVUs: 150,
            gracefulStop: '10s',
            stages: [
                { duration: '10s', target: 10 },
                { duration: '10s', target: 50 },
                { duration: '20s', target: 350 },
                { duration: '20s', target: 650 },
            ],
        },
    },
};

export function setup() {
    console.log(
        `Dataset: ${expectedStats.total} entries, `
        + `${expectedStats.fraud_count} fraud (${expectedStats.fraud_rate}%), `
        + `${expectedStats.legit_count} legit (${expectedStats.legit_rate}%), `
        + `edge cases: ${expectedStats.edge_case_rate}%`
    );
}

export default function () {
    const idx = exec.scenario.iterationInTest;
    if (idx >= testData.length) return;
    const entry = testData[idx];
    const expected = entry.info.expected_response;

    const res = http.post(
        'http://localhost:9999/fraud-score',
        JSON.stringify(entry.request),
        { headers: { 'Content-Type': 'application/json' } }
    );

    if (res.status === 200) {
        const body = JSON.parse(res.body);
        // Per-request scoring: compare against expected.approved
        // expected.approved === true  --> legit transaction
        // expected.approved === false --> fraud transaction
        if (expected.approved === body.approved) {
            if (body.approved) tnCount.add(1); // correctly approved legit
            else tpCount.add(1);               // correctly denied fraud
        } else {
            if (body.approved) fnCount.add(1); // fraud approved (missed fraud)
            else fpCount.add(1);               // legit denied (false block)
        }
    } else {
        errorCount.add(1);
    }
}

export function handleSummary(data) {
    const httpDuration = data.metrics.http_req_duration.values;

    const tp = data.metrics.tp_count ? data.metrics.tp_count.values.count : 0;
    const tn = data.metrics.tn_count ? data.metrics.tn_count.values.count : 0;
    const fp = data.metrics.fp_count ? data.metrics.fp_count.values.count : 0;
    const fn = data.metrics.fn_count ? data.metrics.fn_count.values.count : 0;
    const errs = data.metrics.error_count ? data.metrics.error_count.values.count : 0;

    // Scoring formula:
    //   +1 per correct decision (TP or TN)
    //   -1 per false positive (legit blocked)
    //   -3 per false negative (fraud approved)
    //   -5 per HTTP error / non-200
    // Then multiplied by latency factor based on p99.
    const TARGET_P99_MS = 10;
    const rawScore = (tp * 1) + (tn * 1) + (fp * -1) + (fn * -3) + (errs * -5);
    const p99 = httpDuration['p(99)'];
    const latencyMult = TARGET_P99_MS / Math.max(p99, TARGET_P99_MS);
    const finalScore = Math.max(0, rawScore) * latencyMult;

    const classified = tp + tn + fp + fn;
    const accuracy = classified > 0 ? (tp + tn) / classified : 0;

    const result = {
        expected: expectedStats,
        response_times: {
            min: httpDuration.min.toFixed(2) + 'ms',
            max: httpDuration.max.toFixed(2) + 'ms',
            med: httpDuration['med'].toFixed(2) + 'ms',
            p90: httpDuration['p(90)'].toFixed(2) + 'ms',
            p99: httpDuration['p(99)'].toFixed(2) + 'ms',
        },
        scoring: {
            breakdown : {
                false_positive_detections: fp,
                false_negative_detections: fn,
                true_positive_detections: tp,
                true_negative_detections: tn,
                p99: httpDuration['p(99)'].toFixed(2) + 'ms',
                http_errors: errs,
            },
            accuracy: +(accuracy * 100).toFixed(2) + '%',
            target_p99_ms: TARGET_P99_MS,
            latency_multiplier: +latencyMult.toFixed(4),
            raw_score: rawScore,
            final_score: +finalScore.toFixed(2),
        },
    };

    return {
        'test/results.json': JSON.stringify(result, null, 2),
        //stdout: textSummary(data, { indent: ' ', enableColors: true }),
    };
}
