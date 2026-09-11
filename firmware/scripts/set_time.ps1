<#
    Pone el RTC de la placa en la hora actual de la PC, por USB-Serial-JTAG.

    Manda "settime <epoch UTC>" a la consola del firmware y muestra la
    respuesta. El puerto tiene que estar libre: cerrar idf.py monitor antes.

    Usa pyserial (viene con el entorno Python de ESP-IDF: correr antes el
    export.ps1). System.IO.Ports de .NET no sirve con este puerto: en las
    pruebas no recibio ni un byte del USB-Serial-JTAG, con DTR/RTS en 0 o en 1.
#>

param(
    [string]$Port = "COM3"
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "No se encontro python. Ejecuta el export.ps1 de ESP-IDF (trae pyserial)."
}

# pyserial abre con DTR y RTS en 1, que no dispara el auto-reset (EN baja con
# RTS=1 y DTR=0). El epoch se calcula justo antes de mandarlo.
$code = @'
import sys, time
import serial

port = sys.argv[1]
s = serial.Serial(port, 115200, timeout=0.2)
try:
    s.reset_input_buffer()
    s.write(b"\n")                      # linea vacia: descarta basura parcial
    time.sleep(0.3)
    s.reset_input_buffer()

    epoch = int(time.time())
    print("enviando settime %d" % epoch)
    s.write(("settime %d\n" % epoch).encode())

    out = b""
    deadline = time.time() + 3
    while time.time() < deadline and b"hora local" not in out:
        out += s.read(4096)
finally:
    s.close()

text = out.decode("utf-8", "replace")
for line in text.splitlines():
    if "settime" in line or "hora local" in line:
        print(line.strip())
sys.exit(0 if "settime ok" in text else 1)
'@

# A un archivo temporal y no por "python -c": Windows PowerShell 5.1 se come
# las comillas dobles de los argumentos de programas nativos.
$script = Join-Path ([IO.Path]::GetTempPath()) "ws183_set_time_$PID.py"
Set-Content -Path $script -Value $code -Encoding ASCII
try {
    & python $script $Port
    $rc = $LASTEXITCODE
} finally {
    Remove-Item $script -ErrorAction SilentlyContinue
}
if ($rc -ne 0) {
    throw "La placa no confirmo settime. Puerto ocupado, o firmware sin consola?"
}
