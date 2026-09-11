<#
    Restaura la placa al estado de fabrica desde la imagen completa de 16 MB.

    OJO: esto SOBRESCRIBE los 16 MB enteros, incluida la NVS.
    Necesita el backup hecho antes del primer flash propio.
#>

param(
    [string]$Port = "COM3",
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# El Desktop de esta maquina esta redirigido (D:\Escritorio), asi que se
# resuelve por API en vez de asumir $env:USERPROFILE\Desktop.
$Backup = Join-Path ([Environment]::GetFolderPath('Desktop')) 'ws183-factory-backup'
$Img    = Join-Path $Backup 'flash_16mb.bin'

if (-not (Test-Path $Img)) { throw "Falta la imagen de backup: $Img" }

$len = (Get-Item $Img).Length
if ($len -ne 16777216) { throw "La imagen mide $len bytes, se esperaban 16777216" }

# esptool: primero en PATH (lo deja el export.ps1 de ESP-IDF), si no via python
$esptool = $null
if (Get-Command esptool -ErrorAction SilentlyContinue) {
    $esptool = @('esptool')
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $esptool = @('python', '-m', 'esptool')
} else {
    throw "No se encontro esptool. Ejecuta el export.ps1 de ESP-IDF o instala esptool."
}

Write-Host "Puerto : $Port"
Write-Host "Imagen : $Img ($len bytes)"
Write-Host ""
Write-Host "Esto sobrescribe los 16 MB completos, incluida la NVS." -ForegroundColor Yellow

if (-not $Force) {
    $ans = Read-Host "Escribi 'RESTAURAR' para continuar"
    if ($ans -ne 'RESTAURAR') { Write-Host "Cancelado."; return }
}

# write_flash con guion bajo: es el nombre nativo en esptool 4.x (la version que
# trae ESP-IDF 5.5) y sigue aceptado como alias en 5.x. La forma con guion
# (write-flash) solo existe en 5.x y falla con el esptool del IDF.
& $esptool[0] @($esptool[1..($esptool.Count - 1)]) --port $Port write_flash 0x0 $Img
if ($LASTEXITCODE -ne 0) { throw "esptool fallo con codigo $LASTEXITCODE" }

Write-Host ""
Write-Host "Restaurado. La placa arranca de nuevo el demo de fabrica." -ForegroundColor Green
Write-Host ""
Write-Host "Si la placa no responde por USB: manten apretado BOOT (GPIO0),"
Write-Host "enchufa el USB, espera 2 s y solta. Eso entra al ROM antes de que"
Write-Host "el firmware duerma el USB-Serial-JTAG."
