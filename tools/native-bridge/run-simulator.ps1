#Requires -Version 5.1
# Dumb-proof launcher for the native simulator: builds env:native_runtime if
# needed, installs npm deps if needed, lets you pick a hardware model from a
# dropdown, then starts tools/native-bridge/server.js and opens the controls
# + visualizer pages in your browser. Meant to be double-clicked via
# Run Simulator.bat at the repo root - see CLAUDE.md for what this is bridging.

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$PioExe = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
$BinaryPath = Join-Path $RepoRoot ".pio\build\native_runtime\program.exe"
$ServerJs = Join-Path $RepoRoot "tools\native-bridge\server.js"

function Find-Node {
    $cmd = Get-Command node.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $fallback = "C:\Program Files\nodejs\node.exe"
    if (Test-Path $fallback) { return $fallback }
    return $null
}

$NodeExe = Find-Node
if (-not $NodeExe) {
    [System.Windows.Forms.MessageBox]::Show(
        "Node.js was not found on PATH or at 'C:\Program Files\nodejs\'. Install it from https://nodejs.org and try again.",
        "Andromeda Simulator", "OK", "Error") | Out-Null
    exit 1
}
$NpmCmd = Join-Path (Split-Path $NodeExe -Parent) "npm.cmd"

if (-not (Test-Path $PioExe)) {
    [System.Windows.Forms.MessageBox]::Show(
        "PlatformIO was not found at '$PioExe'. Install PlatformIO Core and try again.",
        "Andromeda Simulator", "OK", "Error") | Out-Null
    exit 1
}

# Ask the already-built binary for its real model list (program.exe
# --list-models, one name per line - see NativeRuntime::init()) rather than
# hardcoding names that could drift from src/geometry/*.cpp. Only falls back
# to a hardcoded list on the very first-ever run, before program.exe exists;
# every run after that first build shows the live registry.
$Models = @("Andromeda Mk1", "L70 MK1", "L10 MK1", "Single Strip Test Rig", "Grid Test Rig")
if (Test-Path $BinaryPath) {
    $liveModels = & $BinaryPath --list-models 2>$null
    if ($LASTEXITCODE -eq 0 -and $liveModels) { $Models = @($liveModels) }
}

# --- UI -----------------------------------------------------------------

$form = New-Object System.Windows.Forms.Form
$form.Text = "Andromeda Native Simulator"
$form.Size = New-Object System.Drawing.Size(540, 420)
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false

$label = New-Object System.Windows.Forms.Label
$label.Text = "Hardware model:"
$label.Location = New-Object System.Drawing.Point(15, 20)
$label.AutoSize = $true
$form.Controls.Add($label)

$combo = New-Object System.Windows.Forms.ComboBox
$combo.Location = New-Object System.Drawing.Point(15, 45)
$combo.Width = 320
$combo.DropDownStyle = "DropDownList"
$Models | ForEach-Object { [void]$combo.Items.Add($_) }
$combo.SelectedIndex = 0
$form.Controls.Add($combo)

$startButton = New-Object System.Windows.Forms.Button
$startButton.Text = "Start"
$startButton.Location = New-Object System.Drawing.Point(350, 43)
$startButton.Width = 80
$form.Controls.Add($startButton)

$stopButton = New-Object System.Windows.Forms.Button
$stopButton.Text = "Stop"
$stopButton.Location = New-Object System.Drawing.Point(435, 43)
$stopButton.Width = 80
$stopButton.Enabled = $false
$form.Controls.Add($stopButton)

$logBox = New-Object System.Windows.Forms.TextBox
$logBox.Multiline = $true
$logBox.ScrollBars = "Vertical"
$logBox.ReadOnly = $true
$logBox.Location = New-Object System.Drawing.Point(15, 80)
$logBox.Size = New-Object System.Drawing.Size(500, 280)
$logBox.Font = New-Object System.Drawing.Font("Consolas", 9)
$form.Controls.Add($logBox)

function Write-Log([string]$msg) {
    $logBox.AppendText("$msg`r`n")
    $logBox.SelectionStart = $logBox.Text.Length
    $logBox.ScrollToCaret()
}

$script:ServerProcess = $null
$script:BrowserTabsOpened = $false

$startButton.Add_Click({
  try {
    $startButton.Enabled = $false
    $combo.Enabled = $false
    $form.Refresh()

    $model = $combo.SelectedItem

    if (-not (Test-Path (Join-Path $RepoRoot "node_modules"))) {
        Write-Log "Installing npm dependencies (first run only)..."
        $form.Refresh()
        $p = Start-Process -FilePath $NpmCmd -ArgumentList "install", "--no-audit", "--no-fund" `
            -WorkingDirectory $RepoRoot -WindowStyle Hidden -Wait -PassThru
        if ($p.ExitCode -ne 0) {
            Write-Log "npm install failed (exit $($p.ExitCode))."
            $startButton.Enabled = $true; $combo.Enabled = $true
            return
        }
    }

    Write-Log "Building native runtime (pio run -e native_runtime)..."
    Write-Log "(first build takes ~30s, later ones are near-instant)"
    $form.Refresh()
    $buildLog = Join-Path $env:TEMP "andromeda-native-build.log"
    $buildErrLog = Join-Path $env:TEMP "andromeda-native-build.err.log"
    $p = Start-Process -FilePath $PioExe -ArgumentList "run", "-e", "native_runtime" `
        -WorkingDirectory $RepoRoot -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput $buildLog -RedirectStandardError $buildErrLog
    if ($p.ExitCode -ne 0 -or -not (Test-Path $BinaryPath)) {
        Write-Log "Build failed. Last lines:"
        Get-Content $buildErrLog -Tail 20 -ErrorAction SilentlyContinue | ForEach-Object { Write-Log $_ }
        Get-Content $buildLog -Tail 20 -ErrorAction SilentlyContinue | ForEach-Object { Write-Log $_ }
        $startButton.Enabled = $true; $combo.Enabled = $true
        return
    }
    Write-Log "Build OK."

    Write-Log "Starting bridge server for model '$model'..."
    # Start-Process -ArgumentList joins array elements with a bare space and
    # does NOT quote elements containing whitespace (PS 5.1) - a model name
    # like "Andromeda Mk1" would otherwise arrive at node as two separate
    # argv entries ("--model=Andromeda", "Mk1"), silently falling back to
    # the default model. Wrapping the whole flag in its own quotes keeps it
    # one token, same fix as the CLI-quoting note in the plan doc.
    $script:ServerProcess = Start-Process -FilePath $NodeExe `
        -ArgumentList @($ServerJs, "`"--model=$model`"") `
        -WorkingDirectory $RepoRoot -WindowStyle Hidden -PassThru

    Start-Sleep -Milliseconds 800
    if ($script:ServerProcess.HasExited) {
        Write-Log "Server exited immediately (exit $($script:ServerProcess.ExitCode)). Is port 8080 already in use?"
        $startButton.Enabled = $true; $combo.Enabled = $true
        return
    }

    Write-Log "Running at http://localhost:8080/"
    if ($script:BrowserTabsOpened) {
        Write-Log "Browser tabs already opened this session - not reopening (refresh them manually if needed)."
    } else {
        Write-Log "Opening controls + visualizer in your browser..."
        Start-Process "http://localhost:8080/"
        Start-Process "http://localhost:8080/visualizer.html"
        $script:BrowserTabsOpened = $true
    }

    $stopButton.Enabled = $true
  } catch {
    Write-Log "Unexpected error: $_"
    $startButton.Enabled = $true; $combo.Enabled = $true
  }
})

$stopButton.Add_Click({
    if ($script:ServerProcess -and -not $script:ServerProcess.HasExited) {
        Stop-Process -Id $script:ServerProcess.Id -Force -ErrorAction SilentlyContinue
        Write-Log "Server stopped."
    }
    $script:ServerProcess = $null
    $stopButton.Enabled = $false
    $startButton.Enabled = $true
    $combo.Enabled = $true
})

$form.Add_FormClosing({
    if ($script:ServerProcess -and -not $script:ServerProcess.HasExited) {
        Stop-Process -Id $script:ServerProcess.Id -Force -ErrorAction SilentlyContinue
    }
})

[void]$form.ShowDialog()
