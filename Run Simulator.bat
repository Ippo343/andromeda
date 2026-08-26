@echo off
rem Double-click entry point for the native LED simulator: picks a hardware
rem model from a dropdown, builds/installs whatever is missing, then opens
rem the controls page + LED visualizer in your browser.
rem See tools/native-bridge/run-simulator.ps1 for the actual logic.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\native-bridge\run-simulator.ps1"
