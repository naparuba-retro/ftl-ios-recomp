@echo off
cd /d "%~dp0"
where python >nul 2>nul && (python play.py %*) || (py -3 play.py %*)
