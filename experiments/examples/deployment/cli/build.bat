@echo off
rem Builds bin\qhufdel_cli.exe with gcc only (no CMake needed).
rem Requires a MinGW-w64 gcc in PATH, e.g. MSYS2 UCRT64: C:\msys64\ucrt64\bin
setlocal
cd /d "%~dp0"

where gcc >nul 2>nul
if errorlevel 1 (
    if exist "C:\msys64\ucrt64\bin\gcc.exe" (
        set "PATH=C:\msys64\ucrt64\bin;%PATH%"
    ) else (
        echo gcc not found. Install MSYS2 ^(pacman -S mingw-w64-ucrt-x86_64-gcc^) or add gcc to PATH.
        exit /b 1
    )
)

if not exist bin mkdir bin

gcc -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter ^
    -Isrc -Ilib\decoder\inc -Ilib\crc32\inc ^
    src\main.c src\bench_timer.c src\model_loader.c src\package_runner.c ^
    lib\decoder\src\decoder.c lib\decoder\src\bit_manager.c lib\crc32\src\crc32.c ^
    -static -o bin\qhufdel_cli.exe
if errorlevel 1 (
    echo Build FAILED
    exit /b 1
)
echo Built bin\qhufdel_cli.exe
endlocal
