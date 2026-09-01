param([string]$ExePath = "$PSScriptRoot\..\build\Release\QuiklightWindows.exe")
$ExePath = (Resolve-Path $ExePath).Path
$workDir = Split-Path $ExePath
$action = New-ScheduledTaskAction -Execute $ExePath -WorkingDirectory $workDir
$trigger = New-ScheduledTaskTrigger -AtLogOn
Register-ScheduledTask -TaskName "Quiklight Windows" -Action $action -Trigger $trigger -Description "Start Quiklight Windows ambilight at user logon" -Force
Write-Host "Installed startup task: Quiklight Windows"
