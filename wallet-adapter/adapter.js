const express = require('express');
const axios = require('axios');
const bodyParser = require('body-parser');

const app = express();
app.use(bodyParser.json());

const MEVACOIND_RPC_URL = 'http://127.0.0.1:17081/json_rpc';

async function rpcCall(method, params = {}) {
    try {
        const response = await axios.post(MEVACOIND_RPC_URL, {
            jsonrpc: "2.0",
            id: "0",
            method: method,
            params: params
        });
        return response.data.result;
    } catch (err) {
        console.error(err.response ? err.response.data : err.message);
        return { error: err.message };
    }
}

// --- Traduzione degli endpoint REST del wallet ---

// /getlastblockheader
app.get('/getlastblockheader', async (req, res) => {
    const result = await rpcCall('getlastblockheader');
    res.json(result);
});

// /height
app.get('/height', async (req, res) => {
    const result = await rpcCall('getblockcount');
    res.json({ height: result.count - 1, network_height: result.count - 1, status: "OK" });
});

// /info
app.get('/info', async (req, res) => {
    const result = await rpcCall('getinfo');
    res.json(result);
});

// Altri endpoint REST possono essere aggiunti nello stesso modo

// Start server
const PORT = 18081;  // esempio, separato da mevacoind
app.listen(PORT, () => {
    console.log(`Wallet adapter listening on port ${PORT}`);
});
