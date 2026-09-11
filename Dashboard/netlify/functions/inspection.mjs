import { getStore } from '@netlify/blobs';

// Stores one inspection record per POST, keeps an index, serves recent history on GET.
const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type'
};

const json = (body, status = 200) => new Response(JSON.stringify(body), {
  status,
  headers: { 'Content-Type': 'application/json', ...CORS }
});

const MAX_HISTORY = 200;

export default async (req) => {
  if (req.method === 'OPTIONS') return new Response('', { status: 204, headers: CORS });

  let store;
  try {
    store = getStore('robot-data');
  } catch (err) {
    return json({ ok: false, error: 'Blobs unavailable: ' + String(err) }, 500);
  }

  // ---- POST: robot pushes an inspection report ----
  if (req.method === 'POST') {
    try {
      const data = await req.json();
      const key = `inspections/${Date.now()}-${Math.random().toString(36).slice(2, 8)}.json`;
      await store.setJSON(key, data);
      await store.setJSON('inspections/latest.json', data);

      const index = (await store.get('inspections/index.json', { type: 'json' })) || [];
      index.push(key);
      while (index.length > MAX_HISTORY) index.shift();
      await store.setJSON('inspections/index.json', index);

      return json({ ok: true, key });
    } catch (err) {
      return json({ ok: false, error: String(err) }, 400);
    }
  }

  // ---- GET: dashboard reads recent inspections (newest first) ----
  if (req.method === 'GET') {
    const url = new URL(req.url);
    const limit = Math.min(parseInt(url.searchParams.get('limit') || '50', 10) || 50, MAX_HISTORY);
    try {
      const index = (await store.get('inspections/index.json', { type: 'json' })) || [];
      const keys = index.slice(-limit).reverse();
      const inspections = [];
      for (const k of keys) {
        try {
          const item = await store.get(k, { type: 'json' });
          if (item) inspections.push({ _id: k, ...item });
        } catch { /* skip unreadable entry */ }
      }
      return json({ ok: true, count: inspections.length, inspections });
    } catch (err) {
      return json({ ok: true, count: 0, inspections: [] });
    }
  }

  return json({ ok: false, error: 'Method not allowed' }, 405);
};

export const config = { path: '/api/inspection' };