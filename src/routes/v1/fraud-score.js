const express = require('express');
const {
  DEFAULT_TOP_K,
  getNearestNeighbors,
  placeholderDataset
} = require('../../services/nearestNeighbors');

const fraudScoreRouter = express.Router();

function safeNumber(value) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : 0;
}

function toEpochSeconds(value) {
  if (typeof value === 'number' && Number.isFinite(value)) {
    return value > 1e12 ? Math.floor(value / 1000) : value;
  }

  if (typeof value === 'string') {
    const numeric = Number(value);
    if (Number.isFinite(numeric)) {
      return numeric > 1e12 ? Math.floor(numeric / 1000) : numeric;
    }

    const parsed = Date.parse(value);
    return Number.isFinite(parsed) ? Math.floor(parsed / 1000) : 0;
  }

  return 0;
}

function toBooleanNumber(value) {
  return value ? 1 : 0;
}

function merchantIdFeature(value) {
  if (typeof value === 'number' && Number.isFinite(value)) {
    return value;
  }
  if (typeof value === 'string') {
    return value.length;
  }
  return 0;
}

function buildFeatureVector(payload) {
  const transaction = payload.transaction || {};
  const customer = payload.customer || {};
  const merchant = payload.merchant || {};
  const terminal = payload.terminal || {};
  const lastTransaction = payload.last_transaction || {};
  const knownMerchants = Array.isArray(customer.known_merchants)
    ? customer.known_merchants
    : [];
  const knownMerchantCount = knownMerchants.length;
  const knownMerchantUniqueCount = new Set(knownMerchants.map(String)).size;
  const requestedAtEpoch = toEpochSeconds(transaction.requested_at);
  const lastTimestampEpoch = toEpochSeconds(lastTransaction.timestamp);
  const secondsSinceLastTransaction = requestedAtEpoch - lastTimestampEpoch;

  return [
    safeNumber(transaction.amount),
    safeNumber(transaction.installments),
    safeNumber(customer.avg_amount),
    safeNumber(customer.tx_count_24h),
    safeNumber(knownMerchantCount),
    safeNumber(knownMerchantUniqueCount),
    merchantIdFeature(merchant.id),
    safeNumber(merchant.mcc),
    safeNumber(merchant.avg_amount),
    toBooleanNumber(terminal.is_online),
    toBooleanNumber(terminal.card_present),
    safeNumber(terminal.km_from_home),
    safeNumber(lastTransaction.km_from_current),
    safeNumber(secondsSinceLastTransaction)
  ];
}

function computeFraudScore(_payload, _vector, _neighbors) {
  // TODO: Replace with real scoring rule once defined.
  return 1.0;
}

fraudScoreRouter.post('/fraud-score', (request, response) => {
  const payload = request.body || {};
  const vector = buildFeatureVector(payload);
  const neighbors = getNearestNeighbors(vector, placeholderDataset, {
    topK: DEFAULT_TOP_K
  });
  const fraudScore = computeFraudScore(payload, vector, neighbors);

  response.status(200).json({
    approved: false,
    fraud_score: fraudScore
  });
});

module.exports = { fraudScoreRouter };
