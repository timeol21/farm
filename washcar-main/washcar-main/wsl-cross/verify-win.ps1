# =============================================================================
# Windows 侧一键入口: 在 PowerShell 里跑 WSL 里的验证闸门
#
# 为什么要这个:
#   仓库路径带括号(washcar(ztl)),而 wsl.exe 会把参数拼成 shell 命令,
#   括号会被 shell 当成语法 —— 直接 `wsl ... bash /mnt/c/.../washcar(ztl)/x.sh` 会报
#   "syntax error near unexpected token '('"。
#   这里在 WSL 里建一个无括号的软链 ~/washcar-win 指向仓库,再用它调用。
#
# 用法(PowerShell,仓库根目录):
#   .\wsl-cross\verify-win.ps1              # 全跑闸门(约 75s)
#   .\wsl-cross\verify-win.ps1 -Fast        # 秒级档(约 6s)
#   .\wsl-cross\verify-win.ps1 -Quick       # 跳过语法闸门
#   .\wsl-cross\verify-win.ps1 -SyntaxOnly  # 只跑语法闸门
#   .\wsl-cross\verify-win.ps1 -Cross       # 追加 aarch64 交叉编译闸门
#
# 等价的 WSL 内命令(路径无括号时直接用):
#   wsl -d Ubuntu-22.04 -- bash /home/ztl/washcar-win/wsl-cross/verify.sh
# =============================================================================
[CmdletBinding()]
param(
    [switch]$Quick,
    [switch]$Fast,
    [switch]$SyntaxOnly,
    [switch]$NoMock,
    [switch]$Cross,
    [switch]$BuildOnly,
    # 留空 = 用 WSL 的默认发行版(wsl --set-default 设的那个)。
    # 指定了才用 -d 传下去 —— 别写死发行版名, 别人机器上的名字不一定一样。
    [string]$Distro = ''
)

$ErrorActionPreference = 'Stop'

$wslArgs = @()
if ($Distro) { $wslArgs = @('-d', $Distro) }

# WSL 里的家目录: 软链要放在那儿, 写死 /home/ztl 换机器就废了
$wslHome = (& wsl @wslArgs -- bash -lc 'echo $HOME' 2>$null | Out-String).Trim()
if (-not $wslHome) { throw "WSL 没响应; 用 -Distro 指定发行版" }
$Link = "$wslHome/washcar-win"

# 仓库在 Windows 侧的绝对路径 -> WSL 挂载路径(C:\... -> /mnt/c/..., D:\ -> /mnt/d/...)
$repoWin = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$drive = $repoWin.Substring(0, 1).ToLower()
$repoWsl = "/mnt/$drive" + $repoWin.Substring(2).Replace('\', '/')

Write-Host ">>> 仓库(Windows): $repoWin"
Write-Host ">>> 仓库(WSL)   : $repoWsl"
Write-Host ">>> WSL 家目录  : $wslHome"

# 建/刷新软链(目标带括号,必须整体加引号)
& wsl @wslArgs -- bash -c "ln -sfn '$repoWsl' '$Link'" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "软链创建失败: ln -sfn '$repoWsl' '$Link'" }

$script = switch ($BuildOnly) {
    $true { 'build-native.sh' }
    default { 'verify.sh' }
}

$argsList = @()
if ($Fast)       { $argsList += '--fast' }
if ($Quick)      { $argsList += '--quick' }
if ($SyntaxOnly) { $argsList += '--syntax-only' }
if ($NoMock)     { $argsList += '--no-mock' }
if ($Cross)      { $argsList += '--cross' }

$cmd = "bash $Link/wsl-cross/$script"
if ($argsList.Count -gt 0) { $cmd += ' ' + ($argsList -join ' ') }

Write-Host ">>> 执行: wsl $($wslArgs -join ' ') -- $cmd"
& wsl @wslArgs -- bash -c $cmd
exit $LASTEXITCODE
