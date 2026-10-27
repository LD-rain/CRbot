# 构建并打包 Windows 预发布版
#
# 用法：
#   pwsh -File tools\package.ps1                      # 自己构建（RelWithDebInfo）并打包
#   pwsh -File tools\package.ps1 -Config Release      # 指定配置
#   pwsh -File tools\package.ps1 -SkipBuild -BuildDir ..\cmake-build-release
#                                                     # 用已经构建好的目录，只打包
#
# 产物：dist\CRBot-<版本>-win64-setup.exe 及 .sha256
# 需要 MSVC 和 Inno Setup 6.3+（ISCC.exe）；支持 -IsccPath 指定编译器。

param(
    [string]$Config = "RelWithDebInfo",
    [string]$BuildDir = "",
    [ValidatePattern('^(AUTO|[1-9][0-9]*)$')]
    [string]$BuildNumber = "AUTO",
    [ValidateSet('prerelease', 'stable')]
    [string]$Channel = "prerelease",
    [string]$IsccPath = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root "build-release" }
$distDir = Join-Path $root "dist"
$BuildDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)

if (-not $IsccPath) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { $IsccPath = $command.Source }
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $IsccPath -and $base) {
            $candidate = Join-Path $base "Inno Setup 6\ISCC.exe"
            if (Test-Path $candidate) { $IsccPath = $candidate }
        }
    }
}
if (-not $IsccPath -or -not (Test-Path $IsccPath)) {
    throw "找不到 ISCC.exe，请安装 Inno Setup 6.3+ 或传入 -IsccPath"
}

Write-Host "==> CRBot ($Config)"
Write-Host "    build dir: $BuildDir"

if (-not $SkipBuild) {
    # ---- 找 cmake / ninja ----
    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if (-not $cmake) {
        $cmake = Join-Path $env:LOCALAPPDATA "Programs\CLion\bin\cmake\win\x64\bin\cmake.exe"
    }
    if (-not (Test-Path $cmake)) {
        throw "找不到 cmake。装一个 CMake 或者把它加进 PATH。"
    }
    $ninjaDir = Join-Path $env:LOCALAPPDATA "Programs\CLion\bin\ninja\win\x64"
    if (Test-Path (Join-Path $ninjaDir "ninja.exe")) {
        $env:PATH = "$ninjaDir;$env:PATH"
    }

    # ---- 找 MSVC 环境 ----
    $vcvars = $null
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installation) { $vcvars = Join-Path $installation "VC\Auxiliary\Build\vcvars64.bat" }
    }
    if (-not $vcvars -or -not (Test-Path $vcvars)) { $vcvars = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\17\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\17\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1 }

    if (-not $vcvars) {
        throw "找不到 vcvars64.bat，请先安装 Visual Studio（C++ 桌面开发）。"
    }
    Write-Host "==> MSVC: $vcvars"

    $cmd = "call `"$vcvars`" >nul && `"$cmake`" -S `"$root`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=$Config -DCRBOT_BUILD_NUMBER=$BuildNumber -DCRBOT_RELEASE_CHANNEL=$Channel && `"$cmake`" --build `"$BuildDir`""
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "构建失败（exit $LASTEXITCODE）" }
} else {
    Write-Host "==> 跳过构建，直接打包已有产物"
}

# ---- 打包 ----
$metadataPath = Join-Path $BuildDir "build-info.json"
if (-not (Test-Path $metadataPath)) { throw "没有构建元数据 $metadataPath，请先重新构建" }
$metadata = Get-Content $metadataPath -Raw | ConvertFrom-Json
$version = $metadata.version
if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' -or
    "$($metadata.build_number)" -notmatch '^[1-9][0-9]*$') { throw "非法版本元数据" }
if ($metadata.configuration -ne $Config) { throw "构建配置为 $($metadata.configuration)，请传入对应的 -Config" }
switch ($metadata.channel) {
    'prerelease' { $expectedVersion = "$version($($metadata.build_number))" }
    'stable' { $expectedVersion = $version }
    default { throw "非法发布通道" }
}
if ($metadata.display_version -ne $expectedVersion) { throw "构建版本不一致" }
$pkgName = "CRBot-$expectedVersion-win64"
$pkgDir = Join-Path $BuildDir "installer-stage\$pkgName"
$exe = Join-Path $BuildDir "CRbot.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $BuildDir "$Config/CRbot.exe" }
if (-not (Test-Path $exe)) { throw "没有找到 $exe（先构建，或用 -BuildDir 指到构建目录）" }

if (Test-Path $pkgDir) { Remove-Item $pkgDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $pkgDir | Out-Null

Copy-Item $exe $pkgDir

# 程序按 <exe>\assets\templates 找模板，所以要把 templates 放进 assets\ 下面
$pkgAssets = Join-Path $pkgDir "assets"
New-Item -ItemType Directory -Force -Path $pkgAssets | Out-Null
Copy-Item (Join-Path $root "assets\templates") $pkgAssets -Recurse
Copy-Item (Join-Path $root "assets\README.md") $pkgAssets
Copy-Item (Join-Path $root "README.md"), (Join-Path $root "CHANGELOG.md"), $metadataPath $pkgDir
[System.IO.File]::WriteAllText((Join-Path $pkgDir "VERSION"), "$version`n", [System.Text.UTF8Encoding]::new($false))
# GPL-3.0 要求分发二进制时随附许可证与第三方声明
Copy-Item (Join-Path $root "LICENSE"), (Join-Path $root "THIRD_PARTY_NOTICES.md") $pkgDir
New-Item -ItemType Directory -Force -Path (Join-Path $pkgDir "docs") | Out-Null
Copy-Item (Join-Path $root "docs\圣水识别方案.md") (Join-Path $pkgDir "docs")

$installerName = "$pkgName-setup"
$installer = Join-Path $distDir "$installerName.exe"
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
if (Test-Path $installer) { Remove-Item $installer -Force }
if (Test-Path "$installer.sha256") { Remove-Item "$installer.sha256" -Force }
& $IsccPath "/DPayloadDir=$pkgDir" "/DAppVersion=$version" "/DDisplayVersion=$expectedVersion" `
    "/DInstallerDir=$distDir" "/DInstallerName=$installerName" (Join-Path $root "packaging\windows\CRBot.iss")
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $installer)) { throw "Inno Setup 编译失败" }
$hash = (Get-FileHash -Algorithm SHA256 $installer).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText("$installer.sha256", "$hash  $installerName.exe`n", [System.Text.UTF8Encoding]::new($false))

Write-Host ""
Write-Host "==> 完成"
Write-Host "    安装程序: $installer"
