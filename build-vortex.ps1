param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"
$Package = Join-Path $Root "package"
$Dist = Join-Path $Root "dist"
$Plugins = Join-Path $Package "Data\SKSE\Plugins"

if (-not $VcpkgRoot) {
    throw "VCPKG_ROOT n'est pas defini."
}

$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $Toolchain)) {
    throw "Toolchain vcpkg introuvable: $Toolchain"
}

cmake -S $Root -B $Build `
    -DCMAKE_TOOLCHAIN_FILE="$Toolchain"

$GeneratedPlugin = Join-Path $Build "__MorphSyncTogetherPlugin.cpp"
if (Test-Path $GeneratedPlugin) {
    $content = Get-Content $GeneratedPlugin -Raw
    if ($content -notmatch "using namespace std::literals") {
        $content = "using namespace std::literals;`r`n" + $content
        Set-Content -Path $GeneratedPlugin -Value $content -Encoding UTF8
        Write-Host "CommonLib generated-file workaround applied."
    }
}

cmake --build $Build --config $Configuration

$dll = Get-ChildItem -Path $Build -Recurse -Filter "MorphSyncTogether.dll" | Select-Object -First 1
if (-not $dll) {
    throw "MorphSyncTogether.dll n'a pas ete trouve apres compilation."
}

New-Item -ItemType Directory -Force -Path $Plugins | Out-Null
Copy-Item $dll.FullName (Join-Path $Plugins "MorphSyncTogether.dll") -Force

New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$zip = Join-Path $Dist "MorphSyncTogether-v0.2.7-Vortex.zip"
if (Test-Path $zip) {
    Remove-Item $zip -Force
}

Compress-Archive `
    -Path (Join-Path $Package "*") `
    -DestinationPath $zip `
    -Force

Write-Host ""
Write-Host "OK - package Vortex cree :" -ForegroundColor Green
Write-Host $zip
