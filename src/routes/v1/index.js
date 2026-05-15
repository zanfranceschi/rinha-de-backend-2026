const express = require('express');
const { healthRouter } = require('./health');
const { fraudScoreRouter } = require('./fraud-score');

const v1Router = express.Router();

v1Router.use(healthRouter);
v1Router.use(fraudScoreRouter);

module.exports = { v1Router };
