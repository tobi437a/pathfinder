<#
.SYNOPSIS
  Run one of the Pathfinder Ghidra scripts headlessly against the analysed GD project.

.EXAMPLE
  tools\ghidra\ghidra.ps1 decomp slope.c PlayerObject::collidedWithSlopeInternal 0x38d580
  tools\ghidra\ghidra.ps1 decomp jump.asm asm:PlayerObject::updateJump
  tools\ghidra\ghidra.ps1 xrefs callers.txt PlayerObject::flipGravity
  tools\ghidra\ghidra.ps1 find hits.txt "PlayerObject::.*slope.*" "GJBaseGameLayer::collision"
  tools\ghidra\ghidra.ps1 import-bro bro.log      # name functions from GeometryDash.bro (writes!)

.NOTES
  Output lands in build\re\<out>. Each run costs ~20-40s of Ghidra startup, so batch targets.
  Every command except import-bro opens the project read-only. Ghidra's GUI must not have
  the project open (it holds a lock); close the project or the whole GUI first.
  Override locations with $env:GHIDRA_HOME / $env:GD_GHIDRA_PROJECT (path without .gpr).
#>
param(
    [Parameter(Mandatory)][ValidateSet('decomp', 'xrefs', 'find', 'import-bro')][string]$Command,
    [Parameter(Mandatory)][string]$Out,
    [Parameter(ValueFromRemainingArguments)][string[]]$Targets
)
$ErrorActionPreference = 'Stop'

$ghidraHome = if ($env:GHIDRA_HOME) { $env:GHIDRA_HOME } else { 'C:\Users\tobi4\Downloads\ghidra_12.1.2_PUBLIC' }
$project = if ($env:GD_GHIDRA_PROJECT) { $env:GD_GHIDRA_PROJECT } else { 'C:\Users\tobi4\GD-2.2081' }
$projDir = Split-Path $project
$projName = Split-Path $project -Leaf

if (Test-Path "$project.lock") {
    throw "Ghidra project $projName is locked - close it in the Ghidra GUI (File > Close Project) and retry."
}

$repo = Resolve-Path "$PSScriptRoot\..\.."
$outDir = Join-Path $repo 'build\re'
New-Item -ItemType Directory -Force $outDir | Out-Null
$outFile = Join-Path $outDir $Out
$script = @{ decomp = 'Decomp.java'; xrefs = 'Xrefs.java'; find = 'Find.java'; 'import-bro' = 'ImportBroma.java' }[$Command]
$mode = @('-readOnly')
if ($Command -eq 'import-bro') {
    $mode = @()
    if (-not $Targets) {
        $gd = (Get-Content (Join-Path $repo 'mod.json') -Raw | ConvertFrom-Json).gd.win
        $Targets = @(Join-Path $repo "build\_deps\bindings-src\bindings\$gd\GeometryDash.bro")
    }
} elseif (-not $Targets) {
    throw "$Command needs at least one target"
}

Remove-Item $outFile -ErrorAction SilentlyContinue
# Ghidra prints JVM warnings on stderr; PowerShell 5.1 turns those into errors under 'Stop'.
$ErrorActionPreference = 'Continue'
$log = & "$ghidraHome\support\analyzeHeadless.bat" $projDir $projName `
    -process GeometryDash.exe -noanalysis @mode `
    -scriptPath "$PSScriptRoot\scripts" -postScript $script $outFile @Targets 2>&1 | Out-String
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $outFile)) {
    Write-Output $log
    throw "Ghidra did not produce $outFile (full log above)."
}
$log -split "`n" |
    Where-Object { $_ -match '\bERROR\b|Exception:|> wrote |ImportBroma:' -and $_ -notmatch 'NativeCommandError|CategoryInfo' } |
    ForEach-Object { $_.Trim() }
"$outFile ($((Get-Content $outFile | Measure-Object -Line).Lines) lines)"
