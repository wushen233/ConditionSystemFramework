param(
    [ValidateSet('debug', 'releasedbg')]
    [string]$Configuration = 'releasedbg',
    [string]$CommonLibF4Path = '',
    [switch]$SkipDependencyBootstrap
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$pinnedCommonLibCommit = 'ca31eeb6c7353555973bc351c6733d6492f2c66e'

if ([string]::IsNullOrWhiteSpace($CommonLibF4Path)) {
    $CommonLibF4Path = Join-Path $projectRoot '.deps\commonlibf4'
}
$CommonLibF4Path = [System.IO.Path]::GetFullPath($CommonLibF4Path)

if (-not (Test-Path -LiteralPath (Join-Path $CommonLibF4Path 'xmake.lua'))) {
    if ($SkipDependencyBootstrap) {
        throw "CommonLibF4 was not found at $CommonLibF4Path"
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $CommonLibF4Path) -Force | Out-Null
    & git clone --recurse-submodules https://github.com/Dear-Modding-FO4/commonlibf4.git $CommonLibF4Path
    if ($LASTEXITCODE -ne 0) { throw 'Failed to clone CommonLibF4.' }

    & git -C $CommonLibF4Path checkout $pinnedCommonLibCommit
    if ($LASTEXITCODE -ne 0) { throw 'Failed to check out the pinned CommonLibF4 revision.' }

    & git -C $CommonLibF4Path submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize CommonLibF4 submodules.' }
}

$env:COMMONLIBF4_PATH = $CommonLibF4Path
Push-Location $projectRoot
try {
    & xmake f -P $projectRoot -y -m $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'XMake configuration failed.' }

    & xmake -P $projectRoot -y ConditionSystemFramework
    if ($LASTEXITCODE -ne 0) { throw 'ConditionSystemFramework build failed.' }
}
finally {
    Pop-Location
}
