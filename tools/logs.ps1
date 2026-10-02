<#
.SYNOPSIS
  Show the latest Geode log or crash log, with GD addresses resolved to function names.

.EXAMPLE
  tools\logs.ps1                       # last 60 lines of the newest Geode log mentioning pathfinder or errors
  tools\logs.ps1 -All -Tail 200        # last 200 lines, unfiltered
  tools\logs.ps1 -Crash                # newest crash log, GeometryDash.exe+offsets symbolised
#>
param(
    [int]$Tail = 60,
    [switch]$All,
    [switch]$Crash,
    [string]$Pattern = 'pathfinder|\bERROR\b|\[error\]|Error|crash'
)
$ErrorActionPreference = 'Stop'
$gd = if ($env:GD_DIR) { $env:GD_DIR } else { 'C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash' }
$dir = if ($Crash) { "$gd\geode\crashlogs" } else { "$gd\geode\logs" }

$file = Get-ChildItem $dir -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $file) { throw "no logs in $dir" }
"== $($file.FullName)  ($($file.LastWriteTime))"

$lines = Get-Content $file.FullName
if ($Crash) {
    # Resolve every GeometryDash.exe+offset through GeometryDash.bro (offline, no Ghidra needed).
    $offs = $lines | Select-String -Pattern 'GeometryDash\.exe\s*\+\s*(?:0x)?([0-9A-Fa-f]+)' -AllMatches |
        ForEach-Object { $_.Matches } | ForEach-Object { '0x' + $_.Groups[1].Value } | Select-Object -Unique
    $lines
    if ($offs) {
        "`n== GeometryDash.exe offsets resolved via GeometryDash.bro (nearest function at or below):"
        python "$PSScriptRoot\gdtools.py" sym @offs
    }
    return
}
if (-not $All) { $lines = $lines | Where-Object { $_ -match $Pattern } }
$lines | Select-Object -Last $Tail
