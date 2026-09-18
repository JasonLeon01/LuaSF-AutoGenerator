@echo off

setlocal EnableDelayedExpansion
cd /d "%~dp0"
for /f "usebackq eol=# tokens=1,2 delims==" %%a in ("versions.conf") do set %%a=%%b

if "%~1"=="" goto :variant_default
if /i "%~1"=="ME" goto :variant_me
if /i "%~1"=="ME-OH" goto :variant_me_oh
echo Unknown SFML variant "%~1". Use ME or ME-OH.
exit /b 2

:variant_me
set "SFML_VARIANT=ME"
set "SFML_VARIANT_REPOSITORY=%SFML_ME_REPOSITORY%"
set "SFML_VARIANT_TAG=%SFML_ME_TAG%"
goto :variant_ready

:variant_me_oh
set "SFML_VARIANT=ME-OH"
set "SFML_VARIANT_REPOSITORY=%SFML_ME_OH_REPOSITORY%"
set "SFML_VARIANT_TAG=%SFML_ME_OH_TAG%"
goto :variant_ready

:variant_default
set "SFML_VARIANT="
set "SFML_VARIANT_REPOSITORY=%SFML_REPOSITORY%"
set "SFML_VARIANT_TAG=%SFML_TAG%"
goto :variant_ready

:variant_ready

if not exist ".venv\Scripts\python.exe" (
    echo Creating Python virtual environment...
    py -3.12 -m venv .venv
    if errorlevel 1 exit /b 1
)

echo Installing Python requirements into .venv...
".venv\Scripts\python.exe" -m pip install -r requirements.txt
if errorlevel 1 exit /b 1

set "SFML_DIR=third_party\SFML"
set "SFML_TAG_FILE=%SFML_DIR%\.luasf-sfml-tag"
set "SFML_SOURCE_FOLDER=%SFML_VARIANT_REPOSITORY%"
for /f "tokens=2 delims=/" %%R in ("%SFML_VARIANT_REPOSITORY%") do set "SFML_SOURCE_FOLDER=%%R"
set "SFML_SOURCE_FOLDER=%SFML_SOURCE_FOLDER%-%SFML_VARIANT_TAG%"
set "SFML_TAG_CURRENT="
if exist "%SFML_TAG_FILE%" set /p SFML_TAG_CURRENT=<"%SFML_TAG_FILE%"

if not exist "%SFML_DIR%\CMakeLists.txt" goto :sfml_download
if not "%SFML_TAG_CURRENT%"=="%SFML_VARIANT_TAG%" goto :sfml_download
echo Using existing third_party\SFML (%SFML_VARIANT_TAG%).
goto :sfml_ready

:sfml_download
if exist "%SFML_DIR%" rmdir /s /q "%SFML_DIR%"
if exist "third_party\%SFML_SOURCE_FOLDER%" rmdir /s /q "third_party\%SFML_SOURCE_FOLDER%"
call "%~dp0download_lib.bat" "SFML %SFML_VARIANT_TAG%" ^
    "https://github.com/%SFML_VARIANT_REPOSITORY%/archive/refs/tags/%SFML_VARIANT_TAG%.zip" ^
    "sfml.zip" ^
    "%SFML_SOURCE_FOLDER%" ^
    "SFML"
if errorlevel 1 exit /b 1
>"%SFML_TAG_FILE%" echo %SFML_VARIANT_TAG%
goto :sfml_ready

:sfml_ready

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
rem The published sol2 headers use CRLF line endings, so the patch has to
rem ignore whitespace to match.
git apply --ignore-whitespace --reverse --check --directory=third_party/sol2 -p1 cmake/sol/pr1606.patch >nul 2>nul
if not errorlevel 1 (
    echo PR #1606 patch already applied to sol2.
) else (
    git apply --ignore-whitespace --check --directory=third_party/sol2 -p1 cmake/sol/pr1606.patch
    if errorlevel 1 exit /b 1
    git apply --ignore-whitespace --directory=third_party/sol2 -p1 cmake/sol/pr1606.patch
    if errorlevel 1 exit /b 1
)

echo.
echo SFML variant: %SFML_VARIANT% (%SFML_VARIANT_TAG%)
