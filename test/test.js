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
    summaryTrendStats: ['p(99)'],
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

function renderSummaryTable(s) {
    const WIDTH = 62;
    const LABEL_W = 42;
    const VALUE_W = WIDTH - LABEL_W - 5; // borders + separator

    const top    = '┌' + '─'.repeat(WIDTH - 2) + '┐';
    const sep    = '├' + '─'.repeat(LABEL_W) + '┬' + '─'.repeat(VALUE_W) + '┤';
    const midRow = '├' + '─'.repeat(LABEL_W) + '┼' + '─'.repeat(VALUE_W) + '┤';
    const bot    = '└' + '─'.repeat(LABEL_W) + '┴' + '─'.repeat(VALUE_W) + '┘';

    const header = (text) => '│' + center(text, WIDTH - 2) + '│';
    const section = (text) => '│' + padRight(' ' + text, LABEL_W) + '┤' + ' '.repeat(VALUE_W) + '│';
    const row = (label, value) =>
        '│' + padRight('  ' + label, LABEL_W) + '│' + padLeft(String(value) + ' ', VALUE_W) + '│';

    const fmtNum = (n, d = 2) => (Number.isFinite(n) ? n.toFixed(d) : '—');
    const yn = (b) => (b ? 'YES' : 'no');

    const lines = [
        top,
        header('FRAUD-SCORE — TEST RESULT'),
        sep,
        row('FINAL SCORE', fmtNum(s.finalScore)),
        midRow,

        section('P99 latency score'),
        midRow,
        row('score', fmtNum(s.p99Score)),
        row('p99', fmtNum(s.p99) + ' ms'),
        row('cut triggered (p99 > 2000ms)', yn(s.p99CutTriggered)),
        midRow,

        section('Detection score'),
        midRow,
        row('score', fmtNum(s.detScore)),
        row('rate component', s.cutTriggered ? '—' : fmtNum(s.rateComponent)),
        row('absolute penalty', s.cutTriggered ? '—' : fmtNum(s.absolutePenalty)),
        row('cut triggered (failures > 15%)', yn(s.cutTriggered)),
        midRow,

        section('Detections'),
        midRow,
        row('true positive  (fraud denied)', s.tp),
        row('true negative  (legit approved)', s.tn),
        row('false positive (legit denied)', s.fp),
        row('false negative (fraud approved)', s.fn),
        row('http errors', s.errs),
        midRow,

        section('Stats'),
        midRow,
        row('total requests (N)', s.N),
        row('failure rate', (s.failureRate * 100).toFixed(2) + ' %'),
        row('weighted errors (E)', s.E),
        row('error rate (ε = E/N)', fmtNum(s.epsilon, 6)),
        bot,
    ];

    return lines.join('\n');
}

function padRight(str, width) {
    const s = String(str);
    return s.length >= width ? s.slice(0, width) : s + ' '.repeat(width - s.length);
}

function padLeft(str, width) {
    const s = String(str);
    return s.length >= width ? s.slice(-width) : ' '.repeat(width - s.length) + s;
}

function center(str, width) {
    const s = String(str);
    if (s.length >= width) return s.slice(0, width);
    const left = Math.floor((width - s.length) / 2);
    const right = width - s.length - left;
    return ' '.repeat(left) + s + ' '.repeat(right);
}

export function handleSummary(data) {

    const K = 1000;
    const T_MAX_MS = 1000;
    const P99_MIN_MS = 1;
    const P99_MAX_MS = 2000;
    const EPSILON_MIN = 0.001;
    const BETA = 300;
    const TX_CORTE = 0.15;
    const SCORE_P99_CORTE = -3000;
    const SCORE_DET_CORTE = -3000;

    const httpDuration = data.metrics.http_req_duration.values;
    const p99 = httpDuration['p(99)'];

    const tp = data.metrics.tp_count ? data.metrics.tp_count.values.count : 0;
    const tn = data.metrics.tn_count ? data.metrics.tn_count.values.count : 0;
    const fp = data.metrics.fp_count ? data.metrics.fp_count.values.count : 0;
    const fn = data.metrics.fn_count ? data.metrics.fn_count.values.count : 0;
    const errs = data.metrics.error_count ? data.metrics.error_count.values.count : 0;

    const N = tp + tn + fp + fn + errs;

    // Erros ponderados (para a fórmula log) e contagem pura (para o corte)
    const E = (fp * 1) + (fn * 3) + (errs * 5);
    const failures = fp + fn + errs;
    const epsilon = N > 0 ? E / N : 0;
    const failureRate = N > 0 ? failures / N : 0;

    // Score P99 (log, com teto em P99_MIN_MS e corte em P99_MAX_MS).
    // p99=0 = nenhuma resposta completou; retorna 0 pra evitar Infinity no JSON.
    let p99Score;
    let p99CutTriggered = false;
    if (p99 <= 0) {
        p99Score = 0;
    } else if (p99 > P99_MAX_MS) {
        p99Score = SCORE_P99_CORTE;
        p99CutTriggered = true;
    } else {
        p99Score = K * Math.log10(T_MAX_MS / Math.max(p99, P99_MIN_MS));
    }

    // Score detecção (log com penalidade absoluta, ou corte em -3000 se falhas > 15%)
    let detScore;
    let rateComponent = 0;
    let absolutePenalty = 0;
    let cutTriggered = false;
    if (failureRate > TX_CORTE) {
        detScore = SCORE_DET_CORTE;
        cutTriggered = true;
    } else {
        rateComponent = K * Math.log10(1 / Math.max(epsilon, EPSILON_MIN));
        absolutePenalty = -BETA * Math.log10(1 + E);
        detScore = rateComponent + absolutePenalty;
    }

    const finalScore = p99Score + detScore;

    const summaryTable = renderSummaryTable({
        finalScore,
        p99,
        p99Score,
        p99CutTriggered,
        detScore,
        rateComponent,
        absolutePenalty,
        cutTriggered,
        tp, tn, fp, fn, errs,
        N,
        failureRate,
        E,
        epsilon,
    });

    const result = {
        expected: expectedStats,
        p99: p99.toFixed(2) + 'ms',
        scoring: {
            breakdown: {
                false_positive_detections: fp,
                false_negative_detections: fn,
                true_positive_detections: tp,
                true_negative_detections: tn,
                http_errors: errs,
            },
            failure_rate: +(failureRate * 100).toFixed(2) + '%',
            weighted_errors_E: E,
            error_rate_epsilon: +epsilon.toFixed(6),
            p99_score: {
                value: +p99Score.toFixed(2),
                cut_triggered: p99CutTriggered,
            },
            detection_score: {
                value: +detScore.toFixed(2),
                rate_component: cutTriggered ? null : +rateComponent.toFixed(2),
                absolute_penalty: cutTriggered ? null : +absolutePenalty.toFixed(2),
                cut_triggered: cutTriggered,
            },
            final_score: +finalScore.toFixed(2),
            final_score_table: summaryTable,
        },
    };

    return {
        'test/results.json': JSON.stringify(result, null, 2),
        //stdout: textSummary(data, { indent: ' ', enableColors: true }),
    };
}