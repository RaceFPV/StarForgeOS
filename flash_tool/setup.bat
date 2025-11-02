@echo off
REM StarForge Flasher - Quick Development Setup Script

echo.
echo ===============================
echo StarForge Flasher Setup
echo ===============================
echo.

REM Check Node.js
where node >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [X] Node.js not found. Please install Node.js 18+ from https://nodejs.org
    pause
    exit /b 1
)

for /f "tokens=*" %%i in ('node --version') do set NODE_VERSION=%%i
echo [OK] Node.js %NODE_VERSION% found

REM Check npm
where npm >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [X] npm not found
    pause
    exit /b 1
)

for /f "tokens=*" %%i in ('npm --version') do set NPM_VERSION=%%i
echo [OK] npm %NPM_VERSION% found

REM Check esptool
where esptool.exe >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    where esptool.py >nul 2>nul
    if %ERRORLEVEL% NEQ 0 (
        echo [!] esptool not found in PATH
        echo     Install with: pip install esptool
        echo     The app will still run but flashing won't work until esptool is installed
    ) else (
        echo [OK] esptool.py found
    )
) else (
    echo [OK] esptool.exe found
)

echo.
echo Installing npm dependencies...
call npm install

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ===============================
    echo [OK] Setup complete!
    echo ===============================
    echo.
    echo Available commands:
    echo   npm start       - Run in development mode
    echo   npm run build   - Build for your platform
    echo   npm run build:win - Build for Windows
    echo.
    echo To get started, run:
    echo   npm start
    echo.
) else (
    echo [X] Setup failed. Please check errors above.
    pause
    exit /b 1
)

pause

