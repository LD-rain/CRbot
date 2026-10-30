# Offline native Windows smoke tests; no emulator or ADB server required.
param([string]$BuildDir = "build", [string]$Config = "Release")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$bin = $BuildDir
if (-not (Test-Path (Join-Path $bin "ui_probe.exe"))) { $bin = Join-Path $BuildDir $Config }
foreach ($name in @('CRbot.exe', 'ui_probe.exe', 'elixir_probe.exe')) {
    if (-not (Test-Path (Join-Path $bin $name))) { throw "Missing $name" }
}
$output = & (Join-Path $bin "ui_probe.exe") (Join-Path $root 'assets/screens/result2.png') (Join-Path $root 'assets/templates/btn_ok.png')
if ($LASTEXITCODE -ne 0 -or "$output" -notmatch 'found=1 score=1.0000') { throw "WIC / template matching smoke test failed: $output" }
& (Join-Path $bin "elixir_probe.exe") (Join-Path $root 'assets/screens') | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Elixir / hand recognition smoke test failed" }
foreach ($name in @('btn_battle.png', 'btn_battle2.png', 'btn_ok.png', 'btn_mode_1v1.png')) {
    if (-not (Test-Path (Join-Path $bin "assets/templates/$name"))) { throw "Missing template $name" }
}
Write-Host 'PASS: Windows executables, WIC decoding, template matching, recognition, template resources'
