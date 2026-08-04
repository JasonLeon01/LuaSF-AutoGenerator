@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "CONFIG_OVERRIDE=%~1"
set "CONFIGURE_ARGS="
if not "%CONFIG_OVERRIDE%"=="" (
    set "CONFIGURE_ARGS=-DLUASF_DEFAULT_CONFIG=%CONFIG_OVERRIDE% -DCMAKE_BUILD_TYPE=%CONFIG_OVERRIDE%"
)

set "PYTHON_EXE=.venv\Scripts\python.exe"

if not exist "%PYTHON_EXE%" (
    echo Missing .venv. Run init.bat first.
    exit /b 1
)

if not exist "third_party\SFML\CMakeLists.txt" (
    echo Missing third_party\SFML. Run init.bat first.
    exit /b 1
)

if not exist "third_party\Lua\src\lua.h" (
    echo Missing third_party\Lua. Run init.bat first.
    exit /b 1
)

if not exist "third_party\sol2\include\sol2\sol.hpp" (
    echo Missing third_party\sol2. Run init.bat first.
    exit /b 1
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

echo Extracting SFML public API...
"%PYTHON_EXE%" tools\extract_sfml_api.py
if errorlevel 1 exit /b 1

echo Generating sol2 bindings...
"%PYTHON_EXE%" tools\generate_sol2_bindings.py
if errorlevel 1 exit /b 1

echo Generating standalone output CMake project...
"%PYTHON_EXE%" tools\generate_build_files.py --force-sort
if errorlevel 1 exit /b 1

echo Configuring output CMake project...
cmake -S output -B output\build %CONFIGURE_ARGS%
if errorlevel 1 exit /b 1

set "BUILD_CONFIG="
for /f "tokens=2 delims==" %%a in ('cmake -N -LA output\build 2^>nul ^| findstr /B /C:"LUASF_DEFAULT_CONFIG:"') do set "BUILD_CONFIG=%%a"
if "%BUILD_CONFIG%"=="" (
    echo Failed to read LUASF_DEFAULT_CONFIG from CMake cache.
    exit /b 1
)

echo Building embedded LuaSF, Lua extension, host luac, and Lua stub from output CMake project...
cmake --build output\build --config %BUILD_CONFIG% --target LuaSF_build_outputs --parallel 1
if errorlevel 1 exit /b 1

set "EMBEDDED_DLL=%~dp0output\build\bin\%BUILD_CONFIG%\embedded\LuaSF.dll"
set "EXTENSION_DLL=%~dp0output\build\bin\%BUILD_CONFIG%\extension\LuaSF.dll"
if not exist "%EMBEDDED_DLL%" set "EMBEDDED_DLL=%~dp0output\build\bin\embedded\%BUILD_CONFIG%\LuaSF.dll"
if not exist "%EXTENSION_DLL%" set "EXTENSION_DLL=%~dp0output\build\bin\extension\%BUILD_CONFIG%\LuaSF.dll"

echo.
echo Done.
echo Project: %~dp0output
echo Embedded DLL: %EMBEDDED_DLL%
echo Lua extension: %EXTENSION_DLL%
echo Stub: %~dp0output\build\LuaSF.d.lua

endlocal
