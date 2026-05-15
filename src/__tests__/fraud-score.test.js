'use strict';

const request = require('supertest');
const app = require('../app');

const samplePayload = {
  id: 'tx-3576980410',
  transaction: {
    amount: 384.88,
    installments: 3,
    requested_at: '2026-03-11T20:23:35Z'
  },
  customer: {
    avg_amount: 769.76,
    tx_count_24h: 3,
    known_merchants: ['MERC-009', 'MERC-001', 'MERC-001']
  },
  merchant: {
    id: 'MERC-001',
    mcc: '5912',
    avg_amount: 298.95
  },
  terminal: {
    is_online: false,
    card_present: true,
    km_from_home: 13.7090520965
  },
  last_transaction: {
    timestamp: '2026-03-11T14:58:35Z',
    km_from_current: 18.8626479774
  }
};

describe('POST /fraud-score', () => {
  it('returns the fixed fraud response', async () => {
    const res = await request(app)
      .post('/fraud-score')
      .send(samplePayload);

    expect(res.status).toBe(200);
    expect(res.headers['content-type']).toMatch(/application\/json/);
    expect(res.body).toEqual({
      approved: false,
      fraud_score: 1.0
    });
  });
});
