# deploy_vcam.ps1 - Post-build deploy script for VCamSampleSource.dll
# Stops Frame Server, re-registers DLL, restarts Frame Server
# Auto-elevates to Administrator if needed

param(
    [Parameter(Mandatory=$true)]
    [string]$DllPath
)

# ── Auto-elevation ────────────────────────────────────────────────────────────
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
{
    Write-Host "Elevating to Administrator..."
    $args = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -DllPath `"$DllPath`""
    Start-Process powershell -ArgumentList $args -Verb RunAs -Wait
    exit $LASTEXITCODE
}

# ── Helpers ───────────────────────────────────────────────────────────────────
function Write-Step([string]$msg) {
    Write-Host "`n[$([datetime]::Now.ToString('HH:mm:ss'))] $msg" -ForegroundColor Cyan
}

function Write-OK([string]$msg) {
    Write-Host "  OK  $msg" -ForegroundColor Green
}

function Write-Warn([string]$msg) {
    Write-Host "  WARN $msg" -ForegroundColor Yellow
}

function Write-Fail([string]$msg) {
    Write-Host "  FAIL $msg" -ForegroundColor Red
}

# ── Validate DLL path ─────────────────────────────────────────────────────────
if (-not (Test-Path $DllPath)) {
    Write-Fail "DLL not found: $DllPath"
    exit 1
}

Write-Host ""
Write-Host "======================================" -ForegroundColor Magenta
Write-Host "  VCamSampleSource.dll Deploy Script  " -ForegroundColor Magenta
Write-Host "======================================" -ForegroundColor Magenta
Write-Host "  DLL: $DllPath"

# ── Frame Server service names ─────────────────────────────────────────────────
# Windows Camera Frame Server (Win11) + optional sensor group service
$services = @("FrameServer", "FrameServerMonitor")

# ── Step 1: Stop Frame Server ─────────────────────────────────────────────────
Write-Step "Stopping Camera Frame Server..."

foreach ($svc in $services) {
    $s = Get-Service -Name $svc -ErrorAction SilentlyContinue
    if ($s -and $s.Status -eq 'Running') {
        Stop-Service -Name $svc -Force -ErrorAction SilentlyContinue
        $s.WaitForStatus('Stopped', (New-TimeSpan -Seconds 10))
        Write-OK "Service '$svc' stopped"
    } else {
        Write-Warn "Service '$svc' not running or not found — skipped"
    }
}

# Kill any leftover FrameServerMonitor or CameraFrameServer processes
@("FrameServerMonitor", "Windows.Media.Capture.Internal") | ForEach-Object {
    $procs = Get-Process -Name $_ -ErrorAction SilentlyContinue
    if ($procs) {
        $procs | Stop-Process -Force
        Write-Warn "Killed process: $_"
    }
}

Start-Sleep -Milliseconds 500

# ── Step 2: Unregister old DLL ────────────────────────────────────────────────
Write-Step "Unregistering old DLL (ignore errors if not registered)..."

$regsvr = "$env:SystemRoot\System32\regsvr32.exe"
& $regsvr /u /s $DllPath
Write-OK "Unregister attempted"

Start-Sleep -Milliseconds 300

# ── Step 3: Register new DLL ──────────────────────────────────────────────────
Write-Step "Registering: $DllPath"

$result = Start-Process $regsvr -ArgumentList "/s `"$DllPath`"" -Wait -PassThru
if ($result.ExitCode -eq 0) {
    Write-OK "DLL registered successfully"
} else {
    Write-Fail "regsvr32 failed with exit code $($result.ExitCode)"
    # Restart Frame Server before exiting so camera still works
    foreach ($svc in $services) {
        Start-Service -Name $svc -ErrorAction SilentlyContinue
    }
    exit 1
}

# ── Step 4: Restart Frame Server ─────────────────────────────────────────────
Write-Step "Restarting Camera Frame Server..."

foreach ($svc in $services) {
    $s = Get-Service -Name $svc -ErrorAction SilentlyContinue
    if ($s) {
        Start-Service -Name $svc -ErrorAction SilentlyContinue
        Write-OK "Service '$svc' started"
    }
}

Start-Sleep -Milliseconds 500

# ── Done ──────────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "======================================" -ForegroundColor Magenta
Write-Host "  Deploy complete - ready to debug!   " -ForegroundColor Green
Write-Host "======================================" -ForegroundColor Magenta
Write-Host ""

exit 0