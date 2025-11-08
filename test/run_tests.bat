@echo off
REM StarForgeOS Test Runner Script (Windows)
REM Run tests across all supported ESP32 board types

setlocal enabledelayedexpansion

echo ========================================
echo StarForgeOS Multi-Board Test Suite
echo ========================================
echo.

REM Check if PlatformIO is installed
where pio >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] PlatformIO not found. Please install it:
    echo   pip install platformio
    exit /b 1
)

echo [OK] PlatformIO found
echo.

REM Change to project root (parent of test directory)
cd /d "%~dp0.."

REM Test mode
set TEST_MODE=%1
if "%TEST_MODE%"=="" set TEST_MODE=build-only

if "%TEST_MODE%"=="build-only" (
    echo ========================================
    echo Running BUILD-ONLY tests
    echo No hardware required
    echo ========================================
    echo.
) else if "%TEST_MODE%"=="hardware" (
    echo ========================================
    echo Running HARDWARE tests
    echo Board must be connected via USB
    echo ========================================
    echo.
) else if "%TEST_MODE%"=="specific" (
    if "%2"=="" (
        echo [ERROR] Please specify a board environment
        echo Usage: %0 specific ^<board^>
        echo Available boards:
        echo   - test-esp32-c3
        echo   - test-esp32-c6
        echo   - test-esp32
        echo   - test-esp32-s2
        echo   - test-esp32-s3
        echo   - test-esp32-s3-touch
        echo   - test-jc2432w328c
        exit /b 1
    )
    set SPECIFIC_BOARD=%2
    echo ========================================
    echo Testing %SPECIFIC_BOARD%
    echo ========================================
    echo.
) else (
    echo [ERROR] Unknown test mode: %TEST_MODE%
    echo Usage: %0 [build-only^|hardware^|specific ^<board^>]
    exit /b 1
)

REM Track results
set PASSED=0
set FAILED=0
set FAILED_BOARDS=

REM Test boards
if "%TEST_MODE%"=="specific" (
    call :run_board_test %SPECIFIC_BOARD%
) else (
    call :run_board_test test-esp32-c3 "ESP32-C3"
    call :run_board_test test-esp32-c6 "ESP32-C6"
    call :run_board_test test-esp32 "ESP32"
    call :run_board_test test-esp32-s2 "ESP32-S2"
    call :run_board_test test-esp32-s3 "ESP32-S3"
    call :run_board_test test-esp32-s3-touch "ESP32-S3-Touch"
    call :run_board_test test-jc2432w328c "JC2432W328C"
)

REM Print summary
echo.
echo ========================================
echo Test Summary
echo ========================================
set /a TOTAL=%PASSED%+%FAILED%
echo Total boards tested: %TOTAL%
echo [OK] Passed: %PASSED%
if %FAILED% GTR 0 (
    echo [FAIL] Failed: %FAILED%
    echo.
    echo Failed boards:
    echo %FAILED_BOARDS%
    exit /b 1
) else (
    echo [OK] All tests passed!
    exit /b 0
)

:run_board_test
set ENV=%1
set NAME=%2
if "%NAME%"=="" set NAME=%ENV%

echo ========================================
echo Testing %NAME% ^(%ENV%^)
echo ========================================

if "%TEST_MODE%"=="build-only" (
    pio test -e %ENV% --without-uploading --without-testing
) else (
    pio test -e %ENV%
)

if %ERRORLEVEL% EQU 0 (
    echo [OK] %NAME% passed
    set /a PASSED+=1
) else (
    echo [FAIL] %NAME% failed
    set /a FAILED+=1
    set FAILED_BOARDS=!FAILED_BOARDS! %NAME%
)

echo.
goto :eof

