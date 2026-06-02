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

if not exist "third_party\Lua\lua.h" (
    echo Missing third_party\Lua. Run init.bat first.
    exit /b 1
)

if not exist "third_party\sol2\include\sol2\sol.hpp" (
    echo Missing third_party\sol2. Run init.bat first.
    exit /b 1
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

echo Building LuaSF and Lua stub from output CMake project...
cmake --build output\build --config %BUILD_CONFIG% --target LuaSF_lua_stub --parallel 1
if errorlevel 1 exit /b 1

echo.
echo Done.
echo Project: %~dp0output
echo DLL: %~dp0output\build\bin\%BUILD_CONFIG%\LuaSF.dll
echo Stub: %~dp0output\build\LuaSF.lua

endlocal
