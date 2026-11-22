@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "ROOT=%CD%"
set "SRC=%ROOT%\source"
set "GAME_DATA=%ROOT%\GameData"
set "DIST=%ROOT%\dist"
set "GENERATED=%ROOT%\build_generated"
set "TOOLS_BUILD=%ROOT%\build-tools"
set "GAME_BUILD=%ROOT%\build-game"
set "CMAKE_EXE="
set "CMAKE_BIN="
set "CTEST_EXE="
set "VSWHERE="
set "VSROOT="

echo ================================================================
echo  ORACLES PC RECOMP v0.2.4 - INDEPENDENT STATIC RECOMPILER BUILD
echo ================================================================
echo.

rem ---------------------------------------------------------------
rem Locate CMake. Accept PATH, standard standalone installs, and the
rem CMake copy bundled with Visual Studio.
rem ---------------------------------------------------------------
for /f "delims=" %%C in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%~fC"

if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if defined ProgramFiles(x86) if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles(x86)%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if defined LOCALAPPDATA if exist "%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe" set "CMAKE_EXE=%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if defined USERPROFILE if exist "%USERPROFILE%\scoop\apps\cmake\current\bin\cmake.exe" set "CMAKE_EXE=%USERPROFILE%\scoop\apps\cmake\current\bin\cmake.exe"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

rem Find the newest VS installation that actually has the MSVC x64 tools.
if exist "!VSWHERE!" (
  for /f "usebackq tokens=*" %%V in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do if not defined VSROOT set "VSROOT=%%V"
)

rem Visual Studio also ships CMake in this location when its CMake component is installed.
if not defined CMAKE_EXE if defined VSROOT if exist "!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not defined CMAKE_EXE (
  echo [ERROR] CMake 3.20 or newer was not found.
  echo         Checked PATH, standard CMake install folders, Scoop, and Visual Studio's bundled CMake.
  echo         Install CMake or add its bin folder to PATH, then run this script again.
  exit /b 1
)

for %%I in ("!CMAKE_EXE!") do set "CMAKE_BIN=%%~dpI"
if exist "!CMAKE_BIN!ctest.exe" set "CTEST_EXE=!CMAKE_BIN!ctest.exe"
if not defined CTEST_EXE for /f "delims=" %%T in ('where ctest.exe 2^>nul') do if not defined CTEST_EXE set "CTEST_EXE=%%~fT"
if not defined CTEST_EXE (
  echo [ERROR] CTest was not found beside CMake or on PATH.
  echo         Reinstall CMake with the testing tools included.
  exit /b 1
)

echo [OK] CMake: "!CMAKE_EXE!"
"!CMAKE_EXE!" --version
if errorlevel 1 exit /b 1

rem ---------------------------------------------------------------
rem Ensure that both cl.exe and nmake.exe are available. A regular
rem command prompt is supported: VsDevCmd initializes the x64 tools.
rem Use NMake Makefiles so CMake uses the initialized compiler instead
rem of trying to rediscover an incomplete Visual Studio generator.
rem ---------------------------------------------------------------
where cl.exe >nul 2>nul
set "HAVE_CL=%ERRORLEVEL%"
where nmake.exe >nul 2>nul
set "HAVE_NMAKE=%ERRORLEVEL%"

if not "!HAVE_CL!!HAVE_NMAKE!"=="00" (
  if not defined VSROOT (
    if exist "!VSWHERE!" (
      for /f "usebackq tokens=*" %%V in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do if not defined VSROOT set "VSROOT=%%V"
    )
  )
  if not defined VSROOT (
    echo [ERROR] The MSVC x64 C++ toolchain was not found.
    echo         Install Visual Studio 2022 or newer with the "Desktop development with C++" workload.
    echo         The required component is the MSVC x64/x86 build tools.
    exit /b 1
  )
  if not exist "!VSROOT!\Common7\Tools\VsDevCmd.bat" (
    echo [ERROR] VsDevCmd.bat was not found under:
    echo         !VSROOT!
    exit /b 1
  )
  echo [OK] Initialising MSVC from: !VSROOT!
  call "!VSROOT!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
  if errorlevel 1 (
    echo [ERROR] Visual Studio's developer environment could not be initialised.
    exit /b 1
  )
)

where cl.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] cl.exe is still unavailable after MSVC initialisation.
  echo         Install the MSVC x64/x86 C++ build tools in Visual Studio Installer.
  exit /b 1
)
where nmake.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] nmake.exe is still unavailable after MSVC initialisation.
  echo         Repair the Visual Studio C++ Build Tools installation.
  exit /b 1
)
for /f "delims=" %%C in ('where cl.exe 2^>nul') do if not defined CL_EXE set "CL_EXE=%%~fC"
echo [OK] MSVC compiler: !CL_EXE!
echo [OK] NMake is available.

if not exist "!SRC!\CMakeLists.txt" (
  echo [ERROR] source\CMakeLists.txt was not found. Extract the complete ZIP first.
  exit /b 1
)
if not exist "!GAME_DATA!\OracleOfAges.gbc" (
  echo [ERROR] Missing GameData\OracleOfAges.gbc
  echo         Add your own legally obtained Oracle of Ages ROM.
  exit /b 1
)
if not exist "!GAME_DATA!\OracleOfSeasons.gbc" (
  echo [ERROR] Missing GameData\OracleOfSeasons.gbc
  echo         Add your own legally obtained Oracle of Seasons ROM.
  exit /b 1
)

if not exist "%DIST%" mkdir "%DIST%"
if not exist "%DIST%\GameData" mkdir "%DIST%\GameData"
if exist "%DIST%\Oracles.exe" del /Q "%DIST%\Oracles.exe"
if not exist "%GENERATED%" mkdir "%GENERATED%"

rem NMake uses the active x64 MSVC environment. Remove caches created by
rem another generator (for example, the Visual Studio 18 2026 generator).
call :prepare_nmake_build "%TOOLS_BUILD%"
call :prepare_nmake_build "%GAME_BUILD%"

 echo [1/5] Configuring the independent compiler with NMake...
"!CMAKE_EXE!" -G "NMake Makefiles" -S "%SRC%" -B "%TOOLS_BUILD%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :failed

 echo [2/5] Building the SM83 static recompiler...
"!CMAKE_EXE!" --build "%TOOLS_BUILD%" --target oracles-recompiler oracles-test-binaries
if errorlevel 1 goto :failed
if not exist "!TOOLS_BUILD!\oracles-render-layout-tests.exe" (
  echo [ERROR] The renderer layout test executable was not built.
  echo         Expected: "!TOOLS_BUILD!\oracles-render-layout-tests.exe"
  echo         Check the build output above for the first compiler error.
  goto :failed
)
if not exist "!TOOLS_BUILD!\oracles-input-bindings-tests.exe" (
  echo [ERROR] The keyboard binding test executable was not built.
  echo         Expected: "!TOOLS_BUILD!\oracles-input-bindings-tests.exe"
  echo         Check the build output above for the first compiler error.
  goto :failed
)
"!CTEST_EXE!" --test-dir "%TOOLS_BUILD%" --output-on-failure
if errorlevel 1 goto :failed
copy /Y "%GAME_DATA%\OracleOfAges.gbc" "%DIST%\GameData\OracleOfAges.gbc" >nul
if errorlevel 1 goto :failed
copy /Y "%GAME_DATA%\OracleOfSeasons.gbc" "%DIST%\GameData\OracleOfSeasons.gbc" >nul
if errorlevel 1 goto :failed

 echo [3/5] Running static analysis and native C++ code generation...
"%TOOLS_BUILD%\oracles-recompiler.exe" --game ages --input "%GAME_DATA%\OracleOfAges.gbc" --output "%GENERATED%\ages_native.cpp" --manifest "%DIST%\GameData\OracleOfAges.orcmeta"
if errorlevel 1 goto :failed
"%TOOLS_BUILD%\oracles-recompiler.exe" --game seasons --input "%GAME_DATA%\OracleOfSeasons.gbc" --output "%GENERATED%\seasons_native.cpp" --manifest "%DIST%\GameData\OracleOfSeasons.orcmeta"
if errorlevel 1 goto :failed

 echo [4/5] Configuring the combined native application with NMake...
"!CMAKE_EXE!" -G "NMake Makefiles" -S "%SRC%" -B "%GAME_BUILD%" -DCMAKE_BUILD_TYPE=Release -DORACLES_BUILD_GAME=ON -DAGES_NATIVE="%GENERATED%\ages_native.cpp" -DSEASONS_NATIVE="%GENERATED%\seasons_native.cpp"
if errorlevel 1 goto :failed

 echo [5/5] Compiling Oracles.exe...
"!CMAKE_EXE!" --build "%GAME_BUILD%" --target Oracles
if errorlevel 1 goto :failed

if not exist "!DIST!\Oracles.exe" (
  echo [ERROR] Build completed but dist\Oracles.exe was not produced.
  exit /b 1
)

echo.
echo ================================================================
echo  BUILD COMPLETE
echo  Output: %DIST%\Oracles.exe
echo  Metadata: %DIST%\GameData\OracleOfAges.orcmeta
echo            %DIST%\GameData\OracleOfSeasons.orcmeta
echo ================================================================
echo.
echo Launch dist\Oracles.exe to open the configuration launcher.
echo Choose Ages or Seasons, GPU, key bindings, 4:3 or 16:9, and window mode.
echo Click LAUNCH! to close the launcher and open the game window.
echo Default keyboard: Arrows, Z (A), X (B), Enter (Start), Right Shift (Select).
echo XInput: D-pad, A, B, Start, Back.
echo.
echo Compatibility note: native blocks and interpreter fallback are active,
echo but the PPU is scanline-oriented and the game builds are still in development.
exit /b 0

:prepare_nmake_build
set "TARGET_BUILD=%~1"
rem Use delayed expansion inside this block. Percent expansion of a path
rem containing parentheses (for example, Downloads\Project(1)) would be
rem reparsed as batch syntax and can abort the script before CMake runs.
if exist "!TARGET_BUILD!\CMakeCache.txt" (
  findstr /C:"CMAKE_GENERATOR:INTERNAL=NMake Makefiles" "!TARGET_BUILD!\CMakeCache.txt" >nul
  if errorlevel 1 (
    echo [INFO] Removing stale CMake cache from a different generator: !TARGET_BUILD!
    rmdir /S /Q "!TARGET_BUILD!"
  )
)
exit /b 0

:failed
echo.
echo [BUILD FAILED] Review the first compiler/CMake error above.
exit /b 1
