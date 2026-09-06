[CmdletBinding()]
param(
    [string]$DocsSource = (Join-Path $PSScriptRoot '..\docs'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\docs\offline'),
    [string]$MarkdownItPath = ''
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Get-RelativeUrl([string]$FromRelativePath, [string]$ToRelativePath) {
    $fromPath = Join-Path $script:OutputRoot ( $FromRelativePath -replace '/', '\' )
    $toPath = Join-Path $script:OutputRoot ( $ToRelativePath -replace '/', '\' )
    $fromDirectory = Split-Path -Parent $fromPath
    return ([System.IO.Path]::GetRelativePath($fromDirectory, $toPath) -replace '\\', '/')
}

function Escape-Html([string]$Value) {
    return [System.Net.WebUtility]::HtmlEncode($Value)
}

function Resolve-SourceRelativePath([string]$CurrentSourceRelativePath, [string]$LinkPath) {
    $currentSourcePath = Join-Path $script:SourceRoot ($CurrentSourceRelativePath -replace '/', '\')
    $currentDirectory = Split-Path -Parent $currentSourcePath
    $candidate = Join-Path $currentDirectory ($LinkPath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        return $null
    }
    return ([System.IO.Path]::GetRelativePath($script:SourceRoot, (Get-FullPath $candidate)) -replace '\\', '/')
}

function Rewrite-DocumentLinks(
    [string]$Content,
    [string]$CurrentSourceRelativePath,
    [string]$CurrentOutputRelativePath,
    [hashtable]$OutputBySource
) {
    $pattern = '(?<open>\]\()(?<path>[^)\s#]+\.md)(?<anchor>#[^)\s]*)?(?<tail>\s+"[^"]*")?(?<close>\))'
    return [System.Text.RegularExpressions.Regex]::Replace($Content, $pattern, {
        param($Match)

        $resolvedSource = Resolve-SourceRelativePath $CurrentSourceRelativePath $Match.Groups['path'].Value
        if ($null -eq $resolvedSource -or -not $OutputBySource.ContainsKey($resolvedSource)) {
            return $Match.Value
        }

        $targetOutput = $OutputBySource[$resolvedSource]
        $relativeUrl = Get-RelativeUrl $CurrentOutputRelativePath $targetOutput
        return $Match.Groups['open'].Value + $relativeUrl +
            $Match.Groups['anchor'].Value + $Match.Groups['tail'].Value + $Match.Groups['close'].Value
    })
}

function Invoke-MarkdownIt([string]$MarkdownPath) {
    $outputPath = Join-Path ([System.IO.Path]::GetTempPath()) ("csf-html-" + [System.Guid]::NewGuid().ToString('N') + '.html')
    $errorPath = Join-Path ([System.IO.Path]::GetTempPath()) ("csf-html-" + [System.Guid]::NewGuid().ToString('N') + '.err')
    $previousEncoding = $env:PYTHONIOENCODING
    try {
        $env:PYTHONIOENCODING = 'utf-8'
        $process = Start-Process -FilePath $script:MarkdownItPath `
            -ArgumentList @($MarkdownPath) `
            -RedirectStandardOutput $outputPath `
            -RedirectStandardError $errorPath `
            -NoNewWindow -PassThru -Wait
        if ($process.ExitCode -ne 0) {
            $errorText = if (Test-Path -LiteralPath $errorPath) {
                [System.IO.File]::ReadAllText($errorPath)
            } else {
                ''
            }
            throw "markdown-it failed for $MarkdownPath with exit code $($process.ExitCode). $errorText"
        }
        return [System.IO.File]::ReadAllText($outputPath, [System.Text.UTF8Encoding]::new($false))
    }
    finally {
        if ($null -eq $previousEncoding) {
            Remove-Item Env:PYTHONIOENCODING -ErrorAction SilentlyContinue
        } else {
            $env:PYTHONIOENCODING = $previousEncoding
        }
        Remove-Item -LiteralPath $outputPath, $errorPath -Force -ErrorAction SilentlyContinue
    }
}

function New-Page(
    [string]$OutputRelativePath,
    [string]$Title,
    [string]$Language,
    [string]$Body,
    [string]$LanguageSwitchTarget
) {
    $homeTarget = if ($Language -eq 'zh-CN') { 'README_CN.html' } else { 'README_EN.html' }
    $guideTarget = if ($Language -eq 'zh-CN') { 'zh-CN/config-guide.html' } else { 'en-US/config-guide.html' }
    $homeUrl = Get-RelativeUrl $OutputRelativePath $homeTarget
    $guideUrl = Get-RelativeUrl $OutputRelativePath $guideTarget
    $switchUrl = Get-RelativeUrl $OutputRelativePath $LanguageSwitchTarget
    $homeText = if ($Language -eq 'zh-CN') { '文档首页' } else { 'Documentation Home' }
    $guideText = if ($Language -eq 'zh-CN') { '配置总指南' } else { 'Configuration Guide' }
    $switchText = if ($Language -eq 'zh-CN') { 'English' } else { '中文'
    }
    $langAttribute = if ($Language -eq 'zh-CN') { 'zh-CN' } else { 'en'
    }

    return @"
<!doctype html>
<html lang="$langAttribute">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>$(Escape-Html $Title)</title>
  <style>
    :root { color-scheme: light; font-family: "Segoe UI", "Microsoft YaHei", sans-serif; line-height: 1.65; }
    body { max-width: 980px; margin: 0 auto; padding: 2rem 1.25rem 4rem; color: #202124; background: #f7f8fa; }
    header { display: flex; flex-wrap: wrap; gap: .75rem 1.25rem; align-items: center; margin-bottom: 1.5rem; padding-bottom: .9rem; border-bottom: 1px solid #d9dde3; }
    header strong { margin-right: auto; font-size: 1.1rem; }
    header a { color: #1558a6; text-decoration: none; }
    header a:hover { text-decoration: underline; }
    main { padding: 1.5rem 1.75rem; background: #fff; border: 1px solid #e2e5e9; border-radius: 10px; box-shadow: 0 4px 18px rgba(32, 33, 36, .06); }
    h1, h2, h3 { line-height: 1.25; color: #172b4d; }
    h1 { margin-top: 0; padding-bottom: .65rem; border-bottom: 2px solid #d7e3f4; }
    a { color: #1558a6; }
    code { padding: .12em .3em; border-radius: 4px; background: #eef1f5; font-family: Consolas, "Courier New", monospace; }
    pre { overflow-x: auto; padding: 1rem; border-radius: 7px; background: #1f2937; color: #f3f4f6; }
    pre code { padding: 0; background: transparent; color: inherit; }
    table { width: 100%; border-collapse: collapse; margin: 1rem 0; }
    th, td { padding: .55rem .7rem; border: 1px solid #d9dde3; text-align: left; vertical-align: top; }
    th { background: #eef4fb; }
    blockquote { margin-left: 0; padding: .5rem 1rem; border-left: 4px solid #9bbce2; background: #f3f7fc; }
    footer { margin-top: 1.25rem; color: #697586; font-size: .9rem; }
  </style>
</head>
<body>
  <header>
    <strong>Condition System Framework</strong>
    <a href="$homeUrl">$homeText</a>
    <a href="$guideUrl">$guideText</a>
    <a href="$switchUrl">$switchText</a>
  </header>
  <main>
$Body
  </main>
  <footer>Offline documentation. The Markdown source remains available in the <code>markdown</code> directory.</footer>
</body>
</html>
"@
}

$script:SourceRoot = Get-FullPath $DocsSource
$script:OutputRoot = Get-FullPath $OutputRoot

if (-not $MarkdownItPath) {
    $command = Get-Command markdown-it -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        throw 'markdown-it was not found in PATH.'
    }
    $MarkdownItPath = $command.Source
}
$script:MarkdownItPath = $MarkdownItPath

$languages = @(
    [pscustomobject]@{
        Id = 'zh-CN'
        RootEntry = 'README_CN.html'
        GuideSource = 'Config_Guide_CN.md'
        GuideOutput = 'config-guide.html'
        AuthorSource = 'Classification_Repair_Author_Guide_CN.md'
        AuthorOutput = 'classification-repair-author-guide.html'
        EntryTitle = 'Condition System Framework 中文离线文档'
        GuideTitle = 'Condition System Framework 配置文档'
        AuthorTitle = '分类与维修作者指南'
        EntryIntro = '这是 CSF 的中文离线文档入口。所有链接都指向本地 HTML 文件，不需要访问 GitHub。'
        GuideLabel = '完整配置指南'
        AuthorLabel = '分类与维修作者指南'
        Modules = @(
            @{ Source = 'config/00_配置总览_CN.md'; Output = 'config/00-overview.html'; Label = '配置总览' }
            @{ Source = 'config/01_分类_CN.md'; Output = 'config/01-classifications.html'; Label = '分类模块' }
            @{ Source = 'config/02_耐久消耗_CN.md'; Output = 'config/02-durability-consumption.html'; Label = '耐久消耗模块' }
            @{ Source = 'config/03_伤害公式_CN.md'; Output = 'config/03-damage-formula.html'; Label = '伤害公式模块' }
            @{ Source = 'config/04_维修_CN.md'; Output = 'config/04-repair.html'; Label = '维修模块' }
            @{ Source = 'config/05_战利品_CN.md'; Output = 'config/05-loot.html'; Label = '战利品模块' }
            @{ Source = 'config/06_物品卡片_CN.md'; Output = 'config/06-item-cards.html'; Label = '物品卡片模块' }
            @{ Source = 'config/07_Provider选择_CN.md'; Output = 'config/07-provider-selection.html'; Label = 'Provider 选择与加载' }
            @{ Source = 'config/08_MCM与INI_CN.md'; Output = 'config/08-mcm-and-ini.html'; Label = 'MCM 与 INI 模块' }
        )
    }
    [pscustomobject]@{
        Id = 'en-US'
        RootEntry = 'README_EN.html'
        GuideSource = 'Config_Guide_EN.md'
        GuideOutput = 'config-guide.html'
        AuthorSource = 'Classification_Repair_Author_Guide_EN.md'
        AuthorOutput = 'classification-repair-author-guide.html'
        EntryTitle = 'Condition System Framework English Offline Documentation'
        GuideTitle = 'Condition System Framework Configuration Docs'
        AuthorTitle = 'Classification and Repair Author Guide'
        EntryIntro = 'This is the offline documentation entry point for CSF. All links target local HTML files and work without GitHub access.'
        GuideLabel = 'Complete configuration guide'
        AuthorLabel = 'Classification and repair author guide'
        Modules = @(
            @{ Source = 'config/00_Configuration_Overview_EN.md'; Output = 'config/00-overview.html'; Label = 'Configuration Overview' }
            @{ Source = 'config/01_Classifications_EN.md'; Output = 'config/01-classifications.html'; Label = 'Classifications' }
            @{ Source = 'config/02_Durability_Consumption_EN.md'; Output = 'config/02-durability-consumption.html'; Label = 'Durability Consumption' }
            @{ Source = 'config/03_Damage_Formula_EN.md'; Output = 'config/03-damage-formula.html'; Label = 'Damage Formula' }
            @{ Source = 'config/04_Repair_EN.md'; Output = 'config/04-repair.html'; Label = 'Repair' }
            @{ Source = 'config/05_Loot_EN.md'; Output = 'config/05-loot.html'; Label = 'Loot' }
            @{ Source = 'config/06_Item_Cards_EN.md'; Output = 'config/06-item-cards.html'; Label = 'Item Cards' }
            @{ Source = 'config/07_Provider_Selection_EN.md'; Output = 'config/07-provider-selection.html'; Label = 'Provider Selection' }
            @{ Source = 'config/08_MCM_and_INI_EN.md'; Output = 'config/08-mcm-and-ini.html'; Label = 'MCM and INI' }
        )
    }
)

Remove-Item -LiteralPath $script:OutputRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $script:OutputRoot | Out-Null

$allSources = @()
foreach ($language in $languages) {
    $allSources += $language.GuideSource
    $allSources += $language.AuthorSource
    $allSources += @($language.Modules | ForEach-Object { $_.Source })
}

foreach ($language in $languages) {
    $languageRoot = Join-Path $script:OutputRoot $language.Id
    New-Item -ItemType Directory -Force -Path (Join-Path $languageRoot 'config') | Out-Null
    $outputBySource = @{}
    $outputBySource[$language.GuideSource] = "$($language.Id)/$($language.GuideOutput)"
    $outputBySource[$language.AuthorSource] = "$($language.Id)/$($language.AuthorOutput)"
    foreach ($module in $language.Modules) {
        $outputBySource[$module.Source] = "$($language.Id)/$($module.Output)"
    }

    $pages = @(
        @{ Source = $language.GuideSource; Output = "$($language.Id)/$($language.GuideOutput)"; Title = $language.GuideTitle }
        @{ Source = $language.AuthorSource; Output = "$($language.Id)/$($language.AuthorOutput)"; Title = $language.AuthorTitle }
    )
    foreach ($module in $language.Modules) {
        $pages += @{ Source = $module.Source; Output = "$($language.Id)/$($module.Output)"; Title = $module.Label }
    }

    foreach ($page in $pages) {
        $sourcePath = Join-Path $script:SourceRoot ($page.Source -replace '/', '\')
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "Missing documentation source: $sourcePath"
        }
        $content = [System.IO.File]::ReadAllText($sourcePath)
        $content = Rewrite-DocumentLinks $content $page.Source $page.Output $outputBySource
        $tempPath = Join-Path ([System.IO.Path]::GetTempPath()) ("csf-doc-" + [System.Guid]::NewGuid().ToString('N') + '.md')
        try {
            Write-Utf8NoBom $tempPath $content
            $body = Invoke-MarkdownIt $tempPath
        }
        finally {
            Remove-Item -LiteralPath $tempPath -Force -ErrorAction SilentlyContinue
        }
        $switchOutput = if ($language.Id -eq 'zh-CN') {
            $page.Output -replace '^zh-CN/', 'en-US/'
        } else {
            $page.Output -replace '^en-US/', 'zh-CN/'
        }
        $html = New-Page $page.Output $page.Title $language.Id $body $switchOutput
        Write-Utf8NoBom (Join-Path $script:OutputRoot ($page.Output -replace '/', '\')) $html
    }

    $entryLinks = @(
        "# $($language.EntryTitle)",
        '',
        $language.EntryIntro,
        '',
        "- [$($language.GuideLabel)]($($language.Id)/$($language.GuideOutput))",
        "- [$($language.AuthorLabel)]($($language.Id)/$($language.AuthorOutput))",
        '',
        $(if ($language.Id -eq 'zh-CN') { '## 配置模块' } else { '## Configuration Modules' })
    )
    foreach ($module in $language.Modules) {
        $entryLinks += "- [$($module.Label)]($($language.Id)/$($module.Output))"
    }
    $entryMarkdown = $entryLinks -join [Environment]::NewLine
    $entryTemp = Join-Path ([System.IO.Path]::GetTempPath()) ("csf-entry-" + [System.Guid]::NewGuid().ToString('N') + '.md')
    try {
        Write-Utf8NoBom $entryTemp $entryMarkdown
        $entryBody = Invoke-MarkdownIt $entryTemp
    }
    finally {
        Remove-Item -LiteralPath $entryTemp -Force -ErrorAction SilentlyContinue
    }
    $entrySwitch = if ($language.Id -eq 'zh-CN') { 'README_EN.html' } else { 'README_CN.html' }
    $entryHtml = New-Page $language.RootEntry $language.EntryTitle $language.Id $entryBody $entrySwitch
    Write-Utf8NoBom (Join-Path $script:OutputRoot $language.RootEntry) $entryHtml

    $markdownRoot = Join-Path $script:OutputRoot (Join-Path 'markdown' $language.Id)
    foreach ($sourceRelativePath in @($language.GuideSource, $language.AuthorSource) + @($language.Modules | ForEach-Object { $_.Source })) {
        $sourcePath = Join-Path $script:SourceRoot ($sourceRelativePath -replace '/', '\')
        $destination = Join-Path $markdownRoot ($sourceRelativePath -replace '/', '\')
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destination -Force
    }
}

Write-Output "Generated offline documentation at $script:OutputRoot"
