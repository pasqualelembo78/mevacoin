'use strict';

/**
 * Mevacoin JSON proxy (Node.js 10 compatible)
 * Normalizza le risposte /sync e simili
 * Garantisce la presenza di fee.amount = 0
 */

var express = require('express');
var fetch = require('node-fetch'); // v2.x
var bodyParser = require('body-parser');

var BACKEND = process.env.BACKEND_URL || 'http://127.0.0.1:17081';
var PORT = process.env.PORT || 18081;

var app = express();

app.use(bodyParser.json({ limit: '10mb' }));
app.use(bodyParser.urlencoded({ extended: true }));

/**
 * Garantisce fee.amount su ogni possibile oggetto tx
 */
function ensureFee(obj) {
  if (!obj || typeof obj !== 'object') return;

  var looksLikeTx =
    obj.hasOwnProperty('hash') ||
    obj.hasOwnProperty('outputs') ||
    obj.hasOwnProperty('txPublicKey') ||
    obj.hasOwnProperty('vin') ||
    obj.hasOwnProperty('vout');

  if (looksLikeTx) {
    if (!obj.hasOwnProperty('fee') || obj.fee === null) {
      obj.fee = { amount: 0 };
    } else if (typeof obj.fee === 'object' && !obj.fee.hasOwnProperty('amount')) {
      obj.fee.amount = 0;
    }
  }

  Object.keys(obj).forEach(function (key) {
    var val = obj[key];
    if (Array.isArray(val)) {
      val.forEach(function (item) {
        ensureFee(item);
      });
    } else if (val && typeof val === 'object') {
      ensureFee(val);
    }
  });
}

/**
 * Proxy universale
 */
app.all('/*', function (req, res) {
  var backendUrl = BACKEND + req.originalUrl;

  var options = {
    method: req.method,
    headers: {}
  };

  // Copia headers tranne host
  Object.keys(req.headers || {}).forEach(function (h) {
    if (h.toLowerCase() !== 'host') {
      options.headers[h] = req.headers[h];
    }
  });

  // Body JSON (no GET)
  if (req.method !== 'GET' && req.body && Object.keys(req.body).length) {
    options.body = JSON.stringify(req.body);
    options.headers['content-type'] = 'application/json';
  }

  fetch(backendUrl, options)
    .then(function (backendRes) {
      var contentType = backendRes.headers.get('content-type') || '';

      if (contentType.indexOf('application/json') !== -1) {
        return backendRes.json().then(function (data) {
          try {
            ensureFee(data);
          } catch (e) {
            // silenzioso
          }

          res.status(backendRes.status);
          res.set('Content-Type', 'application/json');
          res.send(JSON.stringify(data));
        });
      }

      // passthrough binario / testo
      return backendRes.buffer().then(function (buf) {
        res.status(backendRes.status);
        backendRes.headers.forEach(function (v, k) {
          res.set(k, v);
        });
        res.send(buf);
      });
    })
    .catch(function (err) {
      console.error('Proxy error:', err);
      res.status(502).json({
        error: 'proxy_error',
        message: String(err)
      });
    });
});

app.listen(PORT, function () {
  console.log('Mevacoin proxy listening on port ' + PORT);
  console.log('Backend:', BACKEND);
});
