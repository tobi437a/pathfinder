# Rebuild Debug, then score the three 1.8 regression sets (see CLAUDE.md "Current work"):
#   tools\regress-all.ps1 [-Base cur] [-Save name] [-NoBuild]
# runs (vs regress-<Base>.json, default tol), runs-dual (vs t6) and runs-pre1.8 (vs pt6), both at tol 1e-6.
param([string]$Base = "cur", [string]$Save = "", [switch]$NoBuild)
$repo = Split-Path $PSScriptRoot -Parent
if (-not $NoBuild) {
    & "$PSScriptRoot\build.ps1" | Select-Object -Last 1
}
$saveArgs = @()
if ($Save) { $saveArgs = @("--save", $Save) }
python "$PSScriptRoot\regress.py" --p2 --rot --rotfly --base $Base @saveArgs | Select-Object -Last 3
python "$PSScriptRoot\regress.py" --p2 --rot --rotfly --tol 1e-6 --runs runs-dual --base t6 | Select-Object -Last 3
python "$PSScriptRoot\regress.py" --p2 --rot --rotfly --tol 1e-6 --runs runs-pre1.8 --base pt6 | Select-Object -Last 3
