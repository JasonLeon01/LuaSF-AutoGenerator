@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "OUTPUT_DIR=%CD%\output"
set "BUILD_DIR=%OUTPUT_DIR%\build"
set "RESULT_DIR=%OUTPUT_DIR%\result"
set "EMBEDDED_RESULT_DIR=%RESULT_DIR%\embedded"
set "EXTENSION_RESULT_DIR=%RESULT_DIR%\extension"
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
set "SFML_LIB_DIR=%BUILD_DIR%\third_party\SFML\lib\%CONFIG%"
if not exist "%SFML_LIB_DIR%" set "SFML_LIB_DIR=%BUILD_DIR%\third_party\SFML\lib"
set "EMBEDDED_BIN_DIR=%BUILD_DIR%\bin\%CONFIG%\embedded"
if not exist "%EMBEDDED_BIN_DIR%" set "EMBEDDED_BIN_DIR=%BUILD_DIR%\bin\embedded\%CONFIG%"
if not exist "%EMBEDDED_BIN_DIR%" set "EMBEDDED_BIN_DIR=%BIN_DIR%"
set "EXTENSION_BIN_DIR=%BUILD_DIR%\bin\%CONFIG%\extension"
if not exist "%EXTENSION_BIN_DIR%" set "EXTENSION_BIN_DIR=%BUILD_DIR%\bin\extension\%CONFIG%"
if not exist "%EXTENSION_BIN_DIR%" set "EXTENSION_BIN_DIR=%BUILD_DIR%\bin\extension"
set "EMBEDDED_LIB_DIR=%BUILD_DIR%\lib\%CONFIG%\embedded"
if not exist "%EMBEDDED_LIB_DIR%" set "EMBEDDED_LIB_DIR=%BUILD_DIR%\lib\embedded\%CONFIG%"
if not exist "%EMBEDDED_LIB_DIR%" set "EMBEDDED_LIB_DIR=%LIB_DIR%"
set "LUAC_FILE=%BUILD_DIR%\tools\%CONFIG%\luac.exe"
if not exist "%LUAC_FILE%" set "LUAC_FILE=%BUILD_DIR%\tools\luac.exe"

set "STUB_FILE=%BUILD_DIR%\LuaSF.d.lua"

if not exist "%EMBEDDED_BIN_DIR%\LuaSF.dll" (
    echo Missing embedded LuaSF.dll under "%EMBEDDED_BIN_DIR%".
    echo Run build.bat %CONFIG% first, or pass the built config to this script.
    exit /b 1
)

if not exist "%EXTENSION_BIN_DIR%\LuaSF.dll" (
    echo Missing Lua extension LuaSF.dll under "%EXTENSION_BIN_DIR%".
    echo Run build.bat %CONFIG% first, or pass the built config to this script.
    exit /b 1
)

if not exist "%STUB_FILE%" (
    echo Missing Lua stub "%STUB_FILE%".
    echo Run build.bat %CONFIG% first.
    exit /b 1
)

if not exist "%LUAC_FILE%" (
    echo Missing host luac under "%BUILD_DIR%\tools".
    echo Run build.bat %CONFIG% first.
    exit /b 1
)

echo Collecting LuaSF build result...
echo Config: %CONFIG%
echo Source: %BUILD_DIR%
echo Result: %RESULT_DIR%

if exist "%RESULT_DIR%" rmdir /s /q "%RESULT_DIR%"
mkdir "%EMBEDDED_RESULT_DIR%\bin" "%EMBEDDED_RESULT_DIR%\include" "%EMBEDDED_RESULT_DIR%\stub" "%EMBEDDED_RESULT_DIR%\tools" "%EXTENSION_RESULT_DIR%\bin" "%EXTENSION_RESULT_DIR%\stub" >nul

copy /y "%EMBEDDED_BIN_DIR%\*.dll" "%EMBEDDED_RESULT_DIR%\bin\" >nul
if errorlevel 1 (
    echo Failed to copy embedded DLL files.
    exit /b 1
)

call :copy_extension_dlls "%EXTENSION_BIN_DIR%" "%EXTENSION_RESULT_DIR%\bin"
if errorlevel 1 exit /b 1

if exist "%CD%\requirements\*.dll" (
    copy /y "%CD%\requirements\*.dll" "%EMBEDDED_RESULT_DIR%\bin\" >nul
    if errorlevel 1 (
        echo Failed to copy embedded runtime requirement DLL files.
        exit /b 1
    )
    copy /y "%CD%\requirements\*.dll" "%EXTENSION_RESULT_DIR%\bin\" >nul
    if errorlevel 1 (
        echo Failed to copy Lua extension runtime requirement DLL files.
        exit /b 1
    )
)

copy /y "%STUB_FILE%" "%EMBEDDED_RESULT_DIR%\stub\" >nul
if errorlevel 1 (
    echo Failed to copy embedded Lua stub.
    exit /b 1
)
copy /y "%STUB_FILE%" "%EXTENSION_RESULT_DIR%\stub\" >nul
if errorlevel 1 (
    echo Failed to copy Lua extension stub.
    exit /b 1
)
copy /y "%LUAC_FILE%" "%EMBEDDED_RESULT_DIR%\tools\luac.exe" >nul
if errorlevel 1 (
    echo Failed to copy host luac.
    exit /b 1
)

if exist "%OUTPUT_DIR%\include" (
    robocopy "%OUTPUT_DIR%\include" "%EMBEDDED_RESULT_DIR%\include" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)

if exist "%BUILD_DIR%\generated_include\sol" (
    robocopy "%BUILD_DIR%\generated_include\sol" "%EMBEDDED_RESULT_DIR%\include\sol" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)

if exist "%OUTPUT_DIR%\third_party\SFML\include" (
    robocopy "%OUTPUT_DIR%\third_party\SFML\include" "%EMBEDDED_RESULT_DIR%\include" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)

if exist "%OUTPUT_DIR%\third_party\sol2\include" (
    robocopy "%OUTPUT_DIR%\third_party\sol2\include" "%EMBEDDED_RESULT_DIR%\include" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)

if exist "%OUTPUT_DIR%\third_party\Lua\src" (
    mkdir "%EMBEDDED_RESULT_DIR%\include\lua" >nul 2>nul
    copy /y "%OUTPUT_DIR%\third_party\Lua\src\*.h" "%EMBEDDED_RESULT_DIR%\include\lua\" >nul
    if errorlevel 1 exit /b 1
    copy /y "%OUTPUT_DIR%\third_party\Lua\src\*.hpp" "%EMBEDDED_RESULT_DIR%\include\lua\" >nul 2>nul
    call :write_lua_compat_header lua.h
    call :write_lua_compat_header lauxlib.h
    call :write_lua_compat_header lualib.h
)

if exist "%EMBEDDED_LIB_DIR%" (
    mkdir "%EMBEDDED_RESULT_DIR%\lib" >nul 2>nul
    copy /y "%EMBEDDED_LIB_DIR%\*.lib" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
    copy /y "%EMBEDDED_LIB_DIR%\*.exp" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
    copy /y "%EMBEDDED_LIB_DIR%\*.a" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
)

rem Lua's import library is emitted to lib\<config>, while LuaSF's is under
rem lib\<config>\embedded. SFML libraries use their own third_party directory.
rem Export all of them so the complete C/C++ dependency set is available.
if exist "%LIB_DIR%" (
    mkdir "%EMBEDDED_RESULT_DIR%\lib" >nul 2>nul
    copy /y "%LIB_DIR%\*.lib" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
    copy /y "%LIB_DIR%\*.exp" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
    copy /y "%LIB_DIR%\*.a" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
)
if exist "%SFML_LIB_DIR%" (
    mkdir "%EMBEDDED_RESULT_DIR%\lib" >nul 2>nul
    copy /y "%SFML_LIB_DIR%\*.lib" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
    copy /y "%SFML_LIB_DIR%\*.exp" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
    copy /y "%SFML_LIB_DIR%\*.a" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
)

mkdir "%EMBEDDED_RESULT_DIR%\cmake" >nul 2>nul
copy /y "cmake\result_CMakeLists.txt" "%EMBEDDED_RESULT_DIR%\CMakeLists.txt" >nul
if errorlevel 1 (
    echo Failed to copy result CMakeLists.txt.
    exit /b 1
)
copy /y "cmake\LuaSFTargets.cmake" "%EMBEDDED_RESULT_DIR%\cmake\LuaSFTargets.cmake" >nul
if errorlevel 1 (
    echo Failed to copy LuaSFTargets.cmake.
    exit /b 1
)
copy /y "cmake\result_README.md" "%EMBEDDED_RESULT_DIR%\README.md" >nul
if errorlevel 1 (
    echo Failed to copy result README.md.
    exit /b 1
)

(
    echo LuaSF embedded result package
    echo =============================
    echo Config: %CONFIG%
    echo Generated from: %BUILD_DIR%
    echo.
    echo bin:
    dir /b "%EMBEDDED_RESULT_DIR%\bin"
    echo.
    echo stub:
    dir /b "%EMBEDDED_RESULT_DIR%\stub"
    echo.
    echo tools:
    dir /b "%EMBEDDED_RESULT_DIR%\tools"
    echo.
    echo include:
    echo - LuaSF generated headers from output\include
    echo - SFML public headers
    echo - sol2 public headers
    echo - Lua headers under include\lua
    echo - CMake generated sol compatibility headers under include\sol
    echo - Lua compatibility wrappers at include\lua.h, include\lauxlib.h, include\lualib.h
    echo - Windows MSVC redistributable DLLs from requirements are bundled in bin
    if exist "%EMBEDDED_RESULT_DIR%\lib" (
        echo.
        echo lib:
        dir /b "%EMBEDDED_RESULT_DIR%\lib"
    )
    echo.
    echo cmake:
    echo - add_subdirectory^(path/to/result/embedded^)
    echo - target_link_libraries^(your_target PRIVATE LuaSF::LuaSF^)
    echo - luasf_copy_runtime_dlls^(your_target^)
) > "%EMBEDDED_RESULT_DIR%\manifest.txt"

(
    echo LuaSF Lua extension package
    echo ===========================
    echo Config: %CONFIG%
    echo Generated from: %BUILD_DIR%
    echo.
    echo bin:
    dir /b "%EXTENSION_RESULT_DIR%\bin"
    echo.
    echo stub:
    dir /b "%EXTENSION_RESULT_DIR%\stub"
    echo.
    echo Usage:
    echo - Add bin to package.cpath or copy LuaSF.dll next to your Lua script.
    echo - require^("LuaSF"^) returns the sf table.
    echo - The host Lua runtime provides lua_State; lua.dll is not bundled here.
) > "%EXTENSION_RESULT_DIR%\manifest.txt"

(
    echo LuaSF result packages
    echo =====================
    echo Config: %CONFIG%
    echo Generated from: %BUILD_DIR%
    echo.
    echo embedded:
    echo - C/C++ embedded Lua integration package.
    echo - CMake package root: embedded
    echo.
    echo extension:
    echo - Plain Lua C extension package for require^("LuaSF"^).
) > "%RESULT_DIR%\manifest.txt"

echo.
echo Done.
echo Result folder: %RESULT_DIR%
echo Embedded DLLs: %EMBEDDED_RESULT_DIR%\bin
echo Lua extension DLLs: %EXTENSION_RESULT_DIR%\bin
echo Embedded stub: %EMBEDDED_RESULT_DIR%\stub\LuaSF.d.lua
echo Extension stub: %EXTENSION_RESULT_DIR%\stub\LuaSF.d.lua
echo Headers: %EMBEDDED_RESULT_DIR%\include
echo Host luac: %EMBEDDED_RESULT_DIR%\tools\luac.exe

endlocal
exit /b 0

:write_lua_compat_header
> "%EMBEDDED_RESULT_DIR%\include\%~1" echo #include "lua/%~1"
>> "%EMBEDDED_RESULT_DIR%\include\%~1" echo #undef LUA_VERSION_NUM
>> "%EMBEDDED_RESULT_DIR%\include\%~1" echo #define LUA_VERSION_NUM 504
>> "%EMBEDDED_RESULT_DIR%\include\%~1" echo #undef lua_newstate
>> "%EMBEDDED_RESULT_DIR%\include\%~1" echo #define lua_newstate(f, ud) lua_newstate((f), (ud), 0u)
exit /b 0

:copy_extension_dlls
set "COPY_EXTENSION_SRC=%~1"
set "COPY_EXTENSION_DST=%~2"
set "COPY_EXTENSION_COUNT=0"
for %%f in ("%COPY_EXTENSION_SRC%\*.dll") do (
    if exist "%%~f" (
        if /I not "%%~nxf"=="lua.dll" (
            copy /y "%%~f" "%COPY_EXTENSION_DST%\" >nul
            if errorlevel 1 exit /b 1
            set /a COPY_EXTENSION_COUNT+=1
        )
    )
)
if "%COPY_EXTENSION_COUNT%"=="0" (
    echo Failed to copy Lua extension DLL files.
    exit /b 1
)
exit /b 0
