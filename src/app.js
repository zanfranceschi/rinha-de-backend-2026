const express = require('express');
const { v1Router } = require('./routes/v1');

function createApp() {
  const app = express();

  app.use(express.json());
  app.use(v1Router);

  return app;
}

const app = createApp();

module.exports = app;
module.exports.createApp = createApp;
