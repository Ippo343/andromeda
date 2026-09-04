# Builds the three hardware envs (esp32_wroom, esp32_s3_zero, esp32_c3_zero) in parallel
# instead of PlatformIO's own sequential `pio run` (no built-in option for concurrent envs).
# Each env has its own .pio/build/<env> and .pio/libdeps/<env>, so once
# build-scripts/inject_version.py's write-only-on-change guard is in place there is no shared
# mutable state between the processes.
#
# Verified (see the-build-times-are-jazzy-orbit.md): a parallel run produces byte-identical
# firmware.bin per board versus a sequential `pio run`, and is ~2.3x faster on an incremental
# rebuild (measured 45.4s -> 20.0s for a real source edit across all three envs).
#
# Usage:
#   powershell -File tools/build-all.ps1                          # all 3 hardware envs
#   powershell -File tools/build-all.ps1 -Envs esp32_wroom,esp32_c3_zero
#   powershell -File tools/build-all.ps1 -ExtraArgs -t,buildfs     # extra `pio run` args, e.g. a target

param(
    [string[]]$Envs = @("esp32_wroom", "esp32_s3_zero", "esp32_c3_zero"),
    [string[]]$ExtraArgs = @()
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
if (-not (Test-Path $pio)) {
    throw "pio.exe not found at $pio -- see CLAUDE.md's 'Running pio on Windows' note."
}

$logDir = Join-Path $repoRoot ".pio\build-all-logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

Write-Host ("== Building {0} env(s) in parallel: {1} ==" -f $Envs.Count, ($Envs -join ", ")) -ForegroundColor Cyan

$procs = @()
foreach ($e in $Envs) {
    $pioArgs = @("run", "-e", $e) + $ExtraArgs
    $procs += [PSCustomObject]@{
        Env     = $e
        Process = Start-Process -FilePath $pio -ArgumentList $pioArgs `
            -WorkingDirectory $repoRoot -NoNewWindow -PassThru `
            -RedirectStandardOutput (Join-Path $logDir "$e.out") `
            -RedirectStandardError  (Join-Path $logDir "$e.err")
    }
}

$procs.Process | Wait-Process

$failed = @()
foreach ($p in $procs) {
    $ok = $p.Process.ExitCode -eq 0
    $status = if ($ok) { "OK" } else { "FAILED (exit $($p.Process.ExitCode))" }
    $color = if ($ok) { "Green" } else { "Red" }
    Write-Host ("  {0,-16} {1}" -f $p.Env, $status) -ForegroundColor $color
    if (-not $ok) {
        $failed += $p.Env
        Write-Host "  --- $($p.Env) stderr tail ---" -ForegroundColor Yellow
        Get-Content (Join-Path $logDir "$($p.Env).err") -Tail 20 | ForEach-Object { Write-Host "  $_" }
    }
}

if ($failed.Count -gt 0) {
    Write-Host ("`nFAILED envs: {0}. Full logs in {1}" -f ($failed -join ", "), $logDir) -ForegroundColor Red
    exit 1
}

Write-Host "`nAll envs built successfully." -ForegroundColor Green
exit 0
