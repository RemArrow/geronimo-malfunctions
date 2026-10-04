@echo off
rem Builds the Malfunctions GML plugin and its in-game development harness.
rem   build.bat            build into build\
rem   build.bat install    also copy the plugin into the game (refuses while the game runs)
rem
rem Needs the GML SDK 2.2+ (github.com/RemArrow/geronimo-mod-loader): the external\gml submodule,
rem or set GML_SDK to its include folder. Set GML_GAME_DIR to your ...\Geronimo\Binaries\Win64 if
rem the game is not in the default Steam library.
setlocal
set ROOT=%~dp0
set OUT=%ROOT%build
set SDK=%ROOT%external\gml\include
if defined GML_SDK set SDK=%GML_SDK%
set GAME=C:\Program Files (x86)\Steam\steamapps\common\GERONIMO\Geronimo\Binaries\Win64
if defined GML_GAME_DIR set GAME=%GML_GAME_DIR%

if not exist "%SDK%\GML\GML.hpp" (
  echo GML SDK not found at %SDK%
  echo Run: git submodule update --init   or set GML_SDK
  exit /b 1
)
if not defined VCToolsInstallDir (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
for %%D in ("%OUT%\obj" "%OUT%\Malfunctions" "%OUT%\tests\MalfunctionsDev") do if not exist "%%~D" mkdir "%%~D"
set CFLAGS=/nologo /std:c++20 /O2 /MT /EHsc /W3 /Zi /DUNICODE /D_UNICODE /I"%SDK%"
set LFLAGS=/DEBUG /INCREMENTAL:NO

if exist "%ROOT%src\*.cpp" (
  echo === plugin
  cl %CFLAGS% /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%src\*.cpp" /Fe"%OUT%\Malfunctions\Malfunctions.dll" /link %LFLAGS% || exit /b 1
)
echo === dev harness (never installed)
cl %CFLAGS% /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%tests\MalfunctionsDev\MalfunctionsDev.cpp" /Fe"%OUT%\tests\MalfunctionsDev\MalfunctionsDev.dll" /link %LFLAGS% || exit /b 1
for /r "%OUT%" %%F in (*.exp *.lib *.ilk) do del /q "%%F"

if /i "%1"=="install" (
  if not exist "%OUT%\Malfunctions\Malfunctions.dll" ( echo nothing to install yet & exit /b 1 )
  tasklist /FI "IMAGENAME eq Geronimo-Win64-Shipping.exe" | find /I "Geronimo-Win64" >nul && (
    echo Geronimo is running - close it first.
    exit /b 1
  )
  echo === install to "%GAME%\GML\plugins\Malfunctions"
  if not exist "%GAME%\GML\plugins\Malfunctions" mkdir "%GAME%\GML\plugins\Malfunctions"
  copy /y "%OUT%\Malfunctions\Malfunctions.dll" "%GAME%\GML\plugins\Malfunctions\" >nul || exit /b 1
)
echo === done
exit /b 0
