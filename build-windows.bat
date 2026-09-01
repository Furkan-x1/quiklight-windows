@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

echo ========================================
echo   Quiklight Windows - Build
echo ========================================
echo.

rem ------------------------------------------------------------
rem Find Visual Studio and load its C++ build environment FIRST.
rem This must happen even when CMake is already in PATH.
rem ------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Workload.VCTools -property installationPath`) do (
        set "VSINSTALL=%%V"
    )
)

if defined VSINSTALL (
    echo Visual Studio found:
    echo   !VSINSTALL!
    echo Loading MSVC build environment...
    call "!VSINSTALL!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to initialize the Visual Studio C++ environment.
        pause
        exit /b 1
    )
) else (
    echo WARNING: Visual Studio C++ Build Tools were not found via vswhere.
    echo Trying the existing compiler environment...
    echo.
)

echo.

rem ------------------------------------------------------------
rem Verify the compiler. CMake cannot build this project without it.
rem ------------------------------------------------------------
where cl >nul 2>&1
if errorlevel 1 (
    echo ERROR: Microsoft C++ compiler ^(cl.exe^) was not found.
    echo.
    echo Install Visual Studio 2022 / Build Tools with:
    echo   Desktop development with C++
    echo.
    echo Required components include MSVC and a Windows SDK.
    echo.
    pause
    exit /b 1
)

for /f "tokens=*" %%V in ('cl 2^>^&1 ^| findstr /C:"Version"') do echo Compiler: %%V

echo.

rem ------------------------------------------------------------
rem Find CMake.
rem ------------------------------------------------------------
where cmake >nul 2>&1
if errorlevel 1 (
    if exist "!VSINSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "CMAKE=!VSINSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    ) else (
        echo ERROR: CMake was not found.
        echo Install CMake or the Visual Studio CMake component.
        pause
        exit /b 1
    )
) else (
    set "CMAKE=cmake"
)

for /f "delims=" %%V in ('"!CMAKE!" --version') do (
    echo %%V
    goto :cmake_version_done
)
:cmake_version_done

echo.
echo Configuring x64 Release build...

rem Explicitly use the Visual Studio generator. This avoids CMake selecting
rem a generator/toolchain that has no CXX compiler available.
"!CMAKE!" -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto :build_failed

echo.
echo Building Release...
"!CMAKE!" --build build --config Release --parallel
if errorlevel 1 goto :build_failed

set "EXE=%CD%\build\Release\QuiklightWindows.exe"
if not exist "%EXE%" (
    echo.
    echo ERROR: Build completed but the executable was not found:
    echo   %EXE%
    pause
    exit /b 1
)

echo.
echo ========================================
echo   BUILD SUCCESSFUL
echo ========================================
echo.
echo Executable:
echo   %EXE%
echo.
choice /C YN /N /M "Run QuiklightWindows.exe now? [Y/N] "
if errorlevel 2 goto :done
start "" "%EXE%"

done:
echo.
echo Done.
exit /b 0

:build_failed
echo.
echo ========================================
echo   BUILD FAILED
echo ========================================
echo.
echo The compiler/configuration output above contains the cause.
echo.
pause
exit /b 1
