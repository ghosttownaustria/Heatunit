<#
.SYNOPSIS
  Prueft und repariert die WinUSB-Anbindung des Test-Handys (Samsung 04E8:6860).

.DESCRIPTION
  Die App braucht WinUSB auf der MTP-Schnittstelle (MI_00) des Handys, damit libusb
  das Geraet oeffnen kann (sonst: LIBUSB_ERROR_NOT_FOUND). Windows bindet gelegentlich
  wieder Samsungs Treiber (dg_ssudbus / WUDFWpdMtp). Dieses Skript
    1. zeigt den aktuellen Zustand (nur lesend),
    2. stellt den Composite-Parent wieder auf Microsofts usbccgp um, worauf Windows das
       bereits installierte WinUSB-Paket (oem151.inf, libwdi) fuer MI_00 waehlt,
    3. prueft das Ergebnis und fuehrt danach HeadUnit.exe --probe-usb aus.
  Aenderung nur an diesem einen Handy (Instanz aus .tools\usb-driver-backup\before.json).
  Der Rueckweg steht in docs\windows_connection.md. Dateiuebertragung im Explorer ist
  waehrend der WinUSB-Bindung nicht verfuegbar.

  Aufruf (fragt selbst nach Administratorrechten, UAC-Dialog bestaetigen):
    powershell -ExecutionPolicy Bypass -File HeadUnit\scripts\Repair-PhoneDriver.ps1
  Nur pruefen, nichts aendern:
    powershell -ExecutionPolicy Bypass -File HeadUnit\scripts\Repair-PhoneDriver.ps1 -CheckOnly
#>
param([switch]$CheckOnly, [switch]$Elevated)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$backup = Join-Path $root '.tools\usb-driver-backup'
$parentPattern = 'USB\VID_04E8&PID_6860\*'
$interfacePattern = 'USB\VID_04E8&PID_6860&MI_00\*'

function Get-Service-Of($pattern) {
    $device = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object InstanceId -Like $pattern) | Select-Object -First 1
    if (-not $device) { return $null }
    $service = (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data
    [pscustomobject]@{ InstanceId = $device.InstanceId; Service = $service }
}
function Show-State {
    $parent = Get-Service-Of $parentPattern
    $interface = Get-Service-Of $interfacePattern
    if (-not $parent) { Write-Host 'Handy 04E8:6860 nicht gefunden. Kabel, entsperrtes Handy und USB-Modus "Dateien uebertragen" pruefen.' -ForegroundColor Yellow; return 'missing' }
    Write-Host ("Parent    : {0}  (Treiber: {1})" -f $parent.InstanceId, $parent.Service)
    if ($interface) { Write-Host ("MI_00     : {0}  (Treiber: {1})" -f $interface.InstanceId, $interface.Service) }
    else { Write-Host 'MI_00     : nicht vorhanden (Samsung-Parent bindet die Funktionen selbst)' }
    if ($interface -and $interface.Service -eq 'WinUSB') { return 'ok' }
    return 'broken'
}

$state = Show-State
if ($state -eq 'ok') { Write-Host 'WinUSB ist gebunden. Nichts zu tun.' -ForegroundColor Green }
if ($CheckOnly -or $state -ne 'broken') { if (-not $Elevated) { exit ([int]($state -ne 'ok')) } }

if ($state -eq 'broken') {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host 'Administratorrechte noetig; UAC-Dialog erscheint ...'
        $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Elevated"
        Start-Process -FilePath powershell.exe -ArgumentList $arguments -Verb RunAs -Wait
        [void](Show-State)
        exit 0
    }
    foreach ($required in 'parent-driver.ps1', 'DriverApi.cs', 'before.json') {
        if (-not (Test-Path (Join-Path $backup $required))) { throw "Fehlt: $backup\$required (Backup der frueheren Umstellung)" }
    }
    Write-Host 'Stelle den Composite-Parent auf Microsoft usbccgp um ...'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $backup 'parent-driver.ps1') -Install
    if ($LASTEXITCODE -ne 0) { Write-Host (Get-Content (Join-Path $backup 'parent-driver-result.txt') -Raw) -ForegroundColor Red; throw 'Umstellung fehlgeschlagen' }
    Write-Host 'Warte auf die MI_00-Funktion mit WinUSB ...'
    for ($i = 0; $i -lt 30; ++$i) {
        Start-Sleep -Seconds 1
        $interface = Get-Service-Of $interfacePattern
        if ($interface -and $interface.Service -eq 'WinUSB') { break }
    }
    $state = Show-State
    if ($state -ne 'ok') {
        Write-Host 'MI_00 hat WinUSB noch nicht. Kabel einmal ab- und wieder anstecken und dieses Skript mit -CheckOnly erneut ausfuehren.' -ForegroundColor Yellow
    }
}

if ($state -eq 'ok') {
    $app = Join-Path $root 'HeadUnit\out\vs2026\x64\Debug\HeadUnit.exe'
    if (Test-Path $app) {
        Write-Host 'Teste USB-Zugriff (HeadUnit.exe --probe-usb, nur lesende AOA-Abfrage) ...'
        Push-Location (Split-Path $app)
        & $app --probe-usb | Out-Null
        Write-Host ("Ergebnis: Exitcode {0} (0 = Handy oeffnet sich, AOA-Version wurde gelesen)" -f $LASTEXITCODE)
        Pop-Location
    }
}
if ($Elevated) { Read-Host 'Enter zum Schliessen' }
