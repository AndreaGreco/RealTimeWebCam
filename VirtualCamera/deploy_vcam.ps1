# deploy_vcam.ps1 - Post-build deploy script for VCamSampleSource.dll
# Stops Frame Server, re-registers DLL, restarts Frame Server
# Auto-elevates to Administrator if needed
#
# The DLL is always registered from $DeployDir (default C:\Projects\RTVirtualCamera),
# NOT from $DllPath directly: the Frame Server runs as Local Service, which gets
# E_ACCESSDENIED on IMFVirtualCamera::Start if the DLL lives under C:\Users\...
# (the build output is under the repo, which is under the user profile). This
# script copies the freshly built DLL out to the accessible folder before
# registering, so $(TargetPath) can keep pointing at the normal build output.

param(
    [Parameter(Mandatory=$true)]
    [string]$DllPath,

    [string]$DeployDir = "C:\Projects\RTVirtualCamera"
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

# ── Step 2: Copy DLL to an accessible-to-all folder ───────────────────────────
# Local Service (the Frame Server identity) gets E_ACCESSDENIED on
# IMFVirtualCamera::Start if the DLL is registered from under C:\Users\...
# $DeployDir must NOT be under a user profile.
Write-Step "Copying DLL to deploy folder: $DeployDir"

if (-not (Test-Path $DeployDir)) {
    New-Item -ItemType Directory -Path $DeployDir -Force | Out-Null
    Write-OK "Created $DeployDir"
}

$deployedDll = Join-Path $DeployDir (Split-Path $DllPath -Leaf)
Copy-Item -Path $DllPath -Destination $deployedDll -Force
Write-OK "Copied to $deployedDll"

# From here on, register/unregister the DEPLOYED copy, not the build-output path.
$DllPath = $deployedDll

# ── Step 3: Unregister old DLL ────────────────────────────────────────────────
Write-Step "Unregistering old DLL (ignore errors if not registered)..."

$regsvr = "$env:SystemRoot\System32\regsvr32.exe"
& $regsvr /u /s $DllPath
Write-OK "Unregister attempted"

Start-Sleep -Milliseconds 300

# ── Step 4: Register new DLL ──────────────────────────────────────────────────
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

# ── Step 5: Restart Frame Server ─────────────────────────────────────────────
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