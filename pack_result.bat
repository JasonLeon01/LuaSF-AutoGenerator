@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

set "OUTPUT_DIR=%CD%\output"
set "BUILD_DIR=%OUTPUT_DIR%\build"
set "RESULT_DIR=%OUTPUT_DIR%\result"
set "EMBEDDED_RESULT_DIR=%RESULT_DIR%\embedded"
set "EXTENSION_RESULT_DIR=%RESULT_DIR%\extension"
set "PACKAGES_DIR=%OUTPUT_DIR%\packages"
set "STAGING_DIR=%PACKAGES_DIR%\.staging"

if not exist "%EMBEDDED_RESULT_DIR%" (
    echo Missing output\result\embedded. Run collect_result.bat first.
    exit /b 1
)

if not exist "%EXTENSION_RESULT_DIR%" (
    echo Missing output\result\extension. Run collect_result.bat first.
    exit /b 1
)

if not exist "%OUTPUT_DIR%\callback_codecs.json" (
    echo Missing output\callback_codecs.json. Run build.bat first.
    exit /b 1
)

if not exist "%EMBEDDED_RESULT_DIR%\callback_codecs.json" (
    echo Missing embedded callback codec manifest. Run collect_result.bat first.
    exit /b 1
)

if not exist "%EMBEDDED_RESULT_DIR%\sfml_api.json" (
    echo Missing embedded SFML API snapshot. Run collect_result.bat first.
    exit /b 1
)

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
set "SOURCE_NAME=LuaSF-source"
set "EMBEDDED_NAME=LuaSF-embedded-%PLATFORM_TAG%"
set "EXTENSION_NAME=LuaSF-extension-%PLATFORM_TAG%"
set "SOURCE_ZIP=%PACKAGES_DIR%\%SOURCE_NAME%.zip"
set "EMBEDDED_ZIP=%PACKAGES_DIR%\%EMBEDDED_NAME%.zip"
set "EXTENSION_ZIP=%PACKAGES_DIR%\%EXTENSION_NAME%.zip"

echo Packing LuaSF redistributable archives...
echo Platform: %PLATFORM_TAG%
echo Packages: %PACKAGES_DIR%

if exist "%PACKAGES_DIR%" rmdir /s /q "%PACKAGES_DIR%"
mkdir "%STAGING_DIR%\%SOURCE_NAME%"
mkdir "%STAGING_DIR%\%EMBEDDED_NAME%"
mkdir "%STAGING_DIR%\%EXTENSION_NAME%"

rem Source package: output\ without build, bin, result, packages.
for /f "delims=" %%I in ('dir /b /a "%OUTPUT_DIR%"') do (
    if /i not "%%I"=="build" if /i not "%%I"=="bin" if /i not "%%I"=="result" if /i not "%%I"=="packages" (
        if exist "%OUTPUT_DIR%\%%I\*" (
            xcopy /e /i /q /y "%OUTPUT_DIR%\%%I" "%STAGING_DIR%\%SOURCE_NAME%\%%I\" >nul
        ) else (
            copy /y "%OUTPUT_DIR%\%%I" "%STAGING_DIR%\%SOURCE_NAME%\" >nul
        )
    )
)

xcopy /e /i /q /y "%EMBEDDED_RESULT_DIR%\*" "%STAGING_DIR%\%EMBEDDED_NAME%\" >nul
xcopy /e /i /q /y "%EXTENSION_RESULT_DIR%\*" "%STAGING_DIR%\%EXTENSION_NAME%\" >nul

powershell -NoProfile -Command "Compress-Archive -LiteralPath '%STAGING_DIR%\%SOURCE_NAME%' -DestinationPath '%SOURCE_ZIP%' -CompressionLevel Optimal"
if errorlevel 1 (
    echo Failed to create "%SOURCE_ZIP%".
    exit /b 1
)
powershell -NoProfile -Command "Compress-Archive -LiteralPath '%STAGING_DIR%\%EMBEDDED_NAME%' -DestinationPath '%EMBEDDED_ZIP%' -CompressionLevel Optimal"
if errorlevel 1 (
    echo Failed to create "%EMBEDDED_ZIP%".
    exit /b 1
)
powershell -NoProfile -Command "Compress-Archive -LiteralPath '%STAGING_DIR%\%EXTENSION_NAME%' -DestinationPath '%EXTENSION_ZIP%' -CompressionLevel Optimal"
if errorlevel 1 (
    echo Failed to create "%EXTENSION_ZIP%".
    exit /b 1
)

rmdir /s /q "%STAGING_DIR%"

echo.
echo Done.
echo Source: %SOURCE_ZIP%
echo Embedded: %EMBEDDED_ZIP%
echo Extension: %EXTENSION_ZIP%
exit /b 0

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
