import http from 'k6/http';
import { check } from 'k6';

const payload = {
    id: 'tx-smoke-001',
    transaction: {
        amount: 384.88,
        installments: 3,
        requested_at: '2026-03-11T20:23:35Z',
    },
    customer: {
        avg_amount: 769.76,
        tx_count_24h: 3,
        known_merchants: ['MERC-009', 'MERC-001', 'MERC-001'],
    },
    merchant: {
        id: 'MERC-001',
        mcc: '5912',
        avg_amount: 298.95,
    },
    terminal: {
        is_online: false,
        card_present: true,
        km_from_home: 13.7090520965,
    },
    last_transaction: {
        timestamp: '2026-03-11T14:58:35Z',
        km_from_current: 18.8626479774,
    },
};

export const options = {
    vus: 1,
    iterations: 5,
    // Teto absoluto por iteração — se o backend começar a responder lento,
    // o k6 aborta em vez de pendurar o job até o timeout do GitHub Actions.
    maxDuration: '60s',
    thresholds: {
        checks: ['rate==1.0'],
        http_req_failed: ['rate==0.0'],
    },
};

export default function smokeTest() {
    const res = http.post(
        'http://localhost:9999/fraud-score',
        JSON.stringify(payload),
        { headers: { 'Content-Type': 'application/json' }, timeout: '10s' },
    );

    check(res, {
        'status is 200': (r) => r.status === 200,
        'body is json': (r) => {
            try { JSON.parse(r.body); return true; } catch { return false; }
        },
        'approved is boolean': (r) => {
            try { return typeof JSON.parse(r.body).approved === 'boolean'; } catch { return false; }
        },
        'fraud_score is number': (r) => {
            try { return typeof JSON.parse(r.body).fraud_score === 'number'; } catch { return false; }
        },
    });
}
