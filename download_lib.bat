@echo off
:: download_lib.bat — Download, extract, and rename a GitHub-hosted zip archive.
::
:: Usage:
::   call download_lib.bat <DisplayName> <URL> <ZipFile> <ExtractedFolder> <TargetFolder>
::
:: Parameters:
::   DisplayName     — Human-readable name shown in log messages  (e.g. SFML)
::   URL             — Full download URL
::   ZipFile         — Local filename for the downloaded zip      (e.g. sfml.zip)
::   ExtractedFolder — Folder name GitHub creates inside third_party after extraction
::   TargetFolder    — Desired final folder name inside third_party

setlocal

set "LIB_NAME=%~1"
set "URL=%~2"
set "ZIP=%~3"
set "SRC_FOLDER=%~4"
set "DEST_FOLDER=%~5"

echo Downloading %LIB_NAME%...
powershell -Command "Invoke-WebRequest -Uri '%URL%' -OutFile '%ZIP%'"
if errorlevel 1 (
    echo Failed to download %LIB_NAME%.
    exit /b 1
)

echo Extracting %LIB_NAME%...
powershell -Command "Expand-Archive -Path '%ZIP%' -DestinationPath 'third_party' -Force"
if errorlevel 1 (
    echo Failed to extract %LIB_NAME%.
    del "%ZIP%"
    exit /b 1
)

del "%ZIP%"

if exist "third_party\%SRC_FOLDER%" (
    ren "third_party\%SRC_FOLDER%" "%DEST_FOLDER%"
) else (
    echo %LIB_NAME% source folder not found: third_party\%SRC_FOLDER%
    exit /b 1
)

endlocal
exit /b 0