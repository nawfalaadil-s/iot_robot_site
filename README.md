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
└── Dashboard/                       ← deploy this folder to Netlify (or run locally)
    ├── index.html                       AI dashboard v5.0 (real-time)
    ├── FINAL_DASHBOARD.html             backup copy of index.html
    ├── local-server.mjs                 🖥️ LOCAL backend (Node.js, no hosting needed)
    ├── start-local.bat                  🖥️ double-click to start the local server
    ├── netlify.toml                     Netlify build config (only for hosted mode)
    ├── package.json                     function dependency (@netlify/blobs)
    └── netlify/functions/
        ├── live.mjs                     /api/live      (telemetry in/out)
        └── inspection.mjs               /api/inspection (history storage)
```

> Note: the `.ino` must stay inside a folder with the same name
> (`FINAL_ESP32_CODE`) — that's an Arduino IDE requirement.

## 🖥️ Running 100% LOCALLY (no Netlify, no hosting)

You can run everything on your own PC — the dashboard and the backend are
replaced by one small Node.js server with **zero dependencies**.

### Step 1 — Start the server
Double-click **`Dashboard/start-local.bat`** (or: `cd Dashboard` then `node local-server.mjs`).
- Dashboard opens at: `http://localhost:3000`
- Robot data is stored in `Dashboard/data/` as plain JSON files.

### Step 2 — Point the robot at your PC
1. Find your PC's WiFi IP: run `ipconfig` → look for **IPv4 Address** under your Wi-Fi adapter (e.g. `192.168.29.33`).
2. In `ESP32_Code/FINAL_ESP32_CODE/FINAL_ESP32_CODE.ino` (line ~56):
   ```cpp
   const char* DASHBOARD_URL = "http://YOUR-PC-IP:3000";   // e.g. http://192.168.29.33:3000
   ```
3. Upload the firmware. Done — the robot now streams to your local dashboard. ✅

### Requirements & gotchas
- **Same WiFi:** the ESP32 and your PC must be on the same network (WiFi `robo` in the firmware config).
- **Firewall:** if Windows asks, allow **Node.js on private networks**. To open the port manually:
  ```powershell
  netsh advfirewall firewall add rule name="Robot Dashboard 3000" dir=in action=allow protocol=TCP localport=3000
  ```
- **IP changes:** home routers often assign a new IP after a reboot — re-check `ipconfig` if the dashboard stops receiving data (or reserve your PC's IP in the router settings).
- Hosted mode still works: swap `DASHBOARD_URL` back to your `https://...netlify.app` URL.

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

## 🎮 Robot Commands — from the dashboard (WiFi control)

The dashboard's **🎮 Manual Control** section sends commands to the robot over
WiFi (the robot polls `GET /api/command` every 500 ms). No Bluetooth needed —
this also freed ~80 KB of RAM that Classic Bluetooth was eating.

| Button | Command | Button | Command |
|--------|---------|--------|---------|
| ⬆ Forward / ⬇ Back / ⬅ Left / ➡ Right | F / B / L / R | 🤖 Auto / 🕹 Manual | A / M |
| ■ Stop | S | 1 / 2 / 3 | 50% / 75% / 100% speed |
| 🛑 E-STOP | X | 🔍 Inspect | I (needs an RFID machine nearby) |

> Classic Bluetooth is compiled out by default (`ENABLE_BLUETOOTH 0` in the
> `.ino`, at the top) because BT + WiFi together starved the ESP32's RAM
> (~15 KB free → HTTP failed + random crashes). Flip it to `1` to re-enable,
> but expect the memory problems to return.

## ✅ ESP32 libraries (Core 3.x)
`DHT sensor library`, `MFRC522`, `ArduinoJson`. Board: **ESP32 Dev Module**, Serial **115200**.

