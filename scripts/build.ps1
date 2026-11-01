<#
.SYNOPSIS
    Builds CellLoadResearch and packs a Vortex-ready archive: dist/CellLoadResearch-<version>.zip

.PARAMETER LogDirectory
    Written into sLogDirectory of the packaged INI. The plugin puts its log and trace there.
    Default: <repo>\logs. Pass an empty string to keep the SKSE default (<MyDocuments>\My Games\...\SKSE).

.PARAMETER Mode
    xmake build mode (releasedbg or debug).
#>
[CmdletBinding()]
param(
    [string]$LogDirectory,
    [ValidateSet('releasedbg', 'debug')]
    [string]$Mode = 'releasedbg'
)

$ErrorActionPreference = 'Stop'

# Here, not as the parameter's default: $PSScriptRoot can be empty inside param().
if (-not $PSBoundParameters.ContainsKey('LogDirectory')) {
    $LogDirectory = Join-Path $PSScriptRoot '..\logs'
}

$name = 'CellLoadResearch'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Invoke-Native {
    param([string]$Exe, [string[]]$ArgList)
    & $Exe @ArgList
    if ($LASTEXITCODE -ne 0) {
        throw "'$Exe $($ArgList -join ' ')' failed with exit code $LASTEXITCODE"
    }
}

# With these set, xmake installs the DLL straight into Data, bypassing Vortex.
Remove-Item Env:XSE_TES5_GAME_PATH, Env:XSE_TES5_MODS_PATH -ErrorAction SilentlyContinue

if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) {
    throw 'xmake is not in PATH. Install xmake >= 3.0 (https://xmake.io) and reopen the terminal.'
}

$xmakeLua = [IO.File]::ReadAllText((Join-Path $root 'xmake.lua'))
if ($xmakeLua -notmatch 'set_version\("(\d+\.\d+\.\d+)"\)') {
    throw 'set_version("X.Y.Z") not found in xmake.lua'
}
$version = $Matches[1]
Write-Host "== $name $version ($Mode)"

# 1. Build
Push-Location $root
try {
    Invoke-Native 'xmake' @('f', '-m', $Mode, '--skyrim_vr=n', '-y')
    Invoke-Native 'xmake' @('build', '-y', $name)
}
finally {
    Pop-Location
}

$binDir = Join-Path $root "build\windows\x64\$Mode"
$dll = Join-Path $binDir "$name.dll"
$pdb = Join-Path $binDir "$name.pdb"
foreach ($file in $dll, $pdb) {
    if (-not (Test-Path $file)) { throw "Build output not found: $file" }
}

# 2. Stage
$stage = Join-Path $root 'build\stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$plugins = New-Item -ItemType Directory -Force -Path (Join-Path $stage 'SKSE\Plugins')
Copy-Item $dll, $pdb -Destination $plugins

$logDir = ''
if ($LogDirectory) {
    $logDir = [IO.Path]::GetFullPath($LogDirectory)
    # The plugin reads the INI with GetPrivateProfileStringW, which decodes a BOM-less file as ANSI.
    if ($logDir -match '[^\x00-\x7F]') {
        Write-Warning "Log directory contains non-ASCII characters and may be misread by the plugin: $logDir"
    }
}
$ini = [IO.File]::ReadAllText((Join-Path $root "res\$name.ini")) -replace "`r`n", "`n"
$ini = [regex]::Replace($ini, '(?m)^sLogDirectory=.*$', { param($m) "sLogDirectory=$logDir" })
# No BOM: GetPrivateProfileString would treat it as part of the first section name.
[IO.File]::WriteAllText((Join-Path $plugins "$name.ini"), ($ini -replace "`n", "`r`n"), (New-Object Text.UTF8Encoding($false)))

# Loose game files next to SKSE (meshes, textures): res\data\<Folder> -> <Folder> at the archive root.
$dataRoot = Join-Path $root 'res\data'
$dataFolders = @()
if (Test-Path $dataRoot) {
    foreach ($folder in Get-ChildItem $dataRoot -Directory) {
        Copy-Item $folder.FullName -Destination $stage -Recurse
        $dataFolders += $folder.Name
    }
}

# 3. Archive. Windows bsdtar writes '/' separators; Compress-Archive (PS 5.1) writes '\'.
$dist = New-Item -ItemType Directory -Force -Path (Join-Path $root 'dist')
$zip = Join-Path $dist "$name-$version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }

$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
Invoke-Native $tar (@('-a', '-c', '-f', $zip, '-C', $stage, 'SKSE') + $dataFolders)

$entries = @(& $tar -tf $zip)
Write-Host "== $zip"
$entries | ForEach-Object { Write-Host "   $_" }

$roots = @('SKSE') + $dataFolders
$bad = @($entries | Where-Object { ($roots -notcontains ($_ -split '/')[0]) -or $_.Contains('\') })
if ($bad.Count -gt 0) {
    throw "Unexpected archive layout (first level must be $($roots -join ', '), separators '/'): $($bad -join ', ')"
}

Write-Host "== sLogDirectory=$logDir"
Write-Host '== Vortex: Mods -> Install From File -> Enable -> Deploy (for an update choose Replace)'
