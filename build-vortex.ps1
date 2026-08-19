param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"
$Package = Join-Path $Root "package"
$Dist = Join-Path $Root "dist"
$PubesFemalePackage = Join-Path $Root "optional\PubesForeverFemale\package"
$PubesMalePackage = Join-Path $Root "optional\PubesForeverMale\package"
$NordicWarmaidenPackage = Join-Path $Root "optional\NordicWarmaiden\package"
$HIMBOBodyhairPackage = Join-Path $Root "optional\HIMBOBodyhair\package"
$FomodSource = Join-Path $Root "fomod"
$Plugins = Join-Path $Package "Data\SKSE\Plugins"
$Stage = [System.IO.Path]::GetFullPath((Join-Path $Build "fomod-stage"))

if (-not $VcpkgRoot) { throw "VCPKG_ROOT n'est pas defini." }
$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $Toolchain)) { throw "Toolchain vcpkg introuvable: $Toolchain" }

$ConfigureArguments = @("-S", $Root, "-B", $Build, "-DCMAKE_TOOLCHAIN_FILE=$Toolchain")
$Cache = Join-Path $Build "CMakeCache.txt"
if (Test-Path $Cache) {
    $CachedHome = Select-String -Path $Cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
    if ($CachedHome -and -not [string]::Equals([System.IO.Path]::GetFullPath($CachedHome.Matches[0].Groups[1].Value), [System.IO.Path]::GetFullPath($Root), [System.StringComparison]::OrdinalIgnoreCase)) {
        $ConfigureArguments = @("--fresh") + $ConfigureArguments
    }
}

& cmake @ConfigureArguments
if ($LASTEXITCODE -ne 0) { throw "La configuration CMake a echoue (code $LASTEXITCODE)." }

$GeneratedPlugin = Join-Path $Build "__MorphSyncTogetherPlugin.cpp"
if (Test-Path $GeneratedPlugin) {
    $content = Get-Content $GeneratedPlugin -Raw
    if ($content -notmatch "using namespace std::literals") {
        $content = "using namespace std::literals;`r`n" + $content
        Set-Content -Path $GeneratedPlugin -Value $content -Encoding UTF8
    }
}

& cmake --build $Build --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "La compilation CMake a echoue (code $LASTEXITCODE)." }

$dll = Get-ChildItem -Path $Build -Recurse -Filter "MorphSyncTogether.dll" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $dll) { throw "MorphSyncTogether.dll n'a pas ete trouve apres compilation." }

New-Item -ItemType Directory -Force -Path $Plugins | Out-Null
Copy-Item $dll.FullName (Join-Path $Plugins "MorphSyncTogether.dll") -Force

$CoreIni = Join-Path $Plugins "MorphSyncTogether.ini"
$RequiredPaths = @(
    $CoreIni,
    (Join-Path $PubesFemalePackage "Data\SKSE\Plugins\MorphSyncTogether\Providers\PubesForeverFemale.enabled"),
    (Join-Path $PubesMalePackage "Data\SKSE\Plugins\MorphSyncTogether\Providers\PubesForeverMale.enabled"),
    (Join-Path $NordicWarmaidenPackage "Data\SKSE\Plugins\MorphSyncTogether\Providers\NordicWarmaiden.enabled"),
    (Join-Path $HIMBOBodyhairPackage "Data\SKSE\Plugins\MorphSyncTogether\Providers\HIMBOBodyhair.enabled"),
    (Join-Path $FomodSource "ModuleConfig.xml"),
    (Join-Path $FomodSource "info.xml"),
    (Join-Path $FomodSource "ModuleImage.png")
)
foreach ($RequiredPath in $RequiredPaths) {
    if (-not (Test-Path -LiteralPath $RequiredPath)) { throw "Fichier FOMOD requis introuvable: $RequiredPath" }
}

try {
    [void][xml](Get-Content -LiteralPath (Join-Path $FomodSource "ModuleConfig.xml") -Raw)
    [void][xml](Get-Content -LiteralPath (Join-Path $FomodSource "info.xml") -Raw)
} catch { throw "Metadonnees FOMOD XML invalides: $($_.Exception.Message)" }

if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
$CoreStage = Join-Path $Stage "00 Core"
$PubesFemaleStage = Join-Path $Stage "10 Pubes Forever Female"
$PubesMaleStage = Join-Path $Stage "20 Pubes Forever Male"
$NordicWarmaidenStage = Join-Path $Stage "30 Nordic Warmaiden"
$HIMBOBodyhairStage = Join-Path $Stage "40 HIMBO Bodyhair"
$FomodStage = Join-Path $Stage "fomod"
New-Item -ItemType Directory -Force -Path $CoreStage,$PubesFemaleStage,$PubesMaleStage,$NordicWarmaidenStage,$HIMBOBodyhairStage,$FomodStage | Out-Null

Copy-Item (Join-Path $Package "*") $CoreStage -Recurse -Force
Copy-Item (Join-Path $PubesFemalePackage "*") $PubesFemaleStage -Recurse -Force
Copy-Item (Join-Path $PubesMalePackage "*") $PubesMaleStage -Recurse -Force
Copy-Item (Join-Path $NordicWarmaidenPackage "*") $NordicWarmaidenStage -Recurse -Force
Copy-Item (Join-Path $HIMBOBodyhairPackage "*") $HIMBOBodyhairStage -Recurse -Force
Copy-Item (Join-Path $FomodSource "*") $FomodStage -Recurse -Force

New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$zip = Join-Path $Dist "MorphSyncTogether-v0.3.0-FOMOD.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $zip -Force

Write-Host ""
Write-Host "OK - package FOMOD cree :" -ForegroundColor Green
Write-Host $zip
