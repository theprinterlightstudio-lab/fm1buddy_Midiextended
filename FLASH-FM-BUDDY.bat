@echo off
rem FM Buddy 2.0 - back up the board, then flash it. One file, double-click it.
rem
rem Same sequence as FLASH-FM-BUDDY.command on the Mac:
rem   1. works from its own folder, wherever you put it
rem   2. finds python and esptool, installing esptool into .pydeps if it has to
rem   3. finds the board (pcbfun_boot.py, the same finder the repo's tools\pcbfun_flash.bat uses)
rem   4. reads the board's settings and current app back to this folder
rem   5. refuses to go on unless the backup is the right size
rem   6. asks before writing anything
rem   7. writes the app to app0 (0x10000), clears otadata (0xe000)
rem
rem Settings (nvs at 0x9000) and the bootloader are never written.
rem
rem NOT TESTED ON WINDOWS. It mirrors tools\pcbfun_flash.bat, which is proven, and adds the backup
rem that one does not do. Read what it prints rather than trusting it blindly the first time.
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "APP=fm1buddy-pcbfun-s3-28-app.bin"
if not "%~1"=="" set "APP=%~1"

echo FM Buddy 2.0 - flash
echo ====================
echo.
echo Folder: %CD%

if not exist "%APP%" (
  echo.
  echo STOPPED: %APP% is not in this folder.
  goto fail
)
echo %APP% | find /i "factory" >nul
if not errorlevel 1 (
  echo.
  echo STOPPED: that is the factory image. Writing it would erase the board's settings.
  goto fail
)

rem ----------------------------------------------------------------- python and esptool
set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not exist "%PY%" set "PY=py"
"%PY%" --version >nul 2>&1
if errorlevel 1 (
  echo.
  echo STOPPED: no python found. Install Python 3, or run this from the repo where
  echo          %USERPROFILE%\.platformio\penv exists.
  goto fail
)
if not exist ".pydeps\esptool" (
  echo Installing esptool into .pydeps ...
  "%PY%" -m pip install --quiet --disable-pip-version-check --target .pydeps esptool
  if errorlevel 1 (
    echo.
    echo STOPPED: could not install esptool.
    goto fail
  )
)
set ESPTOOL="%PY%" -c "import runpy, sys; sys.path.insert(0, r'.pydeps'); runpy.run_module('esptool', run_name='__main__')"

rem ----------------------------------------------------------------- the board
echo.
echo Finding the board ...
set "PORT="
for /f "usebackq delims=" %%p in (`"%PY%" pcbfun_boot.py`) do set "PORT=%%p"
if "%PORT%"=="" (
  echo.
  echo STOPPED: the board was not found.
  echo   - use a USB cable that carries DATA ^(a charge-only cable is the usual reason^)
  echo   - or on the board: Settings ^> Flash mode ^> Restart for flashing
  goto fail
)
echo Board: %PORT%

rem ----------------------------------------------------------------- back up
for /f "tokens=2 delims==" %%t in ('wmic os get localdatetime /value ^| find "="') do set "T=%%t"
set "BACKUP=fm1buddy-backup-%T:~0,8%-%T:~8,6%"
echo.
echo Backing up to %BACKUP% ^(writes nothing to the board^) ...
mkdir "%BACKUP%" 2>nul

echo   settings and boot data (64 KB) ...
%ESPTOOL% --chip esp32s3 --port %PORT% --before default-reset --after hard-reset read-flash 0x0 0x10000 "%BACKUP%\head-0x0-0x10000.bin"
if errorlevel 1 (
  echo.
  echo STOPPED: could not read the board's settings.
  goto fail
)
echo   the app it is running now (6.5 MB, a minute or so) ...
%ESPTOOL% --chip esp32s3 --port %PORT% --before default-reset --after hard-reset read-flash 0x10000 0x640000 "%BACKUP%\app0-0x10000-0x640000.bin"
if errorlevel 1 (
  echo.
  echo STOPPED: could not read the board's app.
  goto fail
)

rem ----------------------------------------------------------------- is the backup real
set "HEADSZ=0"
set "APP0SZ=0"
for %%f in ("%BACKUP%\head-0x0-0x10000.bin") do set "HEADSZ=%%~zf"
for %%f in ("%BACKUP%\app0-0x10000-0x640000.bin") do set "APP0SZ=%%~zf"
if not "%HEADSZ%"=="65536" (
  echo.
  echo STOPPED: the settings backup is %HEADSZ% bytes, expected 65536.
  echo          Without a good backup this is not reversible.
  goto fail
)
if not "%APP0SZ%"=="6553600" (
  echo.
  echo STOPPED: the app backup is %APP0SZ% bytes, expected 6553600.
  echo          Without a good backup this is not reversible.
  goto fail
)
echo   backup good: %HEADSZ% + %APP0SZ% bytes

rem ----------------------------------------------------------------- ask
echo.
echo About to write %APP% to the board.
echo Its settings ^(Wi-Fi, theme, MIDI setup, touch calibration^) are kept.
echo If anything goes wrong, %BACKUP% puts it back.
echo.
set "ANSWER="
set /p "ANSWER=Flash it now? Type yes and press enter: "
if /i not "%ANSWER%"=="yes" (
  echo.
  echo Not flashed. The backup in %BACKUP% is still there.
  goto done
)

rem ----------------------------------------------------------------- flash
echo.
echo Writing the app to app0 (0x10000) ...
%ESPTOOL% --chip esp32s3 --port %PORT% --before default-reset --after no-reset write-flash 0x10000 "%APP%"
if errorlevel 1 (
  echo.
  echo STOPPED: writing the app failed. The board still has its old app; nothing else changed.
  goto fail
)
echo.
echo Clearing otadata (0xe000), so it boots the new app ...
%ESPTOOL% --chip esp32s3 --port %PORT% --before default-reset --after hard-reset erase-region 0xe000 0x2000
if errorlevel 1 (
  echo.
  echo STOPPED: clearing otadata failed. The new app is in app0; run this again.
  goto fail
)

echo.
echo Done. The board restarts on its own.
echo.
echo Check:  Settings ^> About        version reads dev
echo         Settings ^> Wi-Fi        your network is still there
echo         Touch page ^> mode list  Field is between Touche and Sequencer
echo         any list                the scrollbar is a fat bar you can drag
echo.
echo Backup kept in %BACKUP% - keep it until the new firmware has proved itself.
goto done

:fail
echo.
echo Nothing was written to the board.

:done
echo.
pause
