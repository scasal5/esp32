param([string]$RustRoot="$PSScriptRoot/../.tools/rust-dist/esp")
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/..").Path
$rust=(Resolve-Path $RustRoot).Path
$link=Get-ChildItem "$root/.tools/msvc" -Recurse -Filter link.exe | Where-Object {$_.FullName -match 'Hostx64\\x64'} | Select-Object -First 1
$crt=Get-ChildItem "$root/.tools/msvc" -Recurse -Filter msvcrt.lib | Where-Object {$_.FullName -match '\\x64\\'} | Select-Object -First 1
$um=Get-ChildItem "$root/.tools/sdk" -Recurse -Filter kernel32.lib | Select-Object -First 1
$ucrt=Get-ChildItem "$root/.tools/sdk" -Recurse -Filter ucrt.lib | Where-Object {$_.FullName -notmatch 'enclave'} | Select-Object -First 1
if(-not ($link -and $crt -and $um -and $ucrt)){throw 'Missing isolated MSVC linker/CRT/Windows SDK libraries'}
# Upstream prepends a POSIX ':' to PATH even on Windows. Keep a disposable
# first entry so the following linker/tool entries survive that concatenation.
$env:PATH="$rust/bin;$($link.DirectoryName);$rust/bin;$env:PATH"
$env:LIB="$($crt.DirectoryName);$($um.DirectoryName);$($ucrt.DirectoryName)"
$env:CARGO_HOME="$root/.tools/cargo"
& "$root/.tools/bun/bun-windows-x64/bun.exe" "$root/.deps/pocketjs/tools/esp-idf-native.ts" --target esp32s3 --cargo "$rust/bin/cargo.exe"
if($LASTEXITCODE -ne 0){throw 'Upstream native archive build failed'}
