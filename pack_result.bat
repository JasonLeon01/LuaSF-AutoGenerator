@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "SOURCE_ONLY=0"
if not "%~2"=="" goto :usage
if not "%~1"=="" (
    if /i not "%~1"=="--source-only" goto :usage
    set "SOURCE_ONLY=1"
)

set "OUTPUT_DIR=%CD%\output"
set "BUILD_DIR=%OUTPUT_DIR%\build"
set "RESULT_DIR=%OUTPUT_DIR%\result"
set "EMBEDDED_RESULT_DIR=%RESULT_DIR%\embedded"
set "PACKAGES_DIR=%OUTPUT_DIR%\packages"
set "STAGING_DIR=%PACKAGES_DIR%\.staging"
if "%SOURCE_ONLY%"=="1" set "STAGING_DIR=%PACKAGES_DIR%\.staging-source"

for %%F in (callback_codecs.json sfml_api.json) do (
    if not exist "%OUTPUT_DIR%\LuaSF\%%F" (
        echo Missing output\LuaSF\%%F. Run build.bat first.
        exit /b 1
    )
)
if "%SOURCE_ONLY%"=="0" (
    if not exist "%EMBEDDED_RESULT_DIR%" (
        echo Missing output\result\embedded. Run collect_result.bat first.
        exit /b 1
    )
    for %%F in (callback_codecs.json sfml_api.json) do (
        if not exist "%EMBEDDED_RESULT_DIR%\%%F" (
            echo Missing embedded %%F. Run collect_result.bat first.
            exit /b 1
        )
    )
)

if "%SOURCE_ONLY%"=="1" goto :source_names

set "PLATFORM_OS="
set "PLATFORM_ARCH="
set "PLATFORM_COMPILER="

if exist "%BUILD_DIR%" (
    for /f "tokens=2 delims==" %%a in ('cmake -N -LA "%BUILD_DIR%" 2^>nul ^| findstr /B /C:"CMAKE_SYSTEM_NAME:"') do set "PLATFORM_OS=%%a"
    for /f "tokens=2 delims==" %%a in ('cmake -N -LA "%BUILD_DIR%" 2^>nul ^| findstr /B /C:"CMAKE_SYSTEM_PROCESSOR:"') do set "PLATFORM_ARCH=%%a"
    for /f "tokens=2 delims==" %%a in ('cmake -N -LA "%BUILD_DIR%" 2^>nul ^| findstr /B /C:"CMAKE_CXX_COMPILER_ID:"') do set "PLATFORM_COMPILER=%%a"
)

if not defined PLATFORM_OS set "PLATFORM_OS=Windows"
if not defined PLATFORM_ARCH (
    if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
        set "PLATFORM_ARCH=ARM64"
    ) else (
        set "PLATFORM_ARCH=x64"
    )
)
if not defined PLATFORM_COMPILER set "PLATFORM_COMPILER=MSVC"

call :normalize_os "%PLATFORM_OS%"
set "PLATFORM_OS=%NORMALIZED%"
call :normalize_arch "%PLATFORM_ARCH%"
set "PLATFORM_ARCH=%NORMALIZED%"
call :normalize_compiler "%PLATFORM_COMPILER%"
set "PLATFORM_COMPILER=%NORMALIZED%"

set "PLATFORM_TAG=%PLATFORM_OS%-%PLATFORM_ARCH%-%PLATFORM_COMPILER%"

:source_names
set "SFML_VARIANT="
if exist "%CD%\.luasf-sfml-variant" set /p SFML_VARIANT=<"%CD%\.luasf-sfml-variant"
set "VARIANT_SUFFIX="
if not "%SFML_VARIANT%"=="" set "VARIANT_SUFFIX=-%SFML_VARIANT%"
set "SOURCE_VARIANT_SUFFIX=%VARIANT_SUFFIX%"
if "%SFML_VARIANT%"=="ME-OH" set "SOURCE_VARIANT_SUFFIX=-ME"
set "SOURCE_NAME=LuaSF-source%SOURCE_VARIANT_SUFFIX%"
set "EMBEDDED_NAME=LuaSF-embedded%VARIANT_SUFFIX%-%PLATFORM_TAG%"
set "SOURCE_ZIP=%PACKAGES_DIR%\%SOURCE_NAME%.zip"
set "EMBEDDED_ZIP=%PACKAGES_DIR%\%EMBEDDED_NAME%.zip"

echo Packing LuaSF redistributable archives...
if "%SFML_VARIANT%"=="" (echo SFML variant: default) else (echo SFML variant: %SFML_VARIANT%)
if "%SOURCE_ONLY%"=="0" echo Platform: %PLATFORM_TAG%
echo Packages: %PACKAGES_DIR%

if "%SOURCE_ONLY%"=="0" (
    if exist "%PACKAGES_DIR%" rmdir /s /q "%PACKAGES_DIR%"
) else (
    if exist "%STAGING_DIR%" rmdir /s /q "%STAGING_DIR%"
)
mkdir "%STAGING_DIR%"

rem Source archive contains exactly the LuaSF and LuaGlue CMake projects.
if "%SOURCE_ONLY%"=="0" if "%SFML_VARIANT%"=="ME-OH" goto :pack_embedded
for %%P in (LuaSF LuaGlue) do (
    if not exist "%OUTPUT_DIR%\%%P\CMakeLists.txt" (
        echo Missing source project %%P. Run build.bat first.
        exit /b 1
    )
    robocopy "%OUTPUT_DIR%\%%P" "%STAGING_DIR%\%%P" /E /XD build .git __pycache__ .cache /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 exit /b 1
)
powershell -NoProfile -Command "Compress-Archive -LiteralPath '%STAGING_DIR%\LuaSF','%STAGING_DIR%\LuaGlue' -DestinationPath '%STAGING_DIR%\%SOURCE_NAME%.zip' -CompressionLevel Optimal"
if errorlevel 1 (
    echo Failed to create "%SOURCE_ZIP%".
    exit /b 1
)

move /Y "%STAGING_DIR%\%SOURCE_NAME%.zip" "%SOURCE_ZIP%" >nul
if errorlevel 1 exit /b 1
if "%SOURCE_ONLY%"=="1" goto :pack_done

:pack_embedded
mkdir "%STAGING_DIR%\%EMBEDDED_NAME%"
xcopy /e /i /q /y "%EMBEDDED_RESULT_DIR%\*" "%STAGING_DIR%\%EMBEDDED_NAME%\" >nul

powershell -NoProfile -Command "Compress-Archive -LiteralPath '%STAGING_DIR%\%EMBEDDED_NAME%' -DestinationPath '%EMBEDDED_ZIP%' -CompressionLevel Optimal"
if errorlevel 1 (
    echo Failed to create "%EMBEDDED_ZIP%".
    exit /b 1
)

:pack_done
rmdir /s /q "%STAGING_DIR%"

echo.
echo Done.
if "%SOURCE_ONLY%"=="1" (
    echo Source: %SOURCE_ZIP%
) else if "%SFML_VARIANT%"=="ME-OH" (
    echo Source: use LuaSF-source-ME
) else (
    echo Source: %SOURCE_ZIP%
)
if "%SOURCE_ONLY%"=="0" echo Embedded: %EMBEDDED_ZIP%
exit /b 0

:usage
echo Usage: pack_result.bat [--source-only]
exit /b 2

:normalize_os
set "NORMALIZED=%~1"
if /i "%NORMALIZED%"=="Darwin" set "NORMALIZED=macOS"
if /i "%NORMALIZED%"=="darwin" set "NORMALIZED=macOS"
if /i "%NORMALIZED%"=="OSX" set "NORMALIZED=macOS"
if /i "%NORMALIZED%"=="WIN32" set "NORMALIZED=Windows"
if /i "%NORMALIZED%"=="win32" set "NORMALIZED=Windows"
if /i "%NORMALIZED%"=="windows" set "NORMALIZED=Windows"
if /i "%NORMALIZED%"=="linux" set "NORMALIZED=Linux"
exit /b 0

:normalize_arch
set "NORMALIZED=%~1"
if /i "%NORMALIZED%"=="arm64" set "NORMALIZED=ARM64"
if /i "%NORMALIZED%"=="aarch64" set "NORMALIZED=ARM64"
if /i "%NORMALIZED%"=="x86_64" set "NORMALIZED=x64"
if /i "%NORMALIZED%"=="amd64" set "NORMALIZED=x64"
if /i "%NORMALIZED%"=="AMD64" set "NORMALIZED=x64"
if /i "%NORMALIZED%"=="X64" set "NORMALIZED=x64"
exit /b 0

:normalize_compiler
set "NORMALIZED=%~1"
if /i "%NORMALIZED%"=="AppleClang" set "NORMALIZED=clang"
if /i "%NORMALIZED%"=="Clang" set "NORMALIZED=clang"
if /i "%NORMALIZED%"=="clang" set "NORMALIZED=clang"
if /i "%NORMALIZED%"=="msvc" set "NORMALIZED=MSVC"
if /i "%NORMALIZED%"=="GNU" set "NORMALIZED=gcc"
if /i "%NORMALIZED%"=="GCC" set "NORMALIZED=gcc"
exit /b 0
