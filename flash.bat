@echo off
rem Double-click to flash the Pico. Arguments pass straight through to flash.py.
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 goto use_py
where python >nul 2>nul
if not errorlevel 1 goto use_python
echo Python 3 is needed. Install it from https://www.python.org/downloads/
echo then run this again.
goto done
:use_py
py -3 flash.py %*
goto done
:use_python
python flash.py %*
:done
echo.
pause
