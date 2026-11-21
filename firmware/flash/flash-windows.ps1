# Türspion-Display flashen (Windows). Start über flash-windows.bat (Doppelklick) oder:
#   powershell -ExecutionPolicy Bypass -File flash-windows.ps1 [Datei.bin] [-Check]
# Sucht die Firmware (Argument, Ordner des Skripts, Downloads, sonst das neueste GitHub-Release über "gh"),
# richtet beim ersten Mal esptool in einer eigenen Umgebung ein und spielt sie auf das Board auf.
# -Check liest nur die Chip-Kennung aus und ändert nichts.
param([string]$Bin = "", [switch]$Check)
$ErrorActionPreference = "Stop"
$Repo = "alexanderschubert/reolink-doorbell-esp-display"
$Asset = "doorbell-display-esp32s3-lcd5.bin"
$Home2 = Join-Path $env:LOCALAPPDATA "doorbell-display-flasher"
Set-Location $PSScriptRoot

function Fail($m) { Write-Host "`n$m" -ForegroundColor Red; Read-Host "Zum Beenden Enter drücken"; exit 1 }
Write-Host "`nTürspion-Display flashen" -ForegroundColor White

# 1. Python und esptool
$py = $null
foreach ($c in @("py", "python", "python3")) { if (Get-Command $c -ErrorAction SilentlyContinue) { $py = $c; break } }
if (-not $py) { Fail "Python fehlt. Installiere es von https://www.python.org/downloads/ (Haken bei 'Add python.exe to PATH') und starte das Skript neu." }
$esptool = Join-Path $Home2 "venv\Scripts\esptool.exe"
if (-not (Test-Path $esptool)) {
  Write-Host "`nRichte esptool ein (nur beim ersten Mal, braucht Internet) ..."
  $pyArgs = if ($py -eq "py") { @("-3") } else { @() }
  & $py @pyArgs -m venv (Join-Path $Home2 "venv")
  & (Join-Path $Home2 "venv\Scripts\pip.exe") install --quiet esptool
  if (-not (Test-Path $esptool)) { Fail "esptool konnte nicht installiert werden." }
}

# 2. Firmware
if (-not $Check) {
  if (-not $Bin) {
    $dl = Join-Path $env:USERPROFILE "Downloads"
    $cands = @((Join-Path $PSScriptRoot $Asset)) + (Get-ChildItem $PSScriptRoot -Filter *.bin -ErrorAction SilentlyContinue | ForEach-Object FullName) +
             @((Join-Path $dl $Asset)) + (Get-ChildItem $dl -Filter "doorbell-display-esp32s3-lcd5*.bin" -ErrorAction SilentlyContinue | ForEach-Object FullName)
    foreach ($c in $cands) { if ($c -and (Test-Path $c)) { $Bin = $c; break } }
  }
  if (-not $Bin -and (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Host "`nLade die neueste Firmware von GitHub ..."
    $tag = (gh release list --repo $Repo --limit 20 --json tagName -q ".[].tagName" 2>$null | Where-Object { $_ -like "firmware-*" } | Select-Object -First 1)
    if ($tag) {
      $dir = Join-Path $Home2 "dl"; New-Item -ItemType Directory -Force $dir | Out-Null
      gh release download $tag --repo $Repo --pattern $Asset --dir $dir --clobber
      $Bin = Join-Path $dir $Asset
    }
  }
  if (-not $Bin -or -not (Test-Path $Bin)) { Fail "Keine Firmware gefunden. Lege $Asset neben dieses Skript oder in den Ordner Downloads (https://github.com/$Repo/releases), oder gib den Pfad an." }
  Write-Host ("Firmware: {0} ({1:N1} MB)" -f $Bin, ((Get-Item $Bin).Length / 1MB))
}

# 3. Anschluss
$ports = @()
try {
  $ports = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "\(COM\d+\)" -and $_.Name -match "USB|Serial|JTAG|CH340|CP210" } |
             ForEach-Object { [pscustomobject]@{ Port = ([regex]::Match($_.Name, "COM\d+")).Value; Name = $_.Name } })
} catch { }
if ($ports.Count -eq 0) { $ports = @([System.IO.Ports.SerialPort]::GetPortNames() | ForEach-Object { [pscustomobject]@{ Port = $_; Name = $_ } }) }
if ($ports.Count -eq 0) { Fail "Kein Board gefunden. USB-Kabel anschließen (Datenkabel, nicht nur Ladekabel). Hilft das nicht: BOOT-Taste halten, Kabel einstecken, loslassen." }
$port = $ports[0].Port
if ($ports.Count -gt 1) {
  Write-Host "Mehrere Anschlüsse:"; for ($i = 0; $i -lt $ports.Count; $i++) { Write-Host ("  {0}) {1}" -f ($i + 1), $ports[$i].Name) }
  $n = [int](Read-Host "Nummer wählen"); if ($n -lt 1 -or $n -gt $ports.Count) { Fail "Ungültige Auswahl." }
  $port = $ports[$n - 1].Port
}
Write-Host "Anschluss: $port"

# 4. Prüfen oder flashen
if ($Check) {
  & $esptool --chip esp32s3 -p $port chip_id
  if ($LASTEXITCODE -ne 0) { Fail "Keine Antwort vom Board." }
  Write-Host "`nVerbindung in Ordnung. Es wurde nichts geändert." -ForegroundColor Green
  exit 0
}
Write-Host "`nACHTUNG: Das Board wird komplett neu beschrieben. Eingerichtetes WLAN, Einstellungen und hochgeladene Bilder gehen verloren." -ForegroundColor Yellow
if ((Read-Host "Fortfahren? [j/N]") -notmatch "^[jJyY]$") { Write-Host "Abgebrochen."; exit 0 }

Write-Host "`nSpiele die Firmware auf ..."
& $esptool --chip esp32s3 -p $port -b 921600 write_flash -z 0x0 $Bin
if ($LASTEXITCODE -ne 0) {
  Write-Host "`nNochmal langsamer ..."
  & $esptool --chip esp32s3 -p $port -b 115200 write_flash -z 0x0 $Bin
  if ($LASTEXITCODE -ne 0) { Fail "Aufspielen fehlgeschlagen. BOOT-Taste halten, Kabel neu einstecken, loslassen und noch einmal versuchen." }
}
Write-Host "`nFertig. Das Display startet neu und zeigt die Einrichtung:" -ForegroundColor Green
Write-Host "  1. Mit dem WLAN 'Tuerspion-Display-Setup' verbinden (Kennwort steht auf dem Display)"
Write-Host "  2. Im Browser http://192.168.4.1 öffnen und WLAN und die Zugangsdaten des Türspions eintragen"
Read-Host "`nZum Beenden Enter drücken"
