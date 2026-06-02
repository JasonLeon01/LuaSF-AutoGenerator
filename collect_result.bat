@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "OUTPUT_DIR=%CD%\output"
set "BUILD_DIR=%OUTPUT_DIR%\build"
set "RESULT_DIR=%OUTPUT_DIR%\result"
set "CONFIG=%~1"

if not exist "%BUILD_DIR%" (
    echo Missing output\build. Run build.bat first.
    exit /b 1
)

if "%CONFIG%"=="" (
    for /f "tokens=2 delims==" %%a in ('cmake -N -LA "%BUILD_DIR%" 2^>nul ^| findstr /B /C:"LUASF_DEFAULT_CONFIG:"') do set "CONFIG=%%a"
)

if "%CONFIG%"=="" set "CONFIG=Release"

set "BIN_DIR=%BUILD_DIR%\bin\%CONFIG%"
set "LIB_DIR=%BUILD_DIR%\lib\%CONFIG%"
if not exist "%BIN_DIR%" (
    set "BIN_DIR=%BUILD_DIR%\bin"
)
if not exist "%LIB_DIR%" (
    set "LIB_DIR=%BUILD_DIR%\lib"
)

set "STUB_FILE=%BUILD_DIR%\LuaSF.lua"

if not exist "%BIN_DIR%\LuaSF.dll" (
    echo Missing LuaSF.dll under "%BIN_DIR%".
    echo Run build.bat %CONFIG% first, or pass the built config to this script.
    exit /b 1
)

if not exist "%STUB_FILE%" (
    echo Missing Lua stub "%STUB_FILE%".
    echo Run build.bat %CONFIG% first.
    exit /b 1
)

echo Collecting LuaSF build result...
echo Config: %CONFIG%
echo Source: %BUILD_DIR%
echo Result: %RESULT_DIR%

if exist "%RESULT_DIR%" rmdir /s /q "%RESULT_DIR%"
mkdir "%RESULT_DIR%\bin" "%RESULT_DIR%\include" "%RESULT_DIR%\stub" >nul

copy /y "%BIN_DIR%\*.dll" "%RESULT_DIR%\bin\" >nul
if errorlevel 1 (
    echo Failed to copy DLL files.
    exit /b 1
)

if exist "%CD%\requirements\*.dll" (
    copy /y "%CD%\requirements\*.dll" "%RESULT_DIR%\bin\" >nul
    if errorlevel 1 (
        echo Failed to copy runtime requirement DLL files.
        exit /b 1
    )
)

copy /y "%STUB_FILE%" "%RESULT_DIR%\stub\" >nul
if errorlevel 1 (
    echo Failed to copy Lua stub.
    exit /b 1
)

if exist "%OUTPUT_DIR%\include" (
    robocopy "%OUTPUT_DIR%\include" "%RESULT_DIR%\include" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)

if exist "%BUILD_DIR%\generated_include\sol" (
    robocopy "%BUILD_DIR%\generated_include\sol" "%RESULT_DIR%\include\sol" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)

if exist "%OUTPUT_DIR%\third_party\SFML\include" (
    robocopy "%OUTPUT_DIR%\third_party\SFML\include" "%RESULT_DIR%\include" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)

if exist "%OUTPUT_DIR%\third_party\sol2\include" (
    robocopy "%OUTPUT_DIR%\third_party\sol2\include" "%RESULT_DIR%\include" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)

if exist "%OUTPUT_DIR%\third_party\Lua" (
    mkdir "%RESULT_DIR%\include\lua" >nul 2>nul
    copy /y "%OUTPUT_DIR%\third_party\Lua\*.h" "%RESULT_DIR%\include\lua\" >nul
    if errorlevel 1 exit /b 1
    copy /y "%OUTPUT_DIR%\third_party\Lua\*.hpp" "%RESULT_DIR%\include\lua\" >nul 2>nul
    call :write_lua_compat_header lua.h
    call :write_lua_compat_header lauxlib.h
    call :write_lua_compat_header lualib.h
)

if exist "%LIB_DIR%" (
    mkdir "%RESULT_DIR%\lib" >nul 2>nul
    copy /y "%LIB_DIR%\*.lib" "%RESULT_DIR%\lib\" >nul 2>nul
    copy /y "%LIB_DIR%\*.exp" "%RESULT_DIR%\lib\" >nul 2>nul
)

mkdir "%RESULT_DIR%\cmake" >nul 2>nul
copy /y "cmake\result_CMakeLists.txt" "%RESULT_DIR%\CMakeLists.txt" >nul
if errorlevel 1 (
    echo Failed to copy result CMakeLists.txt.
    exit /b 1
)
copy /y "cmake\LuaSFTargets.cmake" "%RESULT_DIR%\cmake\LuaSFTargets.cmake" >nul
if errorlevel 1 (
    echo Failed to copy LuaSFTargets.cmake.
    exit /b 1
)
copy /y "cmake\result_README.md" "%RESULT_DIR%\README.md" >nul
if errorlevel 1 (
    echo Failed to copy result README.md.
    exit /b 1
)

(
    echo LuaSF result package
    echo ====================
    echo Config: %CONFIG%
    echo Generated from: %BUILD_DIR%
    echo.
    echo bin:
    dir /b "%RESULT_DIR%\bin"
    echo.
    echo stub:
    dir /b "%RESULT_DIR%\stub"
    echo.
    echo include:
    echo - LuaSF generated headers from output\include
    echo - SFML public headers
    echo - sol2 public headers
    echo - Lua headers under include\lua
    echo - CMake generated sol compatibility headers under include\sol
    echo - Lua compatibility wrappers at include\lua.h, include\lauxlib.h, include\lualib.h
    echo - Windows MSVC redistributable DLLs from requirements are bundled in bin
    if exist "%RESULT_DIR%\lib" (
        echo.
        echo lib:
        dir /b "%RESULT_DIR%\lib"
    )
    echo.
    echo cmake:
    echo - add_subdirectory^(path/to/result^)
    echo - target_link_libraries^(your_target PRIVATE LuaSF::LuaSF^)
    echo - luasf_copy_runtime_dlls^(your_target^)
) > "%RESULT_DIR%\manifest.txt"

echo.
echo Done.
echo Result folder: %RESULT_DIR%
echo DLLs: %RESULT_DIR%\bin
echo Stub: %RESULT_DIR%\stub\LuaSF.lua
echo Headers: %RESULT_DIR%\include

endlocal
exit /b 0

:write_lua_compat_header
> "%RESULT_DIR%\include\%~1" echo #include "lua/%~1"
>> "%RESULT_DIR%\include\%~1" echo #undef LUA_VERSION_NUM
>> "%RESULT_DIR%\include\%~1" echo #define LUA_VERSION_NUM 504
>> "%RESULT_DIR%\include\%~1" echo #undef lua_newstate
>> "%RESULT_DIR%\include\%~1" echo #define lua_newstate(f, ud) lua_newstate((f), (ud), 0u)
exit /b 0
