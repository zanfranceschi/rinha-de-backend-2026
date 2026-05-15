const express = require('express');

const healthRouter = express.Router();

healthRouter.get('/ready', (_request, response) => {
  response.status(200).json({
    status: 'ok',
    instance: process.env.INSTANCE_ID || process.env.HOSTNAME || 'unknown'
  });
});

module.exports = { healthRouter };
