// ==========================================================================
// LOCAL SERVER for the Industrial Inspection Robot dashboard
// ==========================================================================
// Replaces the Netlify Functions + Netlify Blobs backend with plain Node.js
// (zero dependencies — uses only Node built-ins) so the whole project can run
// on your own PC, no hosting required.
//
//   • Serves the dashboard:      http://localhost:3000
//   • Same API as the Netlify functions, same JSON response shapes:
//       POST /api/live        robot pushes live telemetry   → { ok: true }
//       GET  /api/live        dashboard polls latest        → { ok: true, data: {...}|null }
//       POST /api/inspection  robot pushes inspection       → { ok: true, key }
//       GET  /api/inspection  dashboard polls history       → { ok: true, count, inspections }
//   • Data is stored as JSON files in Dashboard/data/ (git-ignored).
//
// Run:   node local-server.mjs        (or double-click start-local.bat)
// ==========================================================================

import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PORT = process.env.PORT || 3000;
const HOST = '0.0.0.0';                 // listen on all interfaces so the ESP32 can reach it over WiFi

// ---- Local file storage (same roles as the Netlify Blobs keys) ----
const DATA_DIR = path.join(__dirname, 'data');
const LIVE_FILE = path.join(DATA_DIR, 'live-latest.json');
const INSPECTIONS_DIR = path.join(DATA_DIR, 'inspections');
const INDEX_FILE = path.join(DATA_DIR, 'inspections-index.json');
const COMMANDS_FILE = path.join(DATA_DIR, 'commands-pending.json');
const MAX_HISTORY = 200;                // same cap as inspection.mjs

for (const dir of [DATA_DIR, INSPECTIONS_DIR]) {
  fs.mkdirSync(dir, { recursive: true });
}

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type'
};

const json = (res, body, status = 200) => {
  const payload = JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': 'application/json', ...CORS });
  res.end(payload);
};

const readJson = (file, fallback) => {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch {
    return fallback;
  }
};

const writeJson = (file, data) => {
  // write to a temp file then rename → a crash mid-write never corrupts data
  const tmp = file + '.tmp';
  fs.writeFileSync(tmp, JSON.stringify(data, null, 2));
  fs.renameSync(tmp, file);
};

const readBody = (req) => new Promise((resolve, reject) => {
  let size = 0;
  const chunks = [];
  req.on('data', (c) => {
    size += c.length;
    if (size > 1000000) { reject(new Error('Body too large')); req.destroy(); return; }
    chunks.push(c);
  });
  req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
  req.on('error', reject);
});

// ==========================================================================
// API: /api/live  (mirror of netlify/functions/live.mjs)
// ==========================================================================

function handleLive(req, res, body) {
  if (req.method === 'POST') {
    try {
      const data = JSON.parse(body || '{}');
      data._serverTime = Date.now();          // dashboard uses this for the 15 s freshness check
      writeJson(LIVE_FILE, data);
      return json(res, { ok: true });
    } catch (err) {
      return json(res, { ok: false, error: String(err) }, 400);
    }
  }

  if (req.method === 'GET') {
    const data = fs.existsSync(LIVE_FILE) ? readJson(LIVE_FILE, null) : null;
    return json(res, { ok: true, data });
  }

  return json(res, { ok: false, error: 'Method not allowed' }, 405);
}

// ==========================================================================
// API: /api/inspection  (mirror of netlify/functions/inspection.mjs)
// ==========================================================================

function handleInspection(req, res, body, url) {
  if (req.method === 'POST') {
    try {
      const data = JSON.parse(body || '{}');
      data._serverTime = Date.now();   // real wall-clock time for dashboard history
      const key = `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
      writeJson(path.join(INSPECTIONS_DIR, `${key}.json`), data);

      const index = readJson(INDEX_FILE, []);
      index.push(key);
      while (index.length > MAX_HISTORY) {
        const evicted = index.shift();
        try { fs.unlinkSync(path.join(INSPECTIONS_DIR, `${evicted}.json`)); } catch { /* already gone */ }
      }
      writeJson(INDEX_FILE, index);

      console.log(`[inspection] stored ${key}`);
      return json(res, { ok: true, key });
    } catch (err) {
      return json(res, { ok: false, error: String(err) }, 400);
    }
  }

  if (req.method === 'GET') {
    const limit = Math.min(parseInt(url.searchParams.get('limit') || '50', 10) || 50, MAX_HISTORY);
    const index = readJson(INDEX_FILE, []);
    const keys = index.slice(-limit).reverse();          // newest first, same as the Netlify version
    const inspections = [];
    for (const k of keys) {
      const item = readJson(path.join(INSPECTIONS_DIR, `${k}.json`), null);
      if (item) inspections.push({ _id: k, ...item });
    }
    return json(res, { ok: true, count: inspections.length, inspections });
  }

  return json(res, { ok: false, error: 'Method not allowed' }, 405);
}

// ==========================================================================
// API: /api/command  (WiFi robot control — replaces the Bluetooth link)
//   POST {command:"F"}  dashboard queues a command for the robot
//   GET                 robot drains the queue → { ok:true, commands:["F","S"] }
// ==========================================================================

function handleCommand(req, res, body) {
  if (req.method === 'POST') {
    try {
      const data = JSON.parse(body || '{}');
      const cmd = String(data.command || '').trim().toUpperCase().charAt(0);
      if (!cmd) return json(res, { ok: false, error: 'Missing command' }, 400);
      const pending = readJson(COMMANDS_FILE, []);
      pending.push(cmd);
      while (pending.length > 10) pending.shift();   // never queue more than 10
      writeJson(COMMANDS_FILE, pending);
      console.log(`[command] queued ${cmd} (${pending.length} pending)`);
      return json(res, { ok: true, queued: pending.length });
    } catch (err) {
      return json(res, { ok: false, error: String(err) }, 400);
    }
  }

  if (req.method === 'GET') {
    const pending = readJson(COMMANDS_FILE, []);
    if (pending.length) writeJson(COMMANDS_FILE, []);   // drain: robot consumed them
    return json(res, { ok: true, commands: pending });
  }

  return json(res, { ok: false, error: 'Method not allowed' }, 405);
}

// ==========================================================================
// Static files (the dashboard)
// ==========================================================================

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon'
};

function serveStatic(res, urlPath) {
  let rel = decodeURIComponent(urlPath.split('?')[0]);
  if (rel === '/' || rel === '') rel = '/index.html';

  // prevent path traversal — everything must resolve inside the Dashboard folder
  const filePath = path.normalize(path.join(__dirname, rel));
  if (!filePath.startsWith(__dirname)) {
    res.writeHead(403, CORS);
    return res.end('Forbidden');
  }

  fs.readFile(filePath, (err, buf) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain', ...CORS });
      return res.end('404 Not Found');
    }
    const type = MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream';
    res.writeHead(200, { 'Content-Type': type, ...CORS });
    res.end(buf);
  });
}

// ==========================================================================
// Server
// ==========================================================================

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  const p = url.pathname;

  if (req.method === 'OPTIONS') {
    res.writeHead(204, CORS);
    return res.end();
  }

  try {
    if (p === '/api/live') return handleLive(req, res, req.method === 'POST' ? await readBody(req) : '');
    if (p === '/api/inspection') return handleInspection(req, res, req.method === 'POST' ? await readBody(req) : '', url);
    if (p === '/api/command') return handleCommand(req, res, req.method === 'POST' ? await readBody(req) : '');
    if (p.startsWith('/api/')) return json(res, { ok: false, error: 'Unknown endpoint' }, 404);
    return serveStatic(res, p);
  } catch (err) {
    console.error('[server] error:', err);
    return json(res, { ok: false, error: String(err) }, 500);
  }
});

server.on('error', (err) => {
  if (err && err.code === 'EADDRINUSE') {
    console.log('========================================================');
    console.log(' ℹ️  Port 3000 is already in use — the server is ALREADY RUNNING.');
    console.log('    Just open http://localhost:3000 in your browser. Nothing else to do.');
    console.log('    (To restart it: close the other server window first, then run this again.)');
    console.log('========================================================');
    process.exit(0);
  }
  console.error('[server] error:', err);
  process.exit(1);
});

server.listen(PORT, HOST, async () => {
  const nets = [];
  try {
    const os = await import('node:os');
    for (const ifaces of Object.values(os.networkInterfaces())) {
      for (const ni of ifaces || []) {
        if (ni.family === 'IPv4' && !ni.internal) nets.push(ni.address);
      }
    }
  } catch { /* cosmetic only */ }

  console.log('========================================');
  console.log(' 🤖 Robot Dashboard — LOCAL SERVER');
  console.log('========================================');
  console.log(` Dashboard (this PC):  http://localhost:${PORT}`);
  for (const ip of nets) {
    console.log(` Dashboard (network):  http://${ip}:${PORT}`);
  }
  console.log(` Robot posts to:       http://<YOUR-PC-IP>:${PORT}/api/live  (and /api/inspection)`);
  console.log(` Data folder:          ${DATA_DIR}`);
  console.log(' Press Ctrl+C to stop');
  console.log('========================================');
});
