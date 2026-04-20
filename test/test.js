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

const totalSent = new Counter('total_sent');
const fraudCount = new Counter('fraud_count');
const legitCount = new Counter('legit_count');
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

    totalSent.add(1);

    const res = http.post(
        'http://localhost:9999/fraud-score',
        JSON.stringify(entry.request),
        { headers: { 'Content-Type': 'application/json' } }
    );

    if (res.status === 200) {
        const body = JSON.parse(res.body);
        if (body.approved) {
            legitCount.add(1);
        } else {
            fraudCount.add(1);
        }

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

    const sent = data.metrics.total_sent ? data.metrics.total_sent.values.count : 0;
    const fc = data.metrics.fraud_count ? data.metrics.fraud_count.values.count : 0;
    const lc = data.metrics.legit_count ? data.metrics.legit_count.values.count : 0;
    const httpReqs = data.metrics.http_reqs ? data.metrics.http_reqs.values.count : 0;
    const httpFailed = data.metrics.http_req_failed ? data.metrics.http_req_failed.values : {};

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
    const rawScore = tp + tn - fp - 3 * fn - 5 * errs;
    const p99 = httpDuration['p(99)'];
    const latencyMult = TARGET_P99_MS / Math.max(p99, TARGET_P99_MS);
    const finalScore = Math.max(0, rawScore) * latencyMult;

    const actualFraudRate = sent > 0 ? +(fc / sent).toFixed(4) : 0;
    const actualLegitRate = sent > 0 ? +(lc / sent).toFixed(4) : 0;

    const result = {
        expected: expectedStats,
        actual: {
            fraud_rate: actualFraudRate,
            legit_count: lc,
            fraud_count: fc,
            total_requests: sent,
            legit_rate: actualLegitRate,
            errors: {
                http_req_failed_rate: httpFailed.rate || 0,
                http_req_failed_count: httpFailed.passes || 0,
            },
        },
        scoring: {
            response_times_min: httpDuration.min.toFixed(2) + 'ms',
            response_times_max: httpDuration.max.toFixed(2) + 'ms',
            response_times_med: httpDuration['med'].toFixed(2) + 'ms',
            response_times_p90: httpDuration['p(90)'].toFixed(2) + 'ms',
            response_times_p99: httpDuration['p(99)'].toFixed(2) + 'ms',
            fraud_rate_error_percent: +(Math.abs(expectedStats.fraud_rate - actualFraudRate) * 100).toFixed(2) + '%',
            confusion: { tp, tn, fp, fn, errors: errs },
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
