@echo off
title Robot Dashboard - Local Server
cd /d "%~dp0"
echo ========================================
echo  Robot Dashboard - LOCAL SERVER
echo  Dashboard: http://localhost:3000
echo  (keep this window open; Ctrl+C to stop)
echo ========================================
node local-server.mjs
pause
