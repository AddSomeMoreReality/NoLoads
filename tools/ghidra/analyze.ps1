# Curator: (re)create the master Ghidra project of SkyrimSE.exe from scratch (docs/GHIDRA.md).
# Ghidra must be closed. About 15 minutes, up to 10 GB of RAM: do not run the game meanwhile.
#   powershell -File tools\ghidra\analyze.ps1 -LogDir <scratchpad>
param(
    [string]$GhidraDir = 'E:\Ghidra\ghidra_12.2_PUBLIC',
    [string]$ProjectDir = 'E:\Games\Skyrim\ModCreation\My attempts\SkyrimSE-1.6.1170',
    [string]$ProjectName = 'SkyrimSE-1.6.1170',
    [string]$Exe = 'E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe',
    [string]$Heap = '10G',
    [string]$LogDir = $env:TEMP
)
$ErrorActionPreference = 'Stop'
$headless = Join-Path $GhidraDir 'support\analyzeHeadless.bat'
$scripts = Join-Path $PSScriptRoot 'scripts'
$program = Split-Path $Exe -Leaf
$env:GHIDRA_HEADLESS_MAXMEM = $Heap  # this process only
New-Item -ItemType Directory -Force $ProjectDir | Out-Null

# 1. Import without analysis; the pre-script marks the program as MSVC-built (enables the RTTI analyzer).
$t0 = Get-Date
& $headless $ProjectDir $ProjectName -import $Exe -overwrite -noanalysis `
    -scriptPath $scripts -preScript ClrAnalysisOptions.java -log (Join-Path $LogDir 'ghidra-import.log')
if ($LASTEXITCODE -ne 0) { throw "import failed: $LASTEXITCODE" }

# 2. Full auto-analysis in a fresh process, so the analyzer set is chosen with the compiler already set.
$t1 = Get-Date
& $headless $ProjectDir $ProjectName -process $program `
    -log (Join-Path $LogDir 'ghidra-analyze.log')
if ($LASTEXITCODE -ne 0) { throw "analysis failed: $LASTEXITCODE" }
$t2 = Get-Date
"import {0:N0} s, analysis {1:N0} s" -f ($t1 - $t0).TotalSeconds, ($t2 - $t1).TotalSeconds
