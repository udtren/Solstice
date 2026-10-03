# SPDX-FileCopyrightText: 2026 Solstice contributors
# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory)][ValidateSet('configure', 'build', 'package')][string]$Stage,
    [Parameter(Mandatory)][string]$Root,
    [int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$Source = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$Root = (Resolve-Path $Root).Path
$Deps = Join-Path $Root 'deps'
$CompilerBin = Join-Path $Root 'llvm-mingw-20251118-ucrt-x86_64/bin'
$Build = Join-Path $Root 'b'
$Install = Join-Path $Root 'i'
$Logs = Join-Path $Root 'logs'
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
# Retain setup-python's executable before placing dependency tools on PATH.
$Python = (Get-Command python).Source
$env:VULKAN_SDK = Join-Path $Root 'vulkan'
$env:PATH = "$CompilerBin;$Deps/bin;$env:VULKAN_SDK/Bin;$env:PATH"
$env:PKG_CONFIG_PATH = "$Deps/lib/pkgconfig;$Deps/share/pkgconfig"
$env:QT_PLUGIN_PATH = "$Deps/plugins"
$env:PYTHONUTF8 = '1'

function Invoke-Logged([string]$Program, [string[]]$Arguments, [string]$LogName) {
    & $Program @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $Logs $LogName)
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

if ($Stage -eq 'configure') {
    Invoke-Logged cmake @(
        '-S', $Source, '-B', $Build, '-G', 'Ninja',
        "-DCMAKE_C_COMPILER=$CompilerBin/clang.exe",
        "-DCMAKE_CXX_COMPILER=$CompilerBin/clang++.exe",
        "-DCMAKE_PREFIX_PATH=$Deps", "-DCMAKE_INSTALL_PREFIX=$Install",
        "-DPython_EXECUTABLE=$Python", "-DPKG_CONFIG_EXECUTABLE=$Deps/bin/pkgconf.exe",
        '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_WITH_QT6=ON', '-DALLOW_UNSTABLE=QT6',
        '-DBUILD_TESTING=OFF', '-DKRITA_ENABLE_PCH=OFF', '-DFOUNDATION_BUILD=ON',
        '-DBRANDING=Next', '-DENABLE_UPDATERS=OFF'
    ) 'configure.log'
    $Cache = Get-Content -Raw (Join-Path $Build 'CMakeCache.txt')
    if ($Cache -notmatch '(?m)^HAVE_KRITA_GPU_ENGINE:INTERNAL=ON\r?$') {
        throw 'Configuration silently disabled the Vulkan GPU engine'
    }
    $QtVersionFile = Join-Path $Deps 'lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake'
    Select-String -Path $QtVersionFile -Pattern '^set\(PACKAGE_VERSION ' |
        ForEach-Object { $_.Line } | Tee-Object -FilePath (Join-Path $Logs 'qt-version.txt')
} elseif ($Stage -eq 'build') {
    Invoke-Logged cmake @('--build', $Build, '--parallel', "$Jobs") 'build.log'
    Invoke-Logged cmake @('--install', $Build) 'install.log'
    foreach ($Required in @('bin/krita.exe', 'bin/krita.com', 'bin/krita.dll', 'bin/libkritagpu.dll')) {
        if (-not (Test-Path (Join-Path $Install $Required))) { throw "Missing output: $Required" }
    }
} else {
    $PackageDir = Join-Path $Root 'packages'
    New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null
    $env:MINGW_BIN_DIR = $CompilerBin
    $env:SEVENZIP_EXE = (Get-Command 7z).Source
    $PackageName = "Solstice-windows-x64-$($env:GITHUB_SHA.Substring(0, 12))"
    Push-Location $PackageDir
    try {
        Invoke-Logged $Python @(
            (Join-Path $Source 'packaging/windows/package-complete.py'),
            '--no-interactive', '--package-name', $PackageName,
            '--src-dir', $Source, '--deps-install-dir', $Deps,
            '--krita-install-dir', $Install
        ) 'package.log'
    } finally { Pop-Location }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'windows-dependencies.lock.json') -Destination $PackageDir
    @"
Solstice Windows x64 experimental, unsigned build
Commit: $env:GITHUB_SHA
Run: https://github.com/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID
GPU engine: compiled; GPU runtime and interactive tests were not run.
Dependency versions and source revisions: windows-dependencies.lock.json
"@ | Set-Content -Path (Join-Path $PackageDir 'BUILD-INFO.txt') -Encoding utf8
    Get-ChildItem -LiteralPath $PackageDir -Filter '*.zip' | ForEach-Object {
        $Hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$Hash  $($_.Name)"
    } | Set-Content -Path (Join-Path $PackageDir 'SHA256SUMS.txt') -Encoding ascii
}
