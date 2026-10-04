param([string]$Source='C:\StarlightInput',[string]$Results='C:\StarlightResults')
$ErrorActionPreference='Stop'
function Run-Check([string]$Executable,[string]$Arguments) {
    $process=Start-Process -FilePath $Executable -ArgumentList $Arguments -PassThru -WindowStyle Hidden
    if(-not $process.WaitForExit(300000)){throw "Check exceeded five minutes: $Executable"}
    $process.Refresh(); return $process.ExitCode
}
try {
    $root=Join-Path $env:USERPROFILE 'Desktop\Starlight Focused Test'
    if(Test-Path -LiteralPath $root){throw 'Focused destination must be fresh'}
    Copy-Item -LiteralPath (Join-Path $Source 'package') -Destination $root -Recurse
    $env:PATH="$env:WINDIR\System32;$env:WINDIR"
    $env:STARLIGHT_DATA_ROOT=Join-Path $root ('Portable Data '+[char]0x03A9)
    if((Run-Check (Join-Path $root 'Starlight.exe') '--verify-package') -ne 0){throw 'Package verification failed'}
    $runtime=Join-Path $root 'runtime\Release'
    $diagnostics=Join-Path $Results 'diagnostics'
    $diagnosticExit=Run-Check (Join-Path $runtime 'Starlight.NativeDiagnosticChecks.exe') ('"'+$diagnostics+'"')
    $diagnosticReport=Get-Content -LiteralPath (Join-Path $diagnostics 'diagnostics.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $denoise=Join-Path $Results 'denoiser'
    $packet=Join-Path $root 'editor\checks\denoise\regression\chaos-rt.packet'
    $denoiseExit=Run-Check (Join-Path $runtime 'Starlight.NativeDenoiseChecks.exe') ('--packet "'+$packet+'" --output "'+$denoise+'"')
    $denoiseReport=Get-Content -LiteralPath (Join-Path $denoise 'result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $portableDump=$diagnosticReport.dump.StartsWith($env:STARLIGHT_DATA_ROOT+'\',[StringComparison]::OrdinalIgnoreCase)
    @{completed=$true;diagnosticsExit=$diagnosticExit;portableDump=$portableDump;diagnostics=$diagnosticReport;denoiserExit=$denoiseExit;denoiser=$denoiseReport;allPassed=($diagnosticExit -eq 0 -and $portableDump -and $denoiseExit -eq 0)} |
        ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $Results 'focused-result.json') -Encoding UTF8
} catch {
    @{completed=$false;error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Results 'focused-result.json') -Encoding UTF8
    exit 1
}
