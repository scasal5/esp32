$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/..").Path
$bun="$root/.tools/bun/bun-windows-x64/bun.exe"
Push-Location "$root/app"
try {
    & $bun install
    if($LASTEXITCODE -ne 0){throw 'Guest dependency install failed'}
    New-Item -ItemType Directory -Force "$root/app/node_modules/@pocketjs" | Out-Null
    $frameworkLink="$root/app/node_modules/@pocketjs/framework"
    if(-not (Test-Path -LiteralPath $frameworkLink)) {
        New-Item -ItemType Junction -Path $frameworkLink -Target "$root/.deps/pocketjs" | Out-Null
    }
    & $bun "$root/.deps/pocketjs/tools/pocket.ts" build --manifest "$root/app/pocket.json" --host-profile "$root/pocket.host.json" --project-root "$root/app" --outdir "$root/out/app" --output "$root/out/app/harness.pocket"
    if($LASTEXITCODE -ne 0){throw 'Package compilation failed'}
    & $bun "$PSScriptRoot/minify_package.ts" "$root/out/app/harness.pocket"
    if($LASTEXITCODE -ne 0){throw 'Package minification failed'}
} finally {Pop-Location}
