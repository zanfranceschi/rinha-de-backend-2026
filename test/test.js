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

export const options = {
    summaryTrendStats: ['min', 'med', 'max', 'p(90)', 'p(99)'],
    scenarios: {
        default: {
            executor: 'ramping-arrival-rate',
            startRate: 1,
            timeUnit: '1s',
            preAllocatedVUs: 5,
            maxVUs: 100,
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
    }
}

export function handleSummary(data) {
    const httpDuration = data.metrics.http_req_duration.values;

    const sent = data.metrics.total_sent ? data.metrics.total_sent.values.count : 0;
    const fc = data.metrics.fraud_count ? data.metrics.fraud_count.values.count : 0;
    const lc = data.metrics.legit_count ? data.metrics.legit_count.values.count : 0;
    const httpReqs = data.metrics.http_reqs ? data.metrics.http_reqs.values.count : 0;
    const httpFailed = data.metrics.http_req_failed ? data.metrics.http_req_failed.values : {};

    const result = {
        expected: expectedStats,
        actual: {
            fraud_rate: sent > 0 ? +(fc / sent).toFixed(4) : 0,
            legit_count: lc,
            fraud_count: fc,
            total_requests: sent,
            legit_rate: sent > 0 ? +(lc / sent).toFixed(4) : 0,
            errors: {
                http_req_failed_rate: httpFailed.rate || 0,
                http_req_failed_count: httpFailed.passes || 0,
            },
        },
        response_times: {
            min: httpDuration.min.toFixed(2) + 'ms',
            max: httpDuration.max.toFixed(2) + 'ms',
            med: httpDuration['med'].toFixed(2) + 'ms',
            p90: httpDuration['p(90)'].toFixed(2) + 'ms',
            p99: httpDuration['p(99)'].toFixed(2) + 'ms',
        },
    };

    return {
        'test/results.json': JSON.stringify(result, null, 2),
        //stdout: textSummary(data, { indent: ' ', enableColors: true }),
    };
}
