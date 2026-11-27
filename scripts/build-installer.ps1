[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$xmakeCommand = Get-Command xmake.exe -ErrorAction SilentlyContinue
if (-not $xmakeCommand) {
    throw 'xmake was not found. Install xmake and ensure xmake.exe is on PATH.'
}

$makensisCommand = Get-Command makensis.exe -ErrorAction SilentlyContinue
if (-not $makensisCommand) {
    $programFilesRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    foreach ($programFilesRoot in $programFilesRoots) {
        $candidate = Join-Path $programFilesRoot 'NSIS\makensis.exe'
        if (Test-Path -LiteralPath $candidate) {
            $makensisPath = $candidate
            break
        }
    }
    if (-not $makensisPath) {
        throw 'makensis was not found. Install NSIS 3 and ensure makensis.exe is on PATH.'
    }
} else {
    $makensisPath = $makensisCommand.Source
}

Push-Location $root
try {
    & $xmakeCommand.Source f -p windows -a $Platform -m release --toolchain=msvc -y
    if ($LASTEXITCODE -ne 0) {
        throw "xmake configuration failed with exit code $LASTEXITCODE."
    }

    & $xmakeCommand.Source build JiDaTSF
    if ($LASTEXITCODE -ne 0) {
        throw "xmake build failed with exit code $LASTEXITCODE."
    }

    $buildDir = Join-Path $root "bin\$Platform\release"
    $dllPath = Join-Path $buildDir 'JiDaTSF.dll'
    $dictionaryPath = Join-Path $root 'data\jidian_wubi86.txt'
    if (-not (Test-Path -LiteralPath $dllPath)) {
        throw "Build output was not found: $dllPath"
    }
    if (-not (Test-Path -LiteralPath $dictionaryPath)) {
        throw "Dictionary file was not found: $dictionaryPath"
    }

    $stageDir = Join-Path $root "dist\stage\$Platform"
    $stageDataDir = Join-Path $stageDir 'data'
    New-Item -ItemType Directory -Path $stageDataDir -Force | Out-Null
    Copy-Item -LiteralPath $dllPath -Destination (Join-Path $stageDir 'JiDaTSF.dll') -Force
    Copy-Item -LiteralPath $dictionaryPath -Destination $stageDataDir -Force

    $cacheScript = Join-Path $PSScriptRoot 'build_JidaDictionaryCache.ps1'
    $stagedDictionary = Join-Path $stageDataDir 'jidian_wubi86.txt'
    $stagedCache = "$stagedDictionary.jdc"
    & $cacheScript -InputPath $stagedDictionary -OutputPath $stagedCache
    if (-not (Test-Path -LiteralPath $stagedCache)) {
        throw 'Dictionary cache generation did not produce the expected .jdc file.'
    }

    $installerPath = Join-Path $root "dist\JiDaTSF-Setup-$Platform.exe"
    New-Item -ItemType Directory -Path (Split-Path -Parent $installerPath) -Force |
        Out-Null
    $installerScript = Join-Path $PSScriptRoot 'JiDaTSF.nsi'
    & $makensisPath /INPUTCHARSET UTF8 "/DPLATFORM=$Platform" "/DSTAGE_DIR=$stageDir" `
        "/DOUTPUT_FILE=$installerPath" $installerScript
    if ($LASTEXITCODE -ne 0) {
        throw "makensis failed with exit code $LASTEXITCODE."
    }

    Write-Host "Installer created: $installerPath"
} finally {
    Pop-Location
}
