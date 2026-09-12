# 🌐 Dashboard (deploy this folder to Netlify)

AI dashboard v5.0 + real-time backend (Netlify Functions + Blobs).

## Contents
| File | Purpose |
|---|---|
| `index.html` | Complete dashboard — HTML + CSS + JS in one file |
| `FINAL_DASHBOARD.html` | Backup copy of index.html (do not deploy both) |
| `netlify.toml` | Netlify build config |
| `netlify/functions/live.mjs` | Backend endpoint `/api/live` — robot telemetry in/out |
| `netlify/functions/inspection.mjs` | Backend endpoint `/api/inspection` — inspection history |
| `package.json` | Declares `@netlify/blobs` used by the functions |

## Deploy

**Option L — Run locally (no hosting at all — recommended for testing):**
```bash
node local-server.mjs        # or double-click start-local.bat
```
Then open `http://localhost:3000` and point the robot at `http://YOUR-PC-IP:3000`
(see the main README → "Running 100% LOCALLY"). Data is saved to `Dashboard/data/`.

**Option A — Git (hosted, includes the backend → real-time data):**
1. Push this folder's contents to a GitHub repo (see main `../README.md`)
2. Netlify → Add new site → Import project → done

**Option B — Netlify CLI:**
```bash
npm install -g netlify-cli
cd Dashboard
netlify deploy --prod
```

**Option C — Drag & drop (app.netlify.com/drop):**
Works instantly, but drag & drop deploys **static files only** — the dashboard will
run in DEMO simulation mode until the functions are deployed via Option A or B.

## After deploying
Copy your site URL (e.g. `https://my-robot.netlify.app`) into the ESP32 firmware:
```cpp
const char* DASHBOARD_URL = "https://my-robot.netlify.app";
```
