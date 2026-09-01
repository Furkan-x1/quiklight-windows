$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsInstall = $null
if (Test-Path $vswhere) {
    $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Workload.VCTools -property installationPath
}

if ($vsInstall) {
    $vsDevCmd = Join-Path $vsInstall 'Common7\Tools\VsDevCmd.bat'
    if (Test-Path $vsDevCmd) {
        cmd /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 && set" | ForEach-Object {
            if ($_ -match '^(.*?)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
        }
    }
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Microsoft C++ compiler (cl.exe) was not found. Install Visual Studio 2022/Build Tools with Desktop development with C++.'
}

cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
cmake --build build --config Release --parallel
Write-Host "Built: $((Resolve-Path .\build\Release\QuiklightWindows.exe).Path)"
