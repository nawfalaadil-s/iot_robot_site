import { getStore } from '@netlify/blobs';

// Receives live telemetry from the robot every few seconds and serves the
// latest snapshot to the dashboard (polled in real time).
const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type'
};

const json = (body, status = 200) => new Response(JSON.stringify(body), {
  status,
  headers: { 'Content-Type': 'application/json', ...CORS }
});

export default async (req) => {
  if (req.method === 'OPTIONS') return new Response('', { status: 204, headers: CORS });

  let store;
  try {
    store = getStore('robot-data');
  } catch (err) {
    return json({ ok: false, error: 'Blobs unavailable: ' + String(err) }, 500);
  }

  // ---- POST: robot pushes live sensor telemetry ----
  if (req.method === 'POST') {
    try {
      const data = await req.json();
      data._serverTime = Date.now();
      await store.setJSON('live/latest.json', data);
      return json({ ok: true });
    } catch (err) {
      return json({ ok: false, error: String(err) }, 400);
    }
  }

  // ---- GET: dashboard reads the latest telemetry ----
  if (req.method === 'GET') {
    try {
      const data = await store.get('live/latest.json', { type: 'json' });
      return json({ ok: true, data: data || null });
    } catch (err) {
      return json({ ok: true, data: null });
    }
  }

  return json({ ok: false, error: 'Method not allowed' }, 405);
};

export const config = { path: '/api/live' };