<#
.SYNOPSIS
  Play macros in real GD through the verifier (src/verify.cpp) and summarise where GD and the
  simulator disagree. Launches GD through Steam, waits for it to finish and exit.

.EXAMPLE
  tools\verify.ps1 jobs.json
  tools\verify.ps1 jobs.json -Minimize

.NOTES
  jobs.json: {"jobs": [{"level": "77239587" | "main:3" | "<local level name>", "macro": "<path>.gdr2"}]}
  The level must be in the GD save. GD must not already be running. Results:
  %LOCALAPPDATA%\GeometryDash\geode\mods\camila314.pathfinder\verify-results.jsonl, plus
  verify\<level>\real.txt, sim.txt, level.lvl for `gdtools compare ... --shift 1`.
#>
param(
    [Parameter(Mandatory)][string]$Jobs,
    [switch]$Minimize,
    [int]$TimeoutSeconds = 3600
)
$ErrorActionPreference = 'Stop'

$saveDir = Join-Path $env:LOCALAPPDATA 'GeometryDash\geode\mods\camila314.pathfinder'
$results = Join-Path $saveDir 'verify-results.jsonl'

if (Get-Process GeometryDash -ErrorAction SilentlyContinue) { throw 'GD is already running - close it first.' }

$json = Get-Content $Jobs -Raw | ConvertFrom-Json
$json | Add-Member -NotePropertyName exit -NotePropertyValue $true -Force
# PowerShell 5.1's Set-Content -Encoding utf8 writes a BOM, which the JSON parser may reject
[IO.File]::WriteAllText((Join-Path $saveDir 'verify.json'), ($json | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding $false))
if (Test-Path $results) { Move-Item $results "$results.prev" -Force }
Remove-Item (Join-Path $saveDir 'verify.json.taken') -ErrorAction SilentlyContinue

Start-Process 'steam://rungameid/322170'
$t = 0
while (-not ($gd = Get-Process GeometryDash -ErrorAction SilentlyContinue) -and $t -lt 60) { Start-Sleep 1; $t++ }
if (-not $gd) { throw 'GD did not start.' }

if ($Minimize) {
    # Minimising pauses the director; before Pathfinder has loaded (and taken verify.json) nothing
    # stops that, and Geode's mod loading on the loading screen would never finish.
    while (-not (Test-Path (Join-Path $saveDir 'verify.json.taken')) -and -not $gd.HasExited) { Start-Sleep -Milliseconds 200 }
    Add-Type -Namespace Win32 -Name User32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr hWnd, int nCmdShow);
[DllImport("user32.dll")] public static extern bool IsIconic(System.IntPtr hWnd);
'@
    $gd.Refresh()
    [Win32.User32]::ShowWindow($gd.MainWindowHandle, 6) | Out-Null   # SW_MINIMIZE
    Start-Sleep 1
    "GD window minimised: $([Win32.User32]::IsIconic($gd.MainWindowHandle))"
}

$start = Get-Date
while (-not $gd.HasExited) {
    if (((Get-Date) - $start).TotalSeconds -gt $TimeoutSeconds) { throw "GD still running after $TimeoutSeconds s." }
    Start-Sleep 2
    $gd.Refresh()
}
"GD ran {0:N0} s" -f ((Get-Date) - $start).TotalSeconds

if (-not (Test-Path $results)) { throw "No results written ($results)." }
Get-Content $results | ForEach-Object {
    $j = $_ | ConvertFrom-Json
    if ($j.result -eq 'error') { "{0,-24} ERROR {1}" -f $j.level, $j.error; return }
    $verdict = if ($j.divergeFrame -lt 0) { 'matches sim' } else { "diverges @ {0} (real {1:N3},{2:N3} sim {3:N3},{4:N3}), max {5:N3}" -f $j.divergeFrame, $j.divergeReal[0], $j.divergeReal[1], $j.divergeSim[0], $j.divergeSim[1], $j.maxDiff }
    "{0,-24} {1,-8} frames {2,-6} sim died {3,-6} {4}  ({5:N0} steps/s)" -f $j.name, $j.result, $j.frames, $j.simDeathFrame, $verdict, $j.stepsPerSecond
}
