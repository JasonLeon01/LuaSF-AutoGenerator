@echo off
rem download_lib.bat - Download, verify, extract, and rename a source archive.
rem
rem Usage:
rem   call download_lib.bat DisplayName URL ArchiveFile ExtractedFolder TargetFolder [SHA256]

setlocal

set "LIB_NAME=%~1"
set "URL=%~2"
set "ARCHIVE=%~3"
set "SRC_FOLDER=%~4"
set "DEST_FOLDER=%~5"
set "EXPECTED_SHA256=%~6"

echo Downloading %LIB_NAME%...
powershell -Command "Invoke-WebRequest -Uri '%URL%' -OutFile '%ARCHIVE%'"
if errorlevel 1 (
    echo Failed to download %LIB_NAME%.
    exit /b 1
)

if not "%EXPECTED_SHA256%"=="" (
    echo Verifying %LIB_NAME% SHA-256...
    powershell -Command "$actual = (Get-FileHash -Algorithm SHA256 -LiteralPath '%ARCHIVE%').Hash.ToLowerInvariant(); if ($actual -ne '%EXPECTED_SHA256%') { Write-Error ('SHA-256 mismatch: ' + $actual); exit 1 }"
    if errorlevel 1 (
        del "%ARCHIVE%"
        exit /b 1
    )
)

echo Extracting %LIB_NAME%...
if /I "%ARCHIVE:~-7%"==".tar.gz" (
    tar -xzf "%ARCHIVE%" -C third_party
) else (
    powershell -Command "Expand-Archive -Path '%ARCHIVE%' -DestinationPath 'third_party' -Force"
)
if errorlevel 1 (
    echo Failed to extract %LIB_NAME%.
    del "%ARCHIVE%"
    exit /b 1
)

del "%ARCHIVE%"

if exist "third_party\%SRC_FOLDER%" (
    ren "third_party\%SRC_FOLDER%" "%DEST_FOLDER%"
) else (
    echo %LIB_NAME% source folder not found: third_party\%SRC_FOLDER%
    exit /b 1
)

endlocal
exit /b 0
