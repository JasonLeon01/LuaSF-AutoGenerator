@echo off

setlocal EnableDelayedExpansion
cd /d "%~dp0"
for /f "usebackq eol=# tokens=1,2 delims==" %%a in ("versions.conf") do set %%a=%%b

if not exist ".venv\Scripts\python.exe" (
    echo Creating Python virtual environment...
    py -3.12 -m venv .venv
    if errorlevel 1 exit /b 1
)

echo Installing Python requirements into .venv...
".venv\Scripts\python.exe" -m pip install -r requirements.txt
if errorlevel 1 exit /b 1

if not exist "third_party\SFML\CMakeLists.txt" (
    call "%~dp0download_lib.bat" "SFML" ^
        "https://github.com/SFML/SFML/archive/refs/tags/%SFML_VERSION%.zip" ^
        "sfml.zip" ^
        "SFML-%SFML_VERSION%" ^
        "SFML"
    if errorlevel 1 exit /b 1
) else (
    echo Using existing third_party\SFML.
)

if not exist "third_party\Lua\src\lua.h" (
    call "%~dp0download_lib.bat" "Lua" ^
        "https://www.lua.org/ftp/lua-%LUA_VERSION%.tar.gz" ^
        "lua.tar.gz" ^
        "lua-%LUA_VERSION%" ^
        "Lua" ^
        "%LUA_SHA256%"
    if errorlevel 1 exit /b 1
) else (
    echo Using existing third_party\Lua.
)

if not exist "third_party\sol2\include\sol2\sol.hpp" (
    echo Downloading sol2 headers...
    mkdir "third_party\sol2\include\sol2" 2>nul
    for %%f in (config.hpp forward.hpp sol.hpp) do (
        powershell -Command "Invoke-WebRequest -Uri 'https://github.com/ThePhD/sol2/releases/download/v%SOL2_VERSION%/%%f' -OutFile 'third_party\sol2\include\sol2\%%f'"
        if errorlevel 1 (
            echo Failed to download sol2 %%f.
            exit /b 1
        )
    )
) else (
    echo Using existing third_party\sol2.
)

echo Applying sol2 PR #1606 patch if needed...
git apply --reverse --check --directory=third_party/sol2 -p1 cmake/sol/pr1606.patch >nul 2>nul
if not errorlevel 1 (
    echo PR #1606 patch already applied to sol2.
) else (
    git apply --check --directory=third_party/sol2 -p1 cmake/sol/pr1606.patch
    if errorlevel 1 exit /b 1
    git apply --directory=third_party/sol2 -p1 cmake/sol/pr1606.patch
    if errorlevel 1 exit /b 1
)
