@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "OUTPUT_DIR=%CD%\output"
set "BUILD_DIR=%OUTPUT_DIR%\build"
set "RESULT_DIR=%OUTPUT_DIR%\result"
set "EMBEDDED_RESULT_DIR=%RESULT_DIR%\embedded"
set "DEPENDENCY_DIR=%CD%\third_party"
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
set "EMBEDDED_LIB_DIR=%BUILD_DIR%\lib\%CONFIG%\embedded"
if not exist "%EMBEDDED_LIB_DIR%" set "EMBEDDED_LIB_DIR=%BUILD_DIR%\lib\embedded\%CONFIG%"
if not exist "%EMBEDDED_LIB_DIR%" set "EMBEDDED_LIB_DIR=%LIB_DIR%"
set "LUAC_FILE=%BUILD_DIR%\tools\%CONFIG%\luac.exe"
if not exist "%LUAC_FILE%" set "LUAC_FILE=%BUILD_DIR%\tools\luac.exe"

set "STUB_FILE=%BUILD_DIR%\LuaSF.d.lua"
set "CALLBACK_CODECS_FILE=%OUTPUT_DIR%\LuaSF\callback_codecs.json"
set "SFML_API_FILE=%OUTPUT_DIR%\LuaSF\sfml_api.json"

if not exist "%EMBEDDED_BIN_DIR%\LuaSF.dll" (
    echo Missing embedded LuaSF.dll under "%EMBEDDED_BIN_DIR%".
    echo Run build.bat %CONFIG% first, or pass the built config to this script.
    exit /b 1
)

if not exist "%STUB_FILE%" (
    echo Missing Lua stub "%STUB_FILE%".
    echo Run build.bat %CONFIG% first.
    exit /b 1
)

if not exist "%CALLBACK_CODECS_FILE%" (
    echo Missing callback codec manifest "%CALLBACK_CODECS_FILE%".
    echo Run build.bat %CONFIG% first.
    exit /b 1
)

if not exist "%SFML_API_FILE%" (
    echo Missing SFML API snapshot "%SFML_API_FILE%".
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
mkdir "%EMBEDDED_RESULT_DIR%\bin" "%EMBEDDED_RESULT_DIR%\include" "%EMBEDDED_RESULT_DIR%\stub" "%EMBEDDED_RESULT_DIR%\tools" >nul

copy /y "%EMBEDDED_BIN_DIR%\*.dll" "%EMBEDDED_RESULT_DIR%\bin\" >nul
if errorlevel 1 (
    echo Failed to copy embedded DLL files.
    exit /b 1
)

if exist "%CD%\requirements\*.dll" (
    copy /y "%CD%\requirements\*.dll" "%EMBEDDED_RESULT_DIR%\bin\" >nul
    if errorlevel 1 (
        echo Failed to copy embedded runtime requirement DLL files.
        exit /b 1
    )
)

copy /y "%STUB_FILE%" "%EMBEDDED_RESULT_DIR%\stub\" >nul
if errorlevel 1 (
    echo Failed to copy embedded Lua stub.
    exit /b 1
)
copy /y "%CALLBACK_CODECS_FILE%" "%EMBEDDED_RESULT_DIR%\callback_codecs.json" >nul
if errorlevel 1 (
    echo Failed to copy callback codec manifest.
    exit /b 1
)
copy /y "%SFML_API_FILE%" "%EMBEDDED_RESULT_DIR%\sfml_api.json" >nul
if errorlevel 1 (
    echo Failed to copy SFML API snapshot.
    exit /b 1
)
copy /y "%LUAC_FILE%" "%EMBEDDED_RESULT_DIR%\tools\luac.exe" >nul
if errorlevel 1 (
    echo Failed to copy host luac.
    exit /b 1
)

if exist "%OUTPUT_DIR%\LuaSF\include" (
    robocopy "%OUTPUT_DIR%\LuaSF\include" "%EMBEDDED_RESULT_DIR%\include" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)


robocopy "%OUTPUT_DIR%\LuaGlue\include" "%EMBEDDED_RESULT_DIR%\include" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1

if exist "%DEPENDENCY_DIR%\SFML\include" (
    robocopy "%DEPENDENCY_DIR%\SFML\include" "%EMBEDDED_RESULT_DIR%\include" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)


if exist "%DEPENDENCY_DIR%\Lua\src" (
    mkdir "%EMBEDDED_RESULT_DIR%\include" >nul 2>nul
    copy /y "%DEPENDENCY_DIR%\Lua\src\*.h" "%EMBEDDED_RESULT_DIR%\include\" >nul
    if errorlevel 1 exit /b 1
    copy /y "%DEPENDENCY_DIR%\Lua\src\*.hpp" "%EMBEDDED_RESULT_DIR%\include\" >nul 2>nul
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

for %%D in ("%BUILD_DIR%\LuaGlue" "%BUILD_DIR%\LuaGlue\%CONFIG%") do (
    if exist "%%~D" (
        copy /y "%%~D\*.lib" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
        copy /y "%%~D\*.a" "%EMBEDDED_RESULT_DIR%\lib\" >nul 2>nul
    )
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
    echo callback codecs:
    echo - callback_codecs.json
    echo - sfml_api.json
    echo.
    echo tools:
    dir /b "%EMBEDDED_RESULT_DIR%\tools"
    echo.
    echo include:
    echo - LuaSF generated headers from output\LuaSF\include
    echo - SFML public headers
    echo - LuaGlue public headers
    echo - Native Lua 5.5 headers under include
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
    echo LuaSF result package
    echo =====================
    echo Config: %CONFIG%
    echo Generated from: %BUILD_DIR%
    echo.
    echo embedded:
    echo - C/C++ embedded Lua integration package.
    echo - CMake package root: embedded
) > "%RESULT_DIR%\manifest.txt"

echo.
echo Done.
echo Result folder: %RESULT_DIR%
echo Embedded DLLs: %EMBEDDED_RESULT_DIR%\bin
echo Embedded stub: %EMBEDDED_RESULT_DIR%\stub\LuaSF.d.lua
echo Callback codecs: %EMBEDDED_RESULT_DIR%\callback_codecs.json
echo SFML API snapshot: %EMBEDDED_RESULT_DIR%\sfml_api.json
echo Headers: %EMBEDDED_RESULT_DIR%\include
echo Host luac: %EMBEDDED_RESULT_DIR%\tools\luac.exe

endlocal
exit /b 0
