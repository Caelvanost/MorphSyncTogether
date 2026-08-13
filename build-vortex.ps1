param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"
$Package = Join-Path $Root "package"
$Plugins = Join-Path $Package "Data\SKSE\Plugins"

if (-not $VcpkgRoot) {
    throw "VCPKG_ROOT n'est pas defini."
}

$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $Toolchain)) {
    throw "Toolchain vcpkg introuvable: $Toolchain"
}

$ConfigureArguments = @(
    "-S", $Root,
    "-B", $Build,
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain"
)

$Cache = Join-Path $Build "CMakeCache.txt"
if (Test-Path $Cache) {
    $CachedHome = Select-String `
        -Path $Cache `
        -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' |
        Select-Object -First 1

    if ($CachedHome -and
        -not [string]::Equals(
            [System.IO.Path]::GetFullPath($CachedHome.Matches[0].Groups[1].Value),
            [System.IO.Path]::GetFullPath($Root),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Host "Le projet a change de chemin; regeneration du cache CMake."
        $ConfigureArguments = @("--fresh") + $ConfigureArguments
    }
}

& cmake @ConfigureArguments
if ($LASTEXITCODE -ne 0) {
    throw "La configuration CMake a echoue (code $LASTEXITCODE)."
}

$GeneratedPlugin = Join-Path $Build "__MorphSyncTogetherPlugin.cpp"
if (Test-Path $GeneratedPlugin) {
    $content = Get-Content $GeneratedPlugin -Raw
    if ($content -notmatch "using namespace std::literals") {
        $content = "using namespace std::literals;`r`n" + $content
        Set-Content -Path $GeneratedPlugin -Value $content -Encoding UTF8
        Write-Host "CommonLib generated-file workaround applied."
    }
}

& cmake --build $Build --config $Configuration
if ($LASTEXITCODE -ne 0) {
    throw "La compilation CMake a echoue (code $LASTEXITCODE)."
}

$dll = Get-ChildItem -Path $Build -Recurse -Filter "MorphSyncTogether.dll" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $dll) {
    throw "MorphSyncTogether.dll n'a pas ete trouve apres compilation."
}

New-Item -ItemType Directory -Force -Path $Plugins | Out-Null
Copy-Item $dll.FullName (Join-Path $Plugins "MorphSyncTogether.dll") -Force

$zip = Join-Path $Root "MorphSyncTogether-v0.2.10-Vortex.zip"
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
