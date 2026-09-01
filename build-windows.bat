@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ========================================
echo  Quiklight Windows - Build
echo ========================================

if exist build (
  echo Cleaning old CMake build directory...
  rmdir /s /q build
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDEVCMD="

if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%V"
)

if defined VSROOT if exist "%VSROOT%\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%VSROOT%\Common7\Tools\VsDevCmd.bat"

if not defined VSDEVCMD (
  for %%P in (
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
  ) do if exist "%%~P" if not defined VSDEVCMD set "VSDEVCMD=%%~P"
)

if not defined VSDEVCMD (
  echo ERROR: Visual Studio 2022 C++ tools were not found.
  pause
  exit /b 1
)

call "%VSDEVCMD%" -arch=x64 >nul
if errorlevel 1 (
  echo ERROR: Failed to initialize the Visual Studio C++ environment.
  pause
  exit /b 1
)

where cl >nul 2>nul
if errorlevel 1 (
  echo ERROR: cl.exe was not found. Install Desktop development with C++.
  pause
  exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
  echo ERROR: CMake was not found in PATH.
  echo Install CMake or enable the CMake component in Visual Studio.
  pause
  exit /b 1
)

echo Configuring x64 Release build...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto :failed

echo.
echo Building Release...
cmake --build build --config Release --parallel
if errorlevel 1 goto :failed

echo.
echo ========================================
echo  BUILD SUCCESSFUL
echo ========================================
echo.
echo Executable:
echo %CD%\build\Release\QuiklightWindows.exe
echo.
choice /C YN /N /M "Run Quiklight now? [Y/N] "
if errorlevel 2 goto :done
start "Quiklight Windows" "%CD%\build\Release\QuiklightWindows.exe"
goto :done

:failed
echo.
echo ========================================
echo  BUILD FAILED
echo ========================================
echo.
pause
exit /b 1

:done
endlocal
