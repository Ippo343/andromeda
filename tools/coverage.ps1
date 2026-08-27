# Rebuilds and runs the native unit tests, then generates and opens an HTML coverage report.
# Requires MSYS2 (pacman -S mingw-w64-x86_64-lcov) since lcov/genhtml are Perl scripts that
# need MSYS2's own runtime -- Git Bash's MSYS runtime can't exec them (different shebang resolution).

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

Write-Host "== Rebuilding and running native tests ==" -ForegroundColor Cyan
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio test -e native
if ($LASTEXITCODE -ne 0) {
    Write-Host "Tests failed (exit $LASTEXITCODE) -- generating coverage anyway." -ForegroundColor Yellow
}

$msysBash = "C:\msys64\usr\bin\bash.exe"
if (-not (Test-Path $msysBash)) {
    throw "MSYS2 bash not found at $msysBash. Install MSYS2 and 'pacman -S mingw-w64-x86_64-lcov' first."
}

$unixRoot = "/c/" + ($repoRoot -replace '^C:\\', '' -replace '\\', '/')

Write-Host "== Capturing coverage ==" -ForegroundColor Cyan
# lcov_branch_coverage is geninfo's actual rc key (see mingw64/bin/geninfo); passing an extra
# --rc branch_coverage=1 alongside it silently drops branch data entirely -- verified by testing
# each flag in isolation, so don't reintroduce it.
# --ignore-errors mismatch: newer geninfo treats gcov "mismatched exception tag" noise from
# libstdc++ template code as fatal; it's not our code (the --remove below strips it), so
# downgrade it to a warning -- keep in sync with .github/workflows/test.yml.
$captureCmd = "export PATH=/mingw64/bin:/usr/bin:/bin:`$PATH && cd '$unixRoot' && lcov --capture --directory .pio/build/native --output-file coverage.info --rc lcov_branch_coverage=1 --ignore-errors mismatch"
& $msysBash -lc $captureCmd

Write-Host "== Filtering coverage (project sources only) ==" -ForegroundColor Cyan
$filterCmd = "export PATH=/mingw64/bin:/usr/bin:/bin:`$PATH && cd '$unixRoot' && lcov --remove coverage.info '*mingw64*' '*.pio*' '*test*' --output-file coverage.info --rc lcov_branch_coverage=1 && lcov --list coverage.info --rc lcov_branch_coverage=1"
& $msysBash -lc $filterCmd

Write-Host "== Generating HTML report ==" -ForegroundColor Cyan
$htmlCmd = "export PATH=/mingw64/bin:/usr/bin:/bin:`$PATH && cd '$unixRoot' && genhtml coverage.info --output-directory coverage_html --branch-coverage"
& $msysBash -lc $htmlCmd

$reportPath = Join-Path $repoRoot "coverage_html\index.html"
Write-Host "== Opening report: $reportPath ==" -ForegroundColor Cyan
Start-Process $reportPath
