param(
    [ValidateSet('debug', 'releasedbg')]
    [string]$Configuration = 'releasedbg',
    [string]$CommonLibF4Path = '',
    [switch]$SkipDependencyBootstrap
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($CommonLibF4Path)) {
    $CommonLibF4Path = [Environment]::GetEnvironmentVariable('COMMONLIBF4_PATH', 'Process')
}

if ([string]::IsNullOrWhiteSpace($CommonLibF4Path)) {
    $workspaceRoot = $projectRoot
    for ($i = 0; $i -lt 8 -and -not (Test-Path -LiteralPath (Join-Path $workspaceRoot 'workspace.json')); $i++) {
        $parent = Split-Path -Parent $workspaceRoot
        if ([string]::IsNullOrWhiteSpace($parent) -or $parent -eq $workspaceRoot) {
            $workspaceRoot = $null
            break
        }
        $workspaceRoot = $parent
    }

    if ($null -ne $workspaceRoot) {
        $workspace = Get-Content -LiteralPath (Join-Path $workspaceRoot 'workspace.json') -Raw | ConvertFrom-Json
        $profileName = [string]$workspace.defaultProfiles.commonlibf4
        $profileProperty = $workspace.cppToolchains.PSObject.Properties[$profileName]
        if ($null -eq $profileProperty) {
            throw "Workspace CommonLibF4 profile is not defined: $profileName"
        }
        $CommonLibF4Path = Join-Path $workspaceRoot ([string]$profileProperty.Value.path)
    }
}

if ([string]::IsNullOrWhiteSpace($CommonLibF4Path)) {
    throw 'Set COMMONLIBF4_PATH, pass -CommonLibF4Path, or run this script inside the configured workspace.'
}

$CommonLibF4Path = [System.IO.Path]::GetFullPath($CommonLibF4Path)
if (-not (Test-Path -LiteralPath (Join-Path $CommonLibF4Path 'xmake.lua'))) {
    throw "CommonLibF4 checkout was not found at $CommonLibF4Path"
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
