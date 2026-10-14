<#
.SYNOPSIS
  Configure (if needed), build, and install Pathfinder into GD. Prints only what matters.

.EXAMPLE
  tools\build.ps1                 # Debug (DEBUG_MODE=1) - the default for development
  tools\build.ps1 RelWithDebInfo  # optimised, still has a PDB
  tools\build.ps1 Debug -Reconfigure

.NOTES
  Build output: build\<Config>\pathfinder.dll, build\gd-sim\<Config>\gd-sim-test.exe.
  setup_geode_mod packages build\tobi437a.pathfinder.geode and installs it into the
  `geode profile` mods folder. Whichever config was built last is the one GD loads.
  GD must be closed for the new build to load (it only reads mods at startup).
#>
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')][string]$Config = 'Debug',
    [switch]$Reconfigure
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path "$PSScriptRoot\.."

if (-not $env:GEODE_SDK) {
    $env:GEODE_SDK = [Environment]::GetEnvironmentVariable('GEODE_SDK', 'User')
    if (-not $env:GEODE_SDK) { throw 'GEODE_SDK is not set (expected C:\Users\tobi4\Documents\Geode)' }
}

if (Get-Process GeometryDash -ErrorAction SilentlyContinue) {
    Write-Warning 'GeometryDash is running - the install step may fail, and GD will not pick up the new build until restarted.'
}

$ErrorActionPreference = 'Continue'  # PS 5.1 turns native stderr into errors under 'Stop'
$cache = "$repo\build\CMakeCache.txt"
if ($Reconfigure -or -not (Test-Path $cache) -or
    -not (Select-String -Quiet -Path $cache -Pattern '^PATHFINDER_DEV_TOOLS:BOOL=ON')) {
    # ClangCL is required: the CMakeLists pass clang-style flags, and Geode's CI builds with clang-cl.
    # PATHFINDER_DEV_TOOLS: debug overlay + verifier in the mod, gd-sim-test, pathfinder-cli.
    cmake -S $repo -B "$repo\build" -G 'Visual Studio 18 2026' -A x64 -T ClangCL -DPATHFINDER_DEV_TOOLS=ON 2>&1 |
        Where-Object { "$_" -match 'Error|error|Configuring done|Generating done' } | ForEach-Object { "$_" }
    if ($LASTEXITCODE) { throw 'CMake configure failed' }
}

$out = cmake --build "$repo\build" --config $Config -- /m /v:minimal 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'

# Our own diagnostics only: deps (Geode headers, arc, CPM packages) are noisy and not ours to fix.
$out | Where-Object {
    ($_ -match ': (fatal )?error' -or $_ -match ': warning' -or $_ -match 'error MSB|LNK\d+') -and
    $_ -notmatch '\\_deps\\|\\Geode\\loader\\|\\bindings\\'
} | ForEach-Object { ($_ -replace [regex]::Escape("$repo\"), '') -replace '\s*\[[^\]]+\.vcxproj\]$', '' } |
    Select-Object -Unique
$out | Where-Object { $_ -match '\| (Done|Error|Warn) \|' } | ForEach-Object { $_.Trim() }

if ($code) { throw "Build failed ($Config), exit $code" }
"Build OK ($Config)"
