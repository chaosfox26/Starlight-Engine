param([string]$Source='C:\StarlightInput',[string]$Results='C:\StarlightResults')
$ErrorActionPreference='Stop'
$checks=[System.Collections.Generic.List[string]]::new()
function Save-State([string]$Step) {
    @{step=$Step;checks=@($checks);time=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Results 'progress.json')
}
function Assert-Check([bool]$Good,[string]$Name) {
    if(-not $Good){throw $Name}; $checks.Add($Name); Save-State $Name
}
function Run-Program([string]$Exe,[string]$Arguments,[int]$Timeout=120000) {
    $p=Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru -WindowStyle Hidden
    if(-not $p.WaitForExit($Timeout)){throw "Timed out: $Exe"};$p.Refresh();return $p.ExitCode
}
try {
    $root=Join-Path $env:USERPROFILE 'Desktop\Starlight Alpha One Test'
    Assert-Check (-not(Test-Path -LiteralPath $root)) 'Fresh portable destination'
    Copy-Item -LiteralPath (Join-Path $Source 'package') -Destination $root -Recurse
    $env:PATH="$env:WINDIR\System32;$env:WINDIR"
    Assert-Check (-not(Get-Command python.exe,cl.exe,cmake.exe -ErrorAction SilentlyContinue)) 'No separately installed Python or build tools'
    $launcher=Join-Path $root 'Starlight.exe'
    Assert-Check ((Run-Program $launcher '--verify-package') -eq 0) 'Portable manifest verified'
    $manifest=Get-Content -LiteralPath (Join-Path $root 'starlight-package.json') -Raw | ConvertFrom-Json
    $graphicsEvidence=Join-Path $Results 'graphics-environment.json'
    Assert-Check ((Run-Program (Join-Path $root 'runtime\Release\Starlight.NativeRenderChecks.exe') ('--graphics-environment "'+$graphicsEvidence+'"')) -eq 0) 'Graphics environment diagnostics recorded'
    $gui=Join-Path $root $manifest.analysis
    $audit=Join-Path $root 'runtime\Release\Starlight.NativeAudit.exe'
    $run=Join-Path $root 'analysis\clean-sandbox-run'
    Save-State 'Full native GUI analysis running'
    $p=Start-Process -FilePath $gui -ArgumentList "--native `"$audit`" `"$root`" `"$run`" both" -PassThru
    $deadline=[DateTime]::UtcNow.AddMinutes(35)
    do {
        Start-Sleep -Seconds 2
        $p.Refresh()
        if($p.HasExited -and -not(Test-Path -LiteralPath (Join-Path $run 'final-status.txt'))){throw 'Analysis GUI exited before reporting'}
        if([DateTime]::UtcNow -gt $deadline){throw 'Full analysis deadline exceeded'}
        $workers=Get-CimInstance Win32_Process -Filter "Name='Starlight.NativeAudit.exe'"
    } until ((Test-Path -LiteralPath (Join-Path $run 'final-status.txt')) -and -not $workers)
    $p.CloseMainWindow() | Out-Null
    Assert-Check ($p.WaitForExit(15000)) 'Analysis window closed before relocation'
    Assert-Check (Test-Path -LiteralPath (Join-Path $run 'AUDIT-SHAREABLE-REPORT.md')) 'Full native analysis produced shareable report'
    $summary=Get-Content -LiteralPath (Join-Path $run 'audit-summary.json') -Raw | ConvertFrom-Json
    Copy-Item -LiteralPath $run -Destination (Join-Path $Results 'full-native-run') -Recurse
    Save-State 'Testing portable relocation'
    $portableData=Join-Path $root 'data'
    New-Item -ItemType Directory -Path $portableData -Force | Out-Null
    'Portable user data' | Set-Content -LiteralPath (Join-Path $portableData 'relocation-test.txt')
    $desktopRoot=[IO.Path]::GetFullPath((Join-Path $env:USERPROFILE 'Desktop'))
    $expectedRoot=Join-Path $desktopRoot 'Starlight Alpha One Test'
    $relocated=Join-Path $desktopRoot ('Starlight Moved ' + [char]0x03A9)
    Assert-Check ([IO.Path]::GetFullPath($root) -eq $expectedRoot -and -not(Test-Path -LiteralPath $relocated)) 'Relocation uses isolated Sandbox test directories'
    Move-Item -LiteralPath $root -Destination $relocated
    $root=$relocated
    $launcher=Join-Path $root 'Starlight.exe'
    Assert-Check ((Run-Program $launcher '--verify-package') -eq 0) 'Relocated portable manifest verified'
    Assert-Check ((Run-Program (Join-Path $root $manifest.editor) '--describe') -eq 0) 'Relocated editor command interface runs without development dependencies'
    Assert-Check ((Get-Content -LiteralPath (Join-Path $root 'data\relocation-test.txt') -Raw).Contains('Portable user data')) 'Portable data moved with its application'
    Save-State 'Testing local installation'
    Assert-Check ((Run-Program $launcher '--install-only') -eq 0) 'Local installation completed'
    $installed=Join-Path $env:LOCALAPPDATA 'Programs\Starlight Preview'
    $installedLauncher=Join-Path $installed 'Starlight.exe'
    Assert-Check ((Run-Program $installedLauncher '--verify-package') -eq 0) 'Installed manifest verified'
    $reg='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\StarlightPreview'
    Assert-Check ((Get-ItemProperty -LiteralPath $reg).Publisher -eq 'ChaosFox') 'Publisher is ChaosFox'
    $shell=New-Object -ComObject WScript.Shell
    $programs=[Environment]::GetFolderPath('Programs')
    $analysisLink=Join-Path $programs 'Starlight Analysis.lnk'
    $editorLink=Join-Path $programs 'Starlight Preview.lnk'
    Assert-Check ($shell.CreateShortcut($editorLink).TargetPath -eq $installedLauncher) 'Editor shortcut targets installed launcher correctly'
    $link=$shell.CreateShortcut($analysisLink)
    Assert-Check ($link.TargetPath -eq $installedLauncher -and $link.Arguments -eq '--analysis') 'Analysis shortcut targets installed launcher correctly'
    Assert-Check ((Run-Program $installedLauncher '--analysis') -eq 0) 'Installed Analysis launch accepted'
    $analysisProcess=$null
    $expectedAnalysisTitle='Starlight Engine ' + [char]0x2014 + ' Deep Analysis'
    foreach($attempt in 1..20) {
        $analysisProcess=Get-Process -ErrorAction SilentlyContinue | Where-Object Path -eq (Join-Path $installed $manifest.analysis)
        if($analysisProcess -and $analysisProcess.MainWindowTitle -eq $expectedAnalysisTitle){break}
        Start-Sleep -Milliseconds 250
    }
    Assert-Check ($null -ne $analysisProcess -and $analysisProcess.MainWindowTitle -eq $expectedAnalysisTitle) 'Installed Analysis window opened'
    $analysisProcess.CloseMainWindow() | Out-Null
    $analysisProcess.WaitForExit(15000) | Out-Null
    'Keep this project' | Set-Content -LiteralPath (Join-Path $installed 'user-project.txt')
    Add-Content -LiteralPath (Join-Path $installed 'LICENSE') -Value 'Test modification: preserve this file'
    Assert-Check ((Run-Program $installedLauncher '--uninstall-quiet') -eq 0) 'Uninstaller accepted modified installed file'
    # The detached helper removes the executable before the remaining payload,
    # shortcuts and final registration. Wait for the whole transaction.
    foreach($attempt in 1..480){
        if(-not(Test-Path -LiteralPath $installedLauncher) -and -not(Test-Path -LiteralPath $analysisLink) -and -not(Test-Path -LiteralPath $editorLink) -and -not(Test-Path -LiteralPath $reg)){break}
        Start-Sleep -Milliseconds 250
    }
    Assert-Check (-not(Test-Path -LiteralPath $installedLauncher)) 'Installed program removed'
    Assert-Check ((Get-Content -LiteralPath (Join-Path $installed 'LICENSE') -Raw).Contains('Test modification')) 'Modified installed file preserved'
    Assert-Check (Test-Path -LiteralPath (Join-Path $installed 'user-project.txt')) 'User project preserved'
    Assert-Check (-not(Test-Path -LiteralPath $analysisLink)) 'Analysis shortcut removed'
    Assert-Check (-not(Test-Path -LiteralPath $editorLink)) 'Editor shortcut removed'
    Assert-Check (-not(Test-Path -LiteralPath $reg)) 'Uninstall registration removed'
    Assert-Check ((Run-Program $launcher '--verify-package') -eq 0) 'Original portable package preserved after install and uninstall'
    Assert-Check (Test-Path -LiteralPath (Join-Path $root 'data\relocation-test.txt')) 'Original portable user data preserved'
    @{completed=$true;lifecyclePassed=$true;allRuntimeStagesPassed=$summary.allRequestedStagesPassed;stages=$summary.stages;checks=@($checks)} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $Results 'result.json')
    Save-State 'Completed; inspect runtime stage outcomes separately'
} catch {
    @{completed=$false;lifecyclePassed=$false;error=$_.Exception.Message;checks=@($checks)} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Results 'result.json')
    Save-State ('Failed: '+$_.Exception.Message)
    exit 1
}
