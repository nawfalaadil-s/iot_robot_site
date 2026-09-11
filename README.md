# 🤖 Industrial Inspection Robot — FINAL BUILD v5.0

Complete project split into two folders: **ESP32_Code** (robot firmware) and
**Dashboard** (web dashboard + real-time backend). Push to GitHub → connect to
Netlify → your site receives **live robot data**. No Firebase needed.

## 📂 Structure

```
final/
├── ESP32_Code/                      ← upload to the ESP32 (Arduino IDE)
│   └── FINAL_ESP32_CODE/
│       └── FINAL_ESP32_CODE.ino         firmware v5.0 (full-speed digital drive)
│
└── Dashboard/                       ← deploy this folder to Netlify
    ├── index.html                       AI dashboard v5.0 (real-time)
    ├── FINAL_DASHBOARD.html             backup copy of index.html
    ├── netlify.toml                     Netlify build config
    ├── package.json                     function dependency (@netlify/blobs)
    └── netlify/functions/
        ├── live.mjs                     /api/live      (telemetry in/out)
        └── inspection.mjs               /api/inspection (history storage)
```

> Note: the `.ino` must stay inside a folder with the same name
> (`FINAL_ESP32_CODE`) — that's an Arduino IDE requirement.

## 🚀 Deployment (3 steps)

### Step 1 — Push to Git
```bash
cd final
git init
git add .
git commit -m "Industrial Inspection Robot v5.0 final"
git branch -M main
git remote add origin https://github.com/YOUR-USER/YOUR-REPO.git
git push -u origin main
```

### Step 2 — Deploy the Dashboard on Netlify
1. https://app.netlify.com → **Add new site → Import an existing project** → pick your repo
2. Netlify auto-detects `Dashboard/netlify.toml`... set these if asked:
   - **Base directory:** `Dashboard`
   - **Publish directory:** `Dashboard` (or `.`)
   - **Functions directory:** `Dashboard/netlify/functions`
3. Deploy → your site URL, e.g. `https://my-robot.netlify.app`
   - Live endpoints: `/api/live` and `/api/inspection`

### Step 3 — Point the ESP32 at your site
In `ESP32_Code/FINAL_ESP32_CODE/FINAL_ESP32_CODE.ino` line ~57:
```cpp
const char* DASHBOARD_URL = "https://my-robot.netlify.app";  // YOUR URL
```
Re-upload the firmware. The robot now streams real-time data to your dashboard. ✅

## 🚗 Motor wiring: ENA/ENB shorted (full-speed mode)

Firmware v5.0 matches your shorted-jumper hardware:
- **No hardware PWM** — speed control via software duty-cycling of IN1–IN4 (100 Hz)
- Speed commands: `1` = 50% duty, `2` = 75% duty, `3` = **100% (default)**
- PID line following steers by duty differences between the wheels
- GPIO 12/25 unused (they would conflict with the jumpers)

## 🔌 Real-time data flow

```
ESP32 ──POST /api/live (every 3 s)──► Netlify Function ──► Netlify Blobs
Dashboard ──GET /api/live (every 3 s)────────────────────────┘ latest snapshot
ESP32 ──POST /api/inspection (after each machine inspection)► history (200 max)
Dashboard ──GET /api/inspection (every 6 s)──► charts, health, AI alerts
```

- Dashboard shows **Connected** while robot data is fresh (<15 s old)
- Falls back to **demo simulation** if the backend is unreachable
- Inspections also persist to the robot's SD card independent of the cloud

## 🎮 Bluetooth Commands (device: `ESP32_INDUSTRIAL_ROBOT`)

| Cmd | Action | Cmd | Action |
|-----|--------|-----|--------|
| F | Forward (full speed) | A | Autonomous mode ON |
| B | Backward | M | Manual mode |
| L / R | Turn left / right | 1 / 2 / 3 | 50% / 75% / 100% speed |
| S | Stop | I | Force inspection |
| X | 🛑 Emergency stop | | |

## ✅ ESP32 libraries (Core 3.x)
`DHT sensor library`, `MFRC522`, `ArduinoJson`. Board: **ESP32 Dev Module**, Serial **115200**.

