param([string]$Source = 'C:\StarlightFixture', [string]$Results = 'C:\StarlightResults')
$ErrorActionPreference = 'Stop'
$checks = [System.Collections.Generic.List[string]]::new()
function Assert-Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw $Name }
    $checks.Add($Name)
}
function Invoke-Launcher([string]$Executable, [string]$Argument, [int]$Expected = 0) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Argument -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(60000)) { throw "Launcher timed out: $Argument" }
    Assert-Check ($process.ExitCode -eq $Expected) "$Argument returned $Expected"
}
try {
    # This fixture contains only the launcher, not a distributable engine.
    $portable = Join-Path $env:USERPROFILE 'Desktop\Starlight Portable Test'
    $installed = Join-Path $env:LOCALAPPDATA 'Programs\Starlight Preview'
    $registration = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\StarlightPreview'
    $shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Starlight Preview.lnk'
    Assert-Check (-not (Test-Path -LiteralPath $portable)) 'Fresh portable destination'
    Assert-Check (-not (Test-Path -LiteralPath $installed)) 'Fresh installation destination'
    Copy-Item -LiteralPath (Join-Path $Source 'package') -Destination $portable -Recurse
    $launcher = Join-Path $portable 'Starlight.exe'
    Invoke-Launcher $launcher '--self-test'
    Invoke-Launcher $launcher '--verify-package'
    Invoke-Launcher $launcher '--install-only'
    Assert-Check (Test-Path -LiteralPath $shortcut) 'Start menu shortcut created'
    Assert-Check ((Get-ItemProperty -LiteralPath $registration).Publisher -eq 'ChaosFox') 'Publisher registered correctly'
    Invoke-Launcher (Join-Path $installed 'Starlight.exe') '--verify-package'
    Invoke-Launcher $launcher '--install-only' 1
    $userData = Join-Path $env:LOCALAPPDATA 'Starlight'
    New-Item -ItemType Directory -Path $userData -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $userData 'keep.txt') -Value 'preserve user settings'
    Set-Content -LiteralPath (Join-Path $installed 'user-project.txt') -Value 'preserve project'
    Invoke-Launcher (Join-Path $installed 'Starlight.exe') '--uninstall-quiet'
    $deadline = (Get-Date).AddSeconds(45)
    while (((Test-Path -LiteralPath $registration) -or (Test-Path -LiteralPath $shortcut)) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    Assert-Check (-not (Test-Path -LiteralPath (Join-Path $installed 'Starlight.exe'))) 'Installed launcher removed'
    Assert-Check (-not (Test-Path -LiteralPath $registration)) 'Uninstall registration removed'
    Assert-Check (-not (Test-Path -LiteralPath $shortcut)) 'Start menu shortcut removed'
    Assert-Check (Test-Path -LiteralPath (Join-Path $installed 'user-project.txt')) 'User project preserved'
    Assert-Check (Test-Path -LiteralPath (Join-Path $userData 'keep.txt')) 'User settings preserved'
    Invoke-Launcher $launcher '--verify-package'
    $result = @{ success = $true; scope = 'Native launcher fixture lifecycle only; no engine feature certification'; checks = @($checks) }
} catch {
    $result = @{ success = $false; error = $_.Exception.Message; checks = @($checks) }
}
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Results 'launcher-result.json') -Encoding UTF8
