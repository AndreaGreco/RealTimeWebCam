# unregister_vcam.ps1 - Removes the dev COM registration for the virtual camera.
# Use before testing the MSI installer so you start from a clean state.
# Stops Frame Server, unregisters the DLL, force-removes the CLSID key, restarts Frame Server.
# Auto-elevates to Administrator if needed.

param(
    # Optional: DLL to unregister via regsvr32 /u. The CLSID key is removed regardless,
    # so this only matters if you want the "symmetric" DllUnregisterServer call too.
    # Must match the deploy folder used by deploy_vcam.ps1 (not the repo build output —
    # Local Service can't see under C:\Users\...).
    [string]$DllPath = "C:\Projects\RTVirtualCamera\VCamSampleSource.dll"
)

# CLSID_VCam - must match Shared/VCamConfig.h and the registry.
$Clsid = "{3CAD447D-F283-4AF4-A3B2-6F5363309F52}"

# -- Auto-elevation -----------------------------------------------------------
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
{
    Write-Host "Elevating to Administrator..."
    $args = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -DllPath `"$DllPath`""
    Start-Process powershell -ArgumentList $args -Verb RunAs -Wait
    exit $LASTEXITCODE
}

# -- Helpers ------------------------------------------------------------------
function Write-Step([string]$msg) { Write-Host "`n[$([datetime]::Now.ToString('HH:mm:ss'))] $msg" -ForegroundColor Cyan }
function Write-OK([string]$msg)   { Write-Host "  OK  $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "  WARN $msg" -ForegroundColor Yellow }
function Write-Fail([string]$msg) { Write-Host "  FAIL $msg" -ForegroundColor Red }

Write-Host ""
Write-Host "=======================================" -ForegroundColor Magenta
Write-Host "  VCam COM Unregister Script           " -ForegroundColor Magenta
Write-Host "=======================================" -ForegroundColor Magenta
Write-Host "  CLSID: $Clsid"
Write-Host "  DLL:   $DllPath"

$services = @("FrameServer", "FrameServerMonitor")

# -- Step 1: Stop Frame Server ------------------------------------------------
Write-Step "Stopping Camera Frame Server..."
foreach ($svc in $services) {
    $s = Get-Service -Name $svc -ErrorAction SilentlyContinue
    if ($s -and $s.Status -eq 'Running') {
        Stop-Service -Name $svc -Force -ErrorAction SilentlyContinue
        try { $s.WaitForStatus('Stopped', (New-TimeSpan -Seconds 10)) } catch {}
        Write-OK "Service '$svc' stopped"
    } else {
        Write-Warn "Service '$svc' not running or not found - skipped"
    }
}
Start-Sleep -Milliseconds 500

# -- Step 2: Unregister via regsvr32 (symmetric, optional) --------------------
Write-Step "Unregistering DLL (DllUnregisterServer)..."
if (Test-Path $DllPath) {
    & "$env:SystemRoot\System32\regsvr32.exe" /u /s $DllPath
    Write-OK "regsvr32 /u attempted on $DllPath"
} else {
    Write-Warn "DLL not found, skipping regsvr32 /u: $DllPath"
}
Start-Sleep -Milliseconds 300

# -- Step 3: Force-remove the CLSID key (64-bit view) -------------------------
Write-Step "Removing CLSID key from 64-bit registry..."
$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine','Registry64')
$base.DeleteSubKeyTree("SOFTWARE\Classes\CLSID\$Clsid", $false)   # $false = don't throw if missing
Write-OK "CLSID key removed (or already absent)"

# -- Step 4: Verify -----------------------------------------------------------
Write-Step "Verifying..."
$check = $base.OpenSubKey("SOFTWARE\Classes\CLSID\$Clsid")
if ($check) {
    Write-Fail "CLSID key STILL present - manual investigation needed"
    $check.Close()
} else {
    Write-OK "Clean: CLSID not registered in 64-bit hive"
}
$base.Close()

# -- Step 5: Restart Frame Server ---------------------------------------------
Write-Step "Restarting Camera Frame Server..."
foreach ($svc in $services) {
    $s = Get-Service -Name $svc -ErrorAction SilentlyContinue
    if ($s) {
        Start-Service -Name $svc -ErrorAction SilentlyContinue
        Write-OK "Service '$svc' started"
    }
}

Write-Host ""
Write-Host "=======================================" -ForegroundColor Magenta
Write-Host "  Unregister complete - clean state    " -ForegroundColor Green
Write-Host "=======================================" -ForegroundColor Magenta
Write-Host ""
exit 0
