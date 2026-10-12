<#
.SYNOPSIS
    將 MSVC、Qt 與建置工具載入目前的 PowerShell 工作階段。

.DESCRIPTION
    @file dev-env.ps1
    @brief Windows 開發環境載入腳本
    @author Taiphoon GCS Team
    @date 2026-10-12

    Ninja 產生器需要 MSVC 環境變數，因此必須先載入 vcvars64.bat。
    必須以 dot-source 方式執行，環境變數才會留在目前的工作階段：

        . .\tools\dev-env.ps1

.PARAMETER QtRoot
    Qt kit 目錄；預設取 QT_ROOT_DIR，未設定時使用 D:\Qt\6.11.2\msvc2022_64。

.PARAMETER QtTools
    Qt 安裝目錄下的 Tools 資料夾，提供 CMake 與 Ninja。
#>
param(
    [string]$QtRoot = $(if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { 'D:\Qt\6.11.2\msvc2022_64' }),
    [string]$QtTools = 'D:\Qt\Tools'
)

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere))
{
    throw "找不到 vswhere.exe，請先安裝 Visual Studio Build Tools（使用 C++ 的桌面開發）"
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath)
{
    throw "找不到含 MSVC x64 工具組的 Visual Studio 安裝"
}

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars))
{
    throw "找不到 $vcvars"
}

if (-not (Test-Path (Join-Path $QtRoot 'lib\cmake\Qt6')))
{
    throw "找不到 Qt kit：$QtRoot"
}

# vcvars64.bat 只能在 cmd 中執行，因此擷取執行後的環境變數再寫回 PowerShell。
$envLines = & cmd.exe /s /c "`"$vcvars`" >nul 2>&1 && set"
if ($LASTEXITCODE -ne 0)
{
    throw "執行 vcvars64.bat 失敗"
}
foreach ($line in $envLines)
{
    $index = $line.IndexOf('=')
    if ($index -gt 0)
    {
        Set-Item -Path ("env:" + $line.Substring(0, $index)) -Value $line.Substring($index + 1)
    }
}

$env:QT_ROOT_DIR = $QtRoot
$env:PATH = @(
    (Join-Path $QtTools 'CMake_64\bin'),
    (Join-Path $QtTools 'Ninja'),
    (Join-Path $QtRoot 'bin'),
    $env:PATH
) -join ';'

Write-Host "已載入 MSVC：$vsPath"
Write-Host "已載入 Qt：$QtRoot"
