@echo off
cd /d "%~dp0\..\docs\mockups"
"C:\Python314\python.exe" -m http.server 17380 --bind 0.0.0.0
