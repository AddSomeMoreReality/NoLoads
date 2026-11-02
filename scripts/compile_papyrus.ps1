<#
.SYNOPSIS
    Compiles res\scripts\*.psc into res\data\Scripts\*.pex with the Creation Kit's Papyrus compiler.
    The .pex files are kept in the repository: build.ps1 only packs them, so a build needs no Creation Kit.
.PARAMETER GamePath
    Default: E:\Games\Skyrim\The Elder Scrolls V - Skyrim (needs "Papyrus Compiler" and Data\Source\Scripts).
#>
param(
    [string]$GamePath = 'E:\Games\Skyrim\The Elder Scrolls V - Skyrim'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$compiler = Join-Path $GamePath 'Papyrus Compiler\PapyrusCompiler.exe'
$imports = Join-Path $GamePath 'Data\Source\Scripts'
$flags = Join-Path $imports 'TESV_Papyrus_Flags.flg'
$out = New-Item -ItemType Directory -Force -Path (Join-Path $root 'res\data\Scripts')

foreach ($source in Get-ChildItem (Join-Path $root 'res\scripts') -Filter *.psc) {
    # Our folder first: a script of ours with a vanilla name shadows the vanilla source.
    & $compiler $source.FullName "-f=$flags" "-i=$($source.DirectoryName);$imports" "-o=$($out.FullName)"
    if ($LASTEXITCODE -ne 0) {
        throw "PapyrusCompiler failed on $($source.Name) (exit $LASTEXITCODE)"
    }
}
Get-ChildItem $out -Filter *.pex | ForEach-Object { Write-Host "== $($_.Name) $($_.Length) bytes" }
