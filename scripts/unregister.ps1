<#
.SYNOPSIS
    卸载 JidaTSF 输入法。
.DESCRIPTION
    - 自动提升到管理员权限
    - 用 regsvr32 /u 反注册
    - 清理 CLSID、语言 profile、注册表残留
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\unregister.ps1
    powershell -ExecutionPolicy Bypass -File .\scripts\unregister.ps1 -Platform x64 -Configuration Release
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [ValidateSet('x64','x86','Auto')]
    [string]$Platform = 'Auto',
    [switch]$NoElevate
)

$ErrorActionPreference = 'Stop'

# --------------------------- 工具函数 ---------------------------

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-SystemRegsvr32 {
    if ([Environment]::Is64BitOperatingSystem) {
        $p = Join-Path $env:WINDIR 'System32\regsvr32.exe'
        if (Test-Path $p) { return $p }
    }
    return (Join-Path $env:WINDIR 'SysWOW64\regsvr32.exe')
}

# --------------------------- 提权 ---------------------------

if (-not (Test-Admin)) {
    if ($NoElevate) { throw '需要管理员权限。' }
    Write-Host '未以管理员运行，尝试自动提升权限...' -ForegroundColor Yellow
    $self = $PSCommandPath
    if (-not $self) { $self = $MyInvocation.MyCommand.Path }
    $argList = @('-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$self`"")
    if ($Configuration) { $argList += @('-Configuration', $Configuration) }
    if ($Platform)      { $argList += @('-Platform', $Platform) }
    Start-Process -FilePath (Get-Process -Id $PID).Path `
                  -ArgumentList $argList -Verb RunAs -Wait
    exit 0
}

# --------------------------- 定位 DLL ---------------------------

$root = Split-Path -Parent $PSScriptRoot
if ($Platform -eq 'Auto') {
    $Platform = if ([Environment]::Is64BitOperatingSystem) { 'x64' } else { 'x86' }
}

$dllPath = Join-Path $root (Join-Path 'bin' (Join-Path $Platform (Join-Path $Configuration 'JidaTSF.dll')))
if (-not (Test-Path $dllPath)) {
    $other = if ($Platform -eq 'x64') { 'x86' } else { 'x64' }
    $alt = Join-Path $root (Join-Path 'bin' (Join-Path $other (Join-Path $Configuration 'JidaTSF.dll')))
    if (Test-Path $alt) { $dllPath = $alt; $Platform = $other }
}

if (-not (Test-Path $dllPath)) {
    Write-Warning "未找到 JidaTSF.dll，跳过 regsvr32，直接清理注册表残留。"
} else {
    Write-Host "反注册: $dllPath" -ForegroundColor Cyan
    $regsvr = Get-SystemRegsvr32
    $proc = Start-Process -FilePath $regsvr `
        -ArgumentList @('/s','/u',"`"$dllPath`"") -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        Write-Warning "regsvr32 /u 返回 $($proc.ExitCode)，继续清理注册表。"
    }
}

# --------------------------- 清理注册表残留 ---------------------------

$clsidStr = '{7A3E8F1C-4B2D-4E5F-9C8A-1B6D3E7F2A5C}'

$pathsToRemove = @(
    "HKLM:\SOFTWARE\Classes\CLSID\$clsidStr",
    "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$clsidStr",
    "HKCU:\SOFTWARE\Classes\CLSID\$clsidStr"
)

foreach ($p in $pathsToRemove) {
    if (Test-Path $p) {
        try {
            Remove-Item $p -Recurse -Force
            Write-Host "  删除 $p" -ForegroundColor DarkGray
        } catch {
            Write-Warning "删除 $p 失败: $($_.Exception.Message)"
        }
    }
}

# TSF 语言 profile 缓存（位于 HKCU 下，可安全删）
$tsfCacheKeys = @(
    "HKCU:\SOFTWARE\Microsoft\CTF\TIP\$clsidStr",
    "HKCU:\SOFTWARE\Microsoft\CTF\Assemblies\$clsidStr"
)
foreach ($p in $tsfCacheKeys) {
    if (Test-Path $p) {
        try {
            Remove-Item $p -Recurse -Force
            Write-Host "  删除 $p" -ForegroundColor DarkGray
        } catch {
            Write-Warning "删除 $p 失败: $($_.Exception.Message)"
        }
    }
}

Write-Host ''
Write-Host '✔ 卸载完成' -ForegroundColor Green
Write-Host '  建议注销后重新登录，以便系统刷新输入法列表。' -ForegroundColor Yellow