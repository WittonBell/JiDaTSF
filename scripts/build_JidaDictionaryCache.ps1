[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

function Remove-EntryPrefix([string]$Value) {
    $start = 0
    while ($start -lt $Value.Length -and
           ($Value[$start] -eq '~' -or $Value[$start] -eq '^' -or
            $Value[$start] -eq '!')) {
        $start++
    }
    return $Value.Substring($start)
}

$resolvedInput = (Resolve-Path -LiteralPath $InputPath).Path
$sourceInfo = Get-Item -LiteralPath $resolvedInput
$lines = [System.IO.File]::ReadAllLines($resolvedInput)
$isJiDa = $false
foreach ($line in $lines) {
    if ($line.Contains('[Text]')) {
        $isJiDa = $true
        break
    }
}

$entries = New-Object 'System.Collections.Generic.List[object]'
$wildcardChar = [char]'z'
$inText = $false
$inDescription = $false

foreach ($originalLine in $lines) {
    $line = $originalLine.TrimEnd([char[]]@(' ', "`t", "`r", "`n"))
    if ([string]::IsNullOrEmpty($line) -or $line.StartsWith('------')) {
        continue
    }

    if ($isJiDa) {
        if ($line -eq '[Description]') {
            $inDescription = $true
            $inText = $false
            continue
        }
        if ($line -eq '[rule]') {
            $inDescription = $false
            $inText = $false
            continue
        }
        if ($line -eq '[Text]') {
            $inDescription = $false
            $inText = $true
            continue
        }
        if ($line.StartsWith('[')) {
            $inDescription = $false
            $inText = $false
            continue
        }
        if ($inDescription) {
            if ($line.StartsWith('WildChar=') -and $line.Length -gt 9) {
                $candidate = [char]::ToLowerInvariant($line[9])
                if ($candidate -ge 'a' -and $candidate -le 'z') {
                    $wildcardChar = $candidate
                }
            }
            continue
        }
        if (-not $inText) {
            continue
        }

        $tokens = [System.Text.RegularExpressions.Regex]::Matches($line, '[^ \t]+')
        if ($tokens.Count -lt 2) {
            continue
        }
        $code = (Remove-EntryPrefix $tokens[0].Value).ToLowerInvariant()
        if ($code.Length -eq 0 -or $code.Length -gt 4 -or
            $code -notmatch '^[a-z]+$') {
            continue
        }
        for ($i = 1; $i -lt $tokens.Count; $i++) {
            $word = Remove-EntryPrefix $tokens[$i].Value
            if ($word.Length -gt 0) {
                $entries.Add([pscustomobject]@{ Code = $code; Word = $word }) |
                    Out-Null
            }
        }
    } else {
        if ($line[0] -eq '#') {
            continue
        }
        $tab = $line.IndexOf([char]9)
        if ($tab -lt 0) {
            continue
        }
        $code = $line.Substring(0, $tab).ToLowerInvariant()
        $word = $line.Substring($tab + 1)
        if ($word.Length -eq 0 -or $code.Length -eq 0 -or
            $code.Length -gt 4) {
            continue
        }
        $entries.Add([pscustomobject]@{ Code = $code; Word = $word }) |
            Out-Null
    }
}

$encoding = [System.Text.Encoding]::Unicode
$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
$temporaryPath = "$OutputPath.tmp"
$stream = $null
$writer = $null
try {
    $stream = [System.IO.File]::Open(
        $temporaryPath,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None)
    $writer = New-Object System.IO.BinaryWriter($stream)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('JDTC'))
    $writer.Write([uint32]1)
    $writer.Write([uint64]$sourceInfo.Length)
    $writer.Write([uint64]$sourceInfo.LastWriteTimeUtc.ToFileTime())
    $writer.Write([uint16][int]$wildcardChar)
    $writer.Write([uint32]$entries.Count)
    foreach ($entry in $entries) {
        $writer.Write([uint32]$entry.Code.Length)
        $writer.Write([uint32]$entry.Word.Length)
        $writer.Write($encoding.GetBytes($entry.Code))
        $writer.Write($encoding.GetBytes($entry.Word))
    }
    $writer.Flush()
} finally {
    if ($writer) {
        $writer.Dispose()
    } elseif ($stream) {
        $stream.Dispose()
    }
}

Move-Item -LiteralPath $temporaryPath -Destination $OutputPath -Force
Write-Host "Generated dictionary cache: $OutputPath ($($entries.Count) entries)"
