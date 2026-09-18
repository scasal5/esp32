param([ValidateSet('off','sta')][string]$Wifi='off',
      [string]$IdfPath='D:/esp/esp-idf',[string]$ToolsPath='D:/esp/tools')
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/..").Path
$firmware=(Resolve-Path "$root/../firmware").Path
$build="$root/build-baseline-$Wifi"
New-Item -ItemType Directory -Force $build | Out-Null
$config=Get-Content "$firmware/sdkconfig" -Raw
foreach($name in @('WS183_BASELINE_METRICS','WS183_BASELINE_WIFI_OFF','ESP_PHY_CALIBRATION_AND_DATA_STORAGE')){
    $config=$config -replace "(?m)^(CONFIG_${name}=.*|# CONFIG_${name} is not set)\r?\n",''
}
$config+="`nCONFIG_WS183_BASELINE_METRICS=y`n# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set`n"
if($Wifi -eq 'off'){$config+="CONFIG_WS183_BASELINE_WIFI_OFF=y`n"}
else{$config+="# CONFIG_WS183_BASELINE_WIFI_OFF is not set`n"}
Set-Content "$build/sdkconfig" $config -Encoding utf8
$env:IDF_PATH=$IdfPath
$env:IDF_TOOLS_PATH=$ToolsPath
. "$IdfPath/export.ps1" *> "$root/.tools/baseline-export.log"
& python "$PSScriptRoot/source_receipt.py" snapshot --mode baseline --out "$build/source-inputs.json"
if($LASTEXITCODE -ne 0){throw 'Source snapshot failed'}
& python "$IdfPath/tools/idf.py" -C $firmware -B $build "-DSDKCONFIG=$build/sdkconfig" build
if($LASTEXITCODE -ne 0){throw 'Baseline build failed'}
& python "$PSScriptRoot/source_receipt.py" verify --mode baseline --out "$build/source-inputs.json"
if($LASTEXITCODE -ne 0){throw 'Baseline inputs changed; repeat this build'}
& python "$PSScriptRoot/receipt.py" --build $build --mode baseline --profile product
if($LASTEXITCODE -ne 0){throw 'Baseline receipt failed'}
Write-Host 'Use flash_ota0.py only. Never idf.py flash.'
