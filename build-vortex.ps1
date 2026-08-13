param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"
$Package = Join-Path $Root "package"
$OptionalOPubesPackage = Join-Path $Root "optional\OPubes\package"
$OptionalRelayHostPackage = Join-Path $Root "optional\RelayHost\package"
$FomodSource = Join-Path $Root "fomod"
$Plugins = Join-Path $Package "Data\SKSE\Plugins"
$Stage = [System.IO.Path]::GetFullPath((Join-Path $Build "fomod-stage"))

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

$CoreIni = Join-Path $Plugins "MorphSyncTogether.ini"
$OPubesIni = Join-Path $OptionalOPubesPackage "Data\SKSE\Plugins\MorphSyncTogether_OPubes.ini"
$RelayHostIni = Join-Path $OptionalRelayHostPackage "Data\SKSE\Plugins\MorphSyncTogether_RelayHost.ini"
$ModuleConfig = Join-Path $FomodSource "ModuleConfig.xml"
$Info = Join-Path $FomodSource "info.xml"
$ModuleImage = Join-Path $FomodSource "ModuleImage.png"

foreach ($RequiredPath in @($CoreIni, $OPubesIni, $RelayHostIni, $ModuleConfig, $Info, $ModuleImage)) {
    if (-not (Test-Path -LiteralPath $RequiredPath)) {
        throw "Fichier FOMOD requis introuvable: $RequiredPath"
    }
}

$CoreIniContent = Get-Content -LiteralPath $CoreIni -Raw
$OPubesIniContent = Get-Content -LiteralPath $OPubesIni -Raw
$RelayHostIniContent = Get-Content -LiteralPath $RelayHostIni -Raw
if ($CoreIniContent -notmatch '(?ms)^\[PubicOverlaySync\].*?^Enabled=0\s*$') {
    throw "Le profil FOMOD principal doit desactiver PubicOverlaySync."
}
if ($OPubesIniContent -notmatch '(?ms)^\[PubicOverlaySync\].*?^Enabled=1\s*$') {
    throw "Le profil FOMOD OPubes doit activer PubicOverlaySync."
}
if ($RelayHostIniContent -notmatch '(?ms)^\[Network\].*?^RelayMode=1\s*$') {
    throw "Le profil FOMOD relais host doit activer RelayMode."
}

try {
    [void][xml](Get-Content -LiteralPath $ModuleConfig -Raw)
    [void][xml](Get-Content -LiteralPath $Info -Raw)
} catch {
    throw "Metadonnees FOMOD XML invalides: $($_.Exception.Message)"
}

$BuildRoot = [System.IO.Path]::GetFullPath($Build).TrimEnd('\')
if (-not $Stage.StartsWith("$BuildRoot\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Le repertoire temporaire FOMOD est hors du repertoire de build: $Stage"
}
if (Test-Path -LiteralPath $Stage) {
    Remove-Item -LiteralPath $Stage -Recurse -Force
}

$CoreStage = Join-Path $Stage "00 Core"
$OPubesStage = Join-Path $Stage "10 OPubes"
$RelayHostStage = Join-Path $Stage "20 Internet Relay Host"
$FomodStage = Join-Path $Stage "fomod"
New-Item -ItemType Directory -Force -Path $CoreStage, $OPubesStage, $RelayHostStage, $FomodStage | Out-Null

Copy-Item (Join-Path $Package "*") $CoreStage -Recurse -Force
Copy-Item (Join-Path $OptionalOPubesPackage "*") $OPubesStage -Recurse -Force
Copy-Item (Join-Path $OptionalRelayHostPackage "*") $RelayHostStage -Recurse -Force
Copy-Item (Join-Path $FomodSource "*") $FomodStage -Recurse -Force

$zip = Join-Path $Root "MorphSyncTogether-v0.4.0-FOMOD.zip"
if (Test-Path $zip) {
    Remove-Item $zip -Force
}

Compress-Archive `
    -Path (Join-Path $Stage "*") `
    -DestinationPath $zip `
    -Force

Add-Type -AssemblyName System.IO.Compression.FileSystem
$Archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    $Entries = @($Archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
    $RequiredEntries = @(
        "00 Core/Data/SKSE/Plugins/MorphSyncTogether.dll",
        "00 Core/Data/SKSE/Plugins/MorphSyncTogether.ini",
        "10 OPubes/Data/SKSE/Plugins/MorphSyncTogether_OPubes.ini",
        "20 Internet Relay Host/Data/SKSE/Plugins/MorphSyncTogether_RelayHost.ini",
        "fomod/ModuleConfig.xml",
        "fomod/info.xml",
        "fomod/ModuleImage.png"
    )
    foreach ($RequiredEntry in $RequiredEntries) {
        if ($Entries -notcontains $RequiredEntry) {
            throw "Entree FOMOD absente de l'archive: $RequiredEntry"
        }
    }
} finally {
    $Archive.Dispose()
}

Write-Host ""
Write-Host "OK - package FOMOD cree et verifie :" -ForegroundColor Green
Write-Host $zip
