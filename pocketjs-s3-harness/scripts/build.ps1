param(
    [ValidateSet('probe','native','headless','pocket')][string]$Mode='native',
    [ValidateSet('debug','perf')][string]$Profile='debug',
    [string]$IdfPath="$PSScriptRoot/../.deps/esp-idf",
    [string]$ToolsPath="$PSScriptRoot/../.tools",
    [string]$ExtraDefaults='',
    [ValidatePattern('^[a-z0-9-]*$')][string]$Variant='',
    [switch]$Reconfigure
)
$ErrorActionPreference='Stop'
# Git Bash re-injects MSYSTEM into child processes; idf_tools.py aborts if it is set.
if ($env:MSYSTEM) { Remove-Item Env:MSYSTEM }
$root=(Resolve-Path "$PSScriptRoot/..").Path
$env:IDF_PATH=(Resolve-Path $IdfPath).Path
$env:IDF_TOOLS_PATH=(Resolve-Path $ToolsPath).Path
. "$env:IDF_PATH/export.ps1" *> "$root/.tools/export.log"
if ($LASTEXITCODE -ne 0) {throw 'IDF export failed; inspect .tools/export.log'}
$defaults="$root/sdkconfig.defaults"
if($Profile -eq 'perf') {$defaults+=";$root/config/sdkconfig.perf"}
if($ExtraDefaults) {$defaults+=";"+(Resolve-Path $ExtraDefaults).Path}
$build="$root/build-$Mode-$Profile"
if($Variant){$build+="-$Variant"}
& python "$PSScriptRoot/source_receipt.py" snapshot --mode $Mode --out "$build/source-inputs.json"
if($LASTEXITCODE -ne 0){throw 'Source snapshot failed'}
$arguments=@('-C',$root,'-B',$build,"-DHARNESS_MODE=$Mode","-DSDKCONFIG=$build/sdkconfig","-DSDKCONFIG_DEFAULTS=$defaults")
if($Reconfigure) {$arguments+='reconfigure'} else {$arguments+='build'}
& python "$env:IDF_PATH/tools/idf.py" @arguments
if($LASTEXITCODE -ne 0){throw "Build failed: $Mode/$Profile"}
& python "$PSScriptRoot/source_receipt.py" verify --mode $Mode --out "$build/source-inputs.json"
if($LASTEXITCODE -ne 0){throw 'Build inputs changed; repeat this build'}
if(-not $Reconfigure){& python "$PSScriptRoot/receipt.py" --build $build --mode $Mode --profile $Profile}
if($LASTEXITCODE -ne 0){throw 'Build receipt failed'}
# idf.py prints write_flash 0x100000 (factory). That is forbidden on this board.
Write-Host ""
Write-Host "Do not run idf.py flash. It writes bootloader, table and factory." -ForegroundColor Yellow
Write-Host ("Flash ota_0 only: python `"$PSScriptRoot/flash_ota0.py`" --build `"$build`" --port <PORT>")
Write-Host "Select/restore boot with scripts/gate0.py probe|restore after a backup."
