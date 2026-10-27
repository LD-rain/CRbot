# On a disposable Windows runner: install, reinstall, verify, then uninstall.
param(
    [string]$DistDir = "dist",
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][int]$BuildNumber
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$installer = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath((Join-Path $DistDir "CRBot-$Version-win64-setup.exe"))
$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{D3B735E8-1F87-4C97-9C62-87439286061A}_is1'
if (Test-Path $key) { throw "CRBot is already installed for this user; run this test on a disposable Windows runner" }
$checksum = (Get-FileHash -Algorithm SHA256 $installer).Hash.ToLowerInvariant() + '  ' + (Split-Path -Leaf $installer)
if ((Get-Content "$installer.sha256" -Raw).Trim() -ne $checksum) { throw 'Installer SHA256 mismatch' }
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("crbot-installer-" + [guid]::NewGuid().ToString('N'))
$installDir = Join-Path $temporary "CRBot Test"
$group = "CRBot Test " + [guid]::NewGuid().ToString('N')
New-Item -ItemType Directory -Path $temporary | Out-Null
$uninstalled = $false
function Run-Setup([string]$File, [string[]]$Arguments) {
    # -Wait also waits for the temporary child process used by the uninstaller.
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Installer failed: $File (exit $($process.ExitCode))" }
}
try {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/TASKS=',
        ('/DIR="{0}"' -f $installDir), ('/GROUP="{0}"' -f $group), ('/LOG="{0}\install.log"' -f $temporary))
    Run-Setup $installer $arguments
    # Reinstall in place and preserve runtime data across upgrades/uninstall.
    $log = Join-Path $installDir "crbot.log"
    Set-Content $log 'installer smoke test: preserve this log'
    Run-Setup $installer $arguments
    if ((Get-Content $log -Raw).Trim() -ne 'installer smoke test: preserve this log') { throw 'Reinstall lost user logs' }
    python (Join-Path $root 'tools/verify_packages.py') $DistDir --version $Version --build-number $BuildNumber --platform win64 --payload-dir $installDir
    if ($LASTEXITCODE -ne 0) { throw 'Installed payload verification failed' }
    $registration = Get-ItemProperty $key
    if ($registration.DisplayVersion -ne $Version -or $registration.InstallLocation.TrimEnd('\') -ne $installDir) {
        throw 'Incorrect uninstall registration or version'
    }
    $shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) "$group\CRBot.lnk"
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($shortcut)
    if ($link.TargetPath -ne (Join-Path $installDir 'CRbot.exe') -or $link.WorkingDirectory -ne $installDir) {
        throw 'Incorrect Start menu shortcut'
    }
    Run-Setup (Join-Path $installDir 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    if ((Test-Path (Join-Path $installDir 'CRbot.exe')) -or (Test-Path $key) -or (Test-Path $shortcut)) {
        throw 'Uninstall did not remove installed files, registration, or shortcut'
    }
    if (-not (Test-Path $log)) { throw 'Uninstall removed user logs' }
    $uninstalled = $true
    Write-Host 'PASS: Windows install, reinstall, payload, Start menu, registration, and uninstall'
} finally {
    if (-not $uninstalled -and (Test-Path (Join-Path $installDir 'unins000.exe'))) {
        Run-Setup (Join-Path $installDir 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    }
    # Preserve diagnostic logs on failure; the CI runner will be discarded.
    if ($uninstalled) { Remove-Item $temporary -Recurse -Force }
    else { Write-Host "Installer diagnostics: $temporary" }
}
