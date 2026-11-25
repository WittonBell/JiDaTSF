<#
.SYNOPSIS
    注册 JidaTSF 输入法（TSF 文本服务）。
.DESCRIPTION
    - 自动提升到管理员权限
    - 校验 DLL 是否存在并匹配当前架构
    - 用 regsvr32 注册 COM/TSF 组件
    - 把 data 目录复制到 DLL 同目录
    - 注册后自检 HKCU/HKCR 是否写入
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\register.ps1
    powershell -ExecutionPolicy Bypass -File .\scripts\register.ps1 -Platform x64 -Configuration Release
#>
[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [ValidateSet('x64','x86','Auto')]
    [string]$Platform = 'Auto',
    [switch]$NoElevate,
    [switch]$SkipDataCopy
)

$ErrorActionPreference = 'Stop'

# --------------------------- 工具函数 ---------------------------

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-Elevate {
    param([string[]]$ArgList)
    $exe  = (Get-Process -Id $PID).Path
    $args = $ArgList -join ' '
    Start-Process -FilePath $exe -ArgumentList $args -Verb RunAs -Wait
}

function Get-SystemRegsvr32 {
    # 强制使用 64 位的 regsvr32，避免 32 位进程在 WOW64 下访问 System32 被重定向
    $sys = [Environment]::GetFolderPath('System')
    # 'System' 在 64 位 PowerShell 中返回 System32，在 32 位 PowerShell 中返回 SysWOW64
    if ([Environment]::Is64BitOperatingSystem) {
        $p = Join-Path $env:WINDIR 'System32\regsvr32.exe'
        if (Test-Path $p) { return $p }
    }
    return (Join-Path $sys 'regsvr32.exe')
}

function Test-DllArchitecture {
    param([string]$DllPath)
    try {
        $fs = [System.IO.File]::OpenRead($DllPath)
        try {
            $br = New-Object System.IO.BinaryReader($fs)
            $fs.Seek(0x3C, 'Begin') | Out-Null
            $peOff = $br.ReadInt32()
            $fs.Seek($peOff + 4, 'Begin') | Out-Null
            $machine = $br.ReadUInt16()
            $br.Close()
        } finally { $fs.Close() }
        switch ($machine) {
            0x8664  { return 'x64' }
            0x014C  { return 'x86' }
            0xAA64  { return 'arm64' }
            default { return 'unknown' }
        }
    } catch { return 'unknown' }
}

# --------------------------- 提权 ---------------------------

if (-not (Test-Admin)) {
    if ($NoElevate) {
        throw '需要管理员权限。请右键以管理员身份运行 PowerShell。'
    }
    Write-Host '未以管理员运行，尝试自动提升权限...' -ForegroundColor Yellow
    $self = $PSCommandPath
    if (-not $self) { $self = $MyInvocation.MyCommand.Path }
    $argList = @('-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$self`"")
    if ($Configuration) { $argList += @('-Configuration', $Configuration) }
    if ($Platform)      { $argList += @('-Platform', $Platform) }
    if ($SkipDataCopy)  { $argList += '-SkipDataCopy' }
    Start-Process -FilePath (Get-Process -Id $PID).Path `
                  -ArgumentList $argList -Verb RunAs -Wait
    exit 0
}

# --------------------------- 定位文件 ---------------------------

$root = Split-Path -Parent $PSScriptRoot
Write-Host "工程根目录: $root" -ForegroundColor Cyan

# 决定平台
if ($Platform -eq 'Auto') {
    $Platform = if ([Environment]::Is64BitOperatingSystem) { 'x64' } else { 'x86' }
}

$dllPath = Join-Path $root (Join-Path 'bin' (Join-Path $Platform (Join-Path $Configuration 'JidaTSF.dll')))
if (-not (Test-Path $dllPath)) {
    # 尝试另一个平台作为回退
    $other = if ($Platform -eq 'x64') { 'x86' } else { 'x64' }
    $alt = Join-Path $root (Join-Path 'bin' (Join-Path $other (Join-Path $Configuration 'JidaTSF.dll')))
    if (Test-Path $alt) {
        Write-Host "未找到 $Platform 版，改用 $other 版: $alt" -ForegroundColor Yellow
        $Platform = $other
        $dllPath  = $alt
    } else {
        throw "未找到 JidaTSF.dll。请先运行 scripts\build.ps1 构建。`n  查找路径: $dllPath"
    }
}
Write-Host "DLL 路径: $dllPath" -ForegroundColor Cyan

# 架构校验
$dllArch = Test-DllArchitecture -DllPath $dllPath
if ($dllArch -ne 'unknown' -and $dllArch -ne $Platform) {
    throw "DLL 架构 ($dllArch) 与目标平台 ($Platform) 不匹配。"
}

# --------------------------- 注册 ---------------------------

$regsvr = Get-SystemRegsvr32
Write-Host "使用 regsvr32: $regsvr" -ForegroundColor Cyan

$regArgs = @('/s', "`"$dllPath`"")
$proc = Start-Process -FilePath $regsvr -ArgumentList $regArgs -Wait -PassThru -NoNewWindow
if ($proc.ExitCode -ne 0) {
    # 回退：DllInstall（SampleIME 风格）
    Write-Warning "regsvr32 返回 $($proc.ExitCode)，尝试 DllInstall 方式..."
    $rundll = Join-Path $env:WINDIR 'System32\rundll32.exe'
    Start-Process -FilePath $rundll `
        -ArgumentList "`"$dllPath`",DllInstall" -Wait -NoNewWindow | Out-Null
}

# --------------------------- 复制 data ---------------------------

if (-not $SkipDataCopy) {
    $srcData = Join-Path $root 'data'
    $dstData = Join-Path (Split-Path $dllPath) 'data'
    if (Test-Path $srcData) {
        New-Item -ItemType Directory $dstData -Force | Out-Null
        Get-ChildItem $srcData -File | ForEach-Object {
            Copy-Item $_.FullName $dstData -Force
            Write-Host ("  + data\{0}" -f $_.Name) -ForegroundColor DarkGray
        }
        $dictionaryPath = Join-Path $dstData 'jidian_wubi86.txt'
        $cacheGenerator = Join-Path $PSScriptRoot 'build-JidaDictionaryCache.ps1'
        if ((Test-Path -LiteralPath $dictionaryPath) -and
            (Test-Path -LiteralPath $cacheGenerator)) {
            try {
                & $cacheGenerator -InputPath $dictionaryPath `
                    -OutputPath ($dictionaryPath + '.jdc')
            } catch {
                Write-Warning "词库缓存生成失败，将回退到文本词库加载: $_"
            }
        }
    } else {
        Write-Warning "未找到 $srcData，跳过码表复制。"
    }
}

$dllDir = Split-Path $dllPath

# 给 ALL APPLICATION PACKAGES (S-1-15-2-1) 授予读取和执行权限
icacls $dllDir /grant "*S-1-15-2-1:(OI)(CI)RX"

# 给 RESTRICTED APPLICATION PACKAGES (S-1-15-2-2) 授予读取和执行权限
icacls $dllDir /grant "*S-1-15-2-2:(OI)(CI)RX"

# --------------------------- 自检 ---------------------------

$clsidStr = '{7A3E8F1C-4B2D-4E5F-9C8A-1B6D3E7F2A5C}'
$clsidKey = "HKLM:\SOFTWARE\Classes\CLSID\$clsidStr"
$ok = Test-Path $clsidKey

Write-Host ''
if ($ok) {
    Write-Host '✔ 注册成功' -ForegroundColor Green
    Write-Host '  下一步：设置 → 时间和语言 → 语言 → 中文 → 选项 → 键盘 → 添加键盘 → 极达五笔' -ForegroundColor Yellow
} else {
    Write-Warning "未在 $clsidKey 找到 CLSID，注册可能失败。请检查事件查看器或重试。"
}

Write-Host ''
Write-Host '如需卸载：powershell -ExecutionPolicy Bypass -File scripts\unregister.ps1' -ForegroundColor DarkGray