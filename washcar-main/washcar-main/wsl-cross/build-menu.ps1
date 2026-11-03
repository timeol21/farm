#Requires -Version 5.1
<#
.SYNOPSIS
    washcar 一键编译 —— 在 Windows 上双击/命令行启动，把源码交给 WSL 编译，产物回传到 Windows。

.DESCRIPTION
    两种编译方式：
      native   -> x86_64，在 WSL 里直接跑（业务逻辑快速验证，秒级出结果）
      aarch64  -> RK3568 盒子产物（glibc 2.31 / gcc-9，现场调试用，产物回传 dist\aarch64）

    整个过程：环境体检 -> 同步源码到 WSL 原生盘 -> configure -> 编译（实时进度条+实时报错）
              -> 收集产物 -> 回传到 wsl-cross\dist\

    WSL 侧干活的是 wsl-cross/build-win.sh，它按 "@@标记@@" 协议吐进度，本脚本负责渲染。

.PARAMETER Mode
    编译方式：native / aarch64。不传则弹菜单选。

.PARAMETER Targets
    目标，空格分隔。可用 main / all，也可写具体 target 名。
    例：-Targets "main"   -Targets "config_test washcar_test"

.PARAMETER BuildType
    Release / Debug。Debug 保留符号，供 gdb / addr2line。

.PARAMETER Check
    只做环境体检，不编译。

.PARAMETER Yes
    不弹任何菜单，全用参数（缺省值：aarch64 + main + Release）。

.PARAMETER Distro
    WSL 发行版名。留空 = 用默认发行版。

.PARAMETER Jobs
    并行数，默认 0 = 按 WSL 的 CPU 核数自动。

.EXAMPLE
    .\wsl-cross\build-menu.ps1

.EXAMPLE
    .\wsl-cross\build-menu.ps1 -Mode aarch64 -Targets main -BuildType Release -Yes
#>
[CmdletBinding()]
param(
    [ValidateSet('native', 'aarch64')][string]$Mode = '',
    [string]$Targets = '',
    [ValidateSet('', 'Release', 'Debug')][string]$BuildType = '',
    [switch]$Check,
    [switch]$Rebuild,
    [switch]$Yes,
    [switch]$ShowDetail,
    [string]$Distro = '',
    # 并行数；0 = 按 WSL 的 CPU 核数自动（默认）
    [int]$Jobs = 0,
    # 部署目标，写进产物里的「部署说明.txt」，格式 user@host:/路径；不给就用项目默认
    [string]$DeployTarget = '',
    # 原生 main 的全链路闭环跑多快：1=照实(约 7 分钟)，50≈9 秒。只影响模拟器虚拟时钟
    [int]$TimeScale = 25,
    # 直接跑 main 跑多久（秒）；0 = 一直跑，Ctrl+C 停（默认一直跑，方便随时下发命令）
    [int]$RunSeconds = 0,
    # 直跑 main 用哪份配置（默认 wsl-cross\run\config.json）
    [string]$Config = ''
)

$ErrorActionPreference = 'Stop'

# 中文输出：PS 5.1 默认按 OEM 代码页解码子进程 stdout，会把 UTF-8 的中文打成乱码
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$OutputEncoding = [Text.Encoding]::UTF8
try { $Host.UI.RawUI.WindowTitle = 'washcar 一键编译' } catch { }

# =============================================================================
# 0. 常量与路径
# =============================================================================
$RepoWin = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$DistWin = Join-Path $PSScriptRoot 'dist'
# 仓库路径里有括号 washcar(ztl)，wsl 传参会被 shell 当语法错误。
# 在 WSL 家目录建一个无括号软链，所有 WSL 侧命令都走这个入口。
$LinkName = 'washcar-win'

$script:WslArgs = @()
if ($Distro) { $script:WslArgs = @('-d', $Distro) }

# 渲染状态
$script:State = @{
    Errors    = 0
    Warnings  = 0
    Artifacts = @()
    Info      = @{}
    UpToDate  = $false
    BarOpen   = $false
    BarPad    = 0
    LastResult = ''
    Stopwatch = [Diagnostics.Stopwatch]::StartNew()
}

# 编译目标清单不写死在这里 —— 由 build-win.sh --list-targets 从 src/CMakeLists.txt
# 的 add_executable 自动发现（说明文字取 "# name: 说明" 注释）。新增测试不用改脚本。
$script:TargetList = $null

# =============================================================================
# 1. 终端渲染：把 @@标记@@ 协议翻译成进度条 / 彩色报错
# =============================================================================
function Get-ConsoleWidth {
    try {
        $w = $Host.UI.RawUI.WindowSize.Width
        if ($w -ge 60) { return [Math]::Min($w - 2, 150) }
    } catch { }
    return 100
}

function Close-Bar {
    if ($script:State.BarOpen) {
        Write-Host ''
        $script:State.BarOpen = $false
        $script:State.BarPad = 0
    }
}

function Write-Sep([string]$char = '-') {
    Write-Host ('  ' + ($char * ((Get-ConsoleWidth) - 4))) -ForegroundColor DarkGray
}

function Write-Phase([string]$text) {
    Close-Bar
    Write-Host ''
    # 协议里是 "1/5 环境体检"，显示成 "[1/5] 环境体检"
    if ($text -match '^(\d+/\d+)\s+(.*)$') {
        Write-Host ('  [' + $Matches[1] + '] ' + $Matches[2]) -ForegroundColor Cyan
    } else {
        Write-Host ('  ' + $text) -ForegroundColor Cyan
    }
}

function Write-ProgressBar([string]$cur, [string]$tot, [string]$label) {
    $c = 0; $t = 0
    [void][int]::TryParse($cur, [ref]$c)
    [void][int]::TryParse($tot, [ref]$t)
    if ($t -le 0) { return }

    $pct = [int][Math]::Floor(100.0 * $c / $t)
    if ($pct -gt 100) { $pct = 100 }
    $bw = 18
    $fill = [int][Math]::Round($bw * $pct / 100.0)
    if ($fill -gt $bw) { $fill = $bw }
    $bar = ('=' * $fill) + ('-' * ($bw - $fill))

    $head = '  [' + $bar + '] ' + $pct.ToString().PadLeft(3) + '% ' + ('{0,4}/{1,-4}' -f $c, $t)
    $tail = ' {0,6:N1}s' -f $script:State.Stopwatch.Elapsed.TotalSeconds
    $room = (Get-ConsoleWidth) - $head.Length - $tail.Length - 3
    if ($room -lt 8) { $room = 8 }
    if ($label.Length -gt $room) { $label = $label.Substring(0, $room - 1) + '~' }

    $text = $head + '  ' + $label.PadRight($room) + $tail
    if ($text.Length -lt $script:State.BarPad) { $text = $text.PadRight($script:State.BarPad) }
    $script:State.BarPad = [Math]::Max($script:State.BarPad, $text.Length)

    Write-Host ("`r" + $text) -NoNewline -ForegroundColor DarkGray
    $script:State.BarOpen = $true
}

function Show-Marker($item) {
    $line = if ($item -is [System.Management.Automation.ErrorRecord]) { $item.Exception.Message } else { [string]$item }
    if ($null -eq $line) { return }

    if ($line -notmatch '^\s*@@([A-Z]+)@@\s?(.*)$') {
        Close-Bar
        Write-Host ('  ' + $line)
        return
    }

    $tag = $Matches[1]
    $rest = $Matches[2]

    switch ($tag) {
        'PHASE' { Write-Phase $rest }
        'PROG' {
            if ($rest -match '^(\d+)\s+(\d+)\s+(.*)$') { Write-ProgressBar $Matches[1] $Matches[2] $Matches[3] }
        }
        'UPTODATE' {
            Close-Bar
            $script:State.UpToDate = $true
            Write-Host '  没有需要重编的文件（增量构建已是最新）' -ForegroundColor Yellow
        }
        'ERR' {
            Close-Bar
            $script:State.Errors++
            Write-Host ('  x ' + $rest) -ForegroundColor Red
        }
        'ERRCTX' {
            Write-Host ('      ' + $rest) -ForegroundColor DarkRed
        }
        'WARN' {
            Close-Bar
            $script:State.Warnings++
            Write-Host ('  ! ' + $rest) -ForegroundColor Yellow
        }
        'HINT' {
            Close-Bar
            Write-Host ('  > ' + $rest) -ForegroundColor Cyan
        }
        'TAIL' {
            Write-Host ('  | ' + $rest) -ForegroundColor DarkGray
        }
        'NOTE' {
            Close-Bar
            Write-Host ('  ' + $rest) -ForegroundColor Gray
        }
        'ART' {
            Close-Bar
            $parts = $rest -split '\|'
            if ($parts.Count -ge 3) {
                $script:State.Artifacts += [pscustomobject]@{
                    WslPath = $parts[0]; Size = $parts[1]; Label = $parts[2]
                    WinPath = ConvertTo-WinPath $parts[0]
                }
            }
        }
        'INFO' {
            if ($rest -match '^([A-Za-z_]+)=(.*)$') { $script:State.Info[$Matches[1]] = $Matches[2] }
        }
        'END' {
            Close-Bar
            $f = $rest -split '\s+'
            $script:State.LastResult = $f[0]
            $script:State.Elapsed = if ($f.Count -gt 1) { [int]$f[1] } else { 0 }
        }
        default {
            Close-Bar
            Write-Host ('  ' + $rest)
        }
    }
}

# =============================================================================
# 2. WSL 桥接
# =============================================================================
function ConvertTo-WslPath([string]$p) {
    $x = $p -replace '\\', '/'
    if ($x -match '^([A-Za-z]):/(.*)$') { return ('/mnt/' + $Matches[1].ToLower() + '/' + $Matches[2]) }
    return $x
}

function ConvertTo-WinPath([string]$p) {
    if ($p -match '^/mnt/([a-z])/(.*)$') { return ($Matches[1].ToUpper() + ':\' + ($Matches[2] -replace '/', '\')) }
    return $p
}

# 展示用：仓库内的路径显示成相对路径，免得一行太长在窄窗口里折行
function Format-Path([string]$p) {
    if ($p -and $p.StartsWith($RepoWin, [StringComparison]::OrdinalIgnoreCase)) {
        return $p.Substring($RepoWin.Length).TrimStart('\', '/')
    }
    return $p
}

function Invoke-WslText([string]$command) {
    $out = & wsl.exe @script:WslArgs -- bash -c $command 2>&1 | Out-String
    return @{ Text = $out.Trim(); Code = $LASTEXITCODE }
}

function Initialize-Bridge {
    # 仓库必须在本地盘：WSL 只能挂 /mnt/<盘符>/，UNC 共享路径（\\server\share）映射不了
    if ($RepoWin -notmatch '^[A-Za-z]:\\') {
        throw "仓库不在本地盘：$RepoWin   WSL 只认 /mnt/<盘符>/，请把仓库放到 C:\ 或 D:\ 这类本地磁盘。"
    }

    $r = Invoke-WslText 'echo $HOME'
    if ($r.Code -ne 0 -or -not $r.Text -or $r.Text -notmatch '^/') {
        throw "WSL 没有响应。先在 cmd 里跑 'wsl -l -v' 确认发行版能用；装在非默认发行版就加 -Distro 名字。"
    }
    $script:WslHome = $r.Text
    $script:LinkWsl = $script:WslHome + '/' + $LinkName
    $script:RepoWsl = ConvertTo-WslPath $RepoWin

    # 目标位置如果被普通目录/文件占着，ln -sfn 会往那个目录"里面"塞链接，
    # 后面的 test -f 必然失败、报错很难懂。先查再建。
    $r = Invoke-WslText ("if [ -L '{0}' ]; then echo link; elif [ -e '{0}' ]; then echo other; else echo none; fi" -f $script:LinkWsl)
    if ($r.Text -eq 'other') {
        throw "$($script:LinkWsl) 已存在且不是软链。删掉它再跑（WSL 里执行 rm -rf '$($script:LinkWsl)'）。"
    }
    $r = Invoke-WslText ("ln -sfn '{0}' '{1}'" -f $script:RepoWsl, $script:LinkWsl)
    if ($r.Code -ne 0) { throw "建软链失败：ln -sfn '$($script:RepoWsl)' '$($script:LinkWsl)'  $($r.Text)" }

    $r = Invoke-WslText ("test -f '{0}/wsl-cross/build-win.sh' && echo yes" -f $script:LinkWsl)
    if ($r.Text -ne 'yes') { throw "WSL 里看不到 wsl-cross/build-win.sh，检查软链 $($script:LinkWsl) -> $($script:RepoWsl)" }
}

function Invoke-BuildScript {    param([hashtable]$Env, [string[]]$ExtraArgs = @())

    $pairs = @()
    foreach ($k in $Env.Keys) { $pairs += ("{0}='{1}'" -f $k, $Env[$k]) }
    $cmd = ($pairs -join ' ') + (" bash '{0}/wsl-cross/build-win.sh'" -f $script:LinkWsl)
    if ($ExtraArgs.Count -gt 0) { $cmd += ' ' + ($ExtraArgs -join ' ') }

    $script:State.Errors = 0
    $script:State.Warnings = 0
    $script:State.Artifacts = @()
    $script:State.Info = @{}
    $script:State.UpToDate = $false
    $script:State.LastResult = ''
    $script:State.Stopwatch = [Diagnostics.Stopwatch]::StartNew()

    & wsl.exe @script:WslArgs -- bash -c $cmd 2>&1 | ForEach-Object { Show-Marker $_ }
    Close-Bar
    return $LASTEXITCODE
}

# =============================================================================
# 3. 交互菜单
# =============================================================================
# 目标清单:问 WSL 侧要,解析 @@TARGET@@ name|desc
function Get-TargetCatalog {
    if ($script:TargetList) { return $script:TargetList }
    $r = Invoke-WslText ("WC_PROJECT_ROOT='{0}' bash '{0}/wsl-cross/build-win.sh' --list-targets" -f $script:LinkWsl)
    $list = @()
    foreach ($ln in ($r.Text -split "`r?`n")) {
        if ($ln -match '^@@TARGET@@\s?(.*)$') {
            $f = $Matches[1] -split '\|', 2
            $list += [pscustomobject]@{
                Name = $f[0]
                Desc = $(if ($f.Count -gt 1) { $f[1] } else { '' })
            }
        }
    }
    if ($list.Count -eq 0) { throw "没能取到编译目标清单（build-win.sh --list-targets 没输出）" }
    $script:TargetList = $list
    return $list
}

function Show-Banner {
    # 输出被重定向/管道接走时没有控制台句柄，Clear-Host 会抛"句柄无效"，跳过清屏即可
    if (-not [Console]::IsOutputRedirected) { try { Clear-Host } catch { } }
    Write-Host ''
    Write-Host '  ==================================================================' -ForegroundColor DarkCyan
    Write-Host '    washcar 一键编译   ·   Windows -> WSL -> 回传' -ForegroundColor Cyan
    Write-Host '  ==================================================================' -ForegroundColor DarkCyan
    Write-Host ("   仓库   " + $RepoWin) -ForegroundColor DarkGray
    Write-Host ("   WSL    " + $script:RepoWsl) -ForegroundColor DarkGray
    Write-Host ''
}

# Read-Host 在 stdin 结束(重定向 / Ctrl+Z)时返回的是 AutomationNull，
# 直接 `.Trim()` 会抛"不能对 Null 调用方法"。统一走这个包装。
function Read-Line([string]$prompt) {
    $v = Read-Host $prompt
    if ($null -eq $v) { return $null }
    return [string]$v
}

function Read-Choice([string]$prompt, [string]$default, [string[]]$valid) {    while ($true) {
        $a = Read-Line $prompt
        if ($null -eq $a -or [string]::IsNullOrWhiteSpace($a)) { $a = $default }
        $a = $a.Trim()
        if ($valid -contains $a) { return $a }
        Write-Host ("  只能选：" + ($valid -join ' / ')) -ForegroundColor Red
    }
}

function Select-Mode {
    Write-Host '  编译方式' -ForegroundColor White
    Write-Host '   [1] 原生 x86_64    在 WSL 里直接跑，秒级验证业务逻辑      （默认）' -ForegroundColor Gray
    Write-Host '   [2] RK3568 aarch64 盒子产物，现场调试用；自动回传 Windows' -ForegroundColor Gray
    Write-Host ''
    $c = Read-Choice '选择' '1' @('1', '2')
    if ($c -eq '2') { return 'aarch64' } else { return 'native' }
}

function Select-Targets([string]$preset) {
    $all = @(Get-TargetCatalog)
    # main 已经是预设 1) 了，单个列表里不再重复列
    $indiv = @($all | Where-Object { $_.Name -ne 'main' })

    Write-Host '  编译目标（可多选：1   或   1 3 5   ，也可以直接写 target 名；回车 = main）' -ForegroundColor White
    Write-Host '   -- 预设 ----------------------------------------------------------' -ForegroundColor DarkGray
    Write-Host '   1) main                主程序（大厅服务全链路）' -ForegroundColor Gray
    Write-Host ('   2) all                 全部目标（{0} 个）' -f $all.Count) -ForegroundColor Gray
    Write-Host '   -- 单个（来自 src/CMakeLists.txt，新增自动出现）------------------' -ForegroundColor DarkGray

    $maxDesc = [Math]::Max(20, (Get-ConsoleWidth) - 42)
    $rows = @([pscustomobject]@{ Name = 'main' }, [pscustomobject]@{ Name = 'all' })
    $n = 3
    foreach ($t in $indiv) {
        $desc = if ($t.Desc) { $t.Desc } else { '(CMakeLists 里没写说明)' }
        if ($desc.Length -gt $maxDesc) { $desc = $desc.Substring(0, $maxDesc - 1) + '~' }
        Write-Host ('  {0,2}) {1,-22} {2}' -f $n, $t.Name, $desc) -ForegroundColor Gray
        $rows += [pscustomobject]@{ Name = $t.Name }
        $n++
    }
    Write-Host ''

    while ($true) {
        $a = Read-Line '  选择'
        if ($null -eq $a -or [string]::IsNullOrWhiteSpace($a)) { $a = if ($preset) { $preset } else { '1' } }
        $picked = @()
        $bad = $null
        foreach ($tok in ($a -split '[,\s]+' | Where-Object { $_ })) {
            if ($tok -match '^\d+$') {
                $i = [int]$tok
                if ($i -lt 1 -or $i -gt $rows.Count) { $bad = $tok; break }
                $picked += $rows[$i - 1].Name
            } elseif ($tok -eq 'main' -or $tok -eq 'all' -or ($all.Name -contains $tok)) {
                $picked += $tok
            } else {
                $bad = $tok; break
            }
        }
        if ($bad) {
            Write-Host ("  没有这一项：" + $bad) -ForegroundColor Red
            continue
        }
        if (-not $picked) { Write-Host '  至少选一个' -ForegroundColor Red; continue }
        return ($picked -join ' ')
    }
}

function Select-BuildType([string]$mode) {
    Write-Host ''
    Write-Host '  构建类型' -ForegroundColor White
    if ($mode -eq 'native') {
        Write-Host '   [1] 快速  -O0 -g0，编译最快，逻辑验证够用   （默认）' -ForegroundColor Gray
        Write-Host '   [2] 调试  -O0 -g，带符号，能挂 gdb          ' -ForegroundColor Gray
    } else {
        Write-Host '   [1] Release  strip 掉调试符号，体积小，直接上盒子  （默认）' -ForegroundColor Gray
        Write-Host '   [2] Debug    保留符号，现场 gdb / addr2line / core dump 用' -ForegroundColor Gray
    }
    Write-Host ''
    $c = Read-Choice '选择' '1' @('1', '2')
    if ($c -eq '2') { return 'Debug' } else { return 'Release' }
}

# =============================================================================
# 4. 主流程
# =============================================================================
# -Check 只是体检，不需要选编译方式/目标，别让它卡在交互菜单上
$interactive = -not $Yes -and -not $Check

# 兜底：别让用户对着 PowerShell 的红色堆栈发呆（双击运行时窗口还会秒关）
# exit 不会被 trap 捕获（已验证），所以下面正常的 exit 不受影响。
trap {
    Close-Bar
    Write-Host ''
    Write-Host ('  [异常] ' + $_.Exception.Message) -ForegroundColor Red
    if ($_.ScriptStackTrace) { Write-Host ('  ' + $_.ScriptStackTrace) -ForegroundColor DarkGray }
    Write-Host ''
    if ($interactive) { $null = Read-Line '  回车退出' }
    exit 1
}

try {
    # 先连上 WSL 再打 banner —— banner 里要显示 WSL 侧的仓库路径
    Initialize-Bridge
    Show-Banner
} catch {
    Write-Host ''
    Write-Host ('  [错误] ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host ''
    exit 2
}

if ($interactive) {
    if (-not $Mode) { $Mode = Select-Mode }
    Write-Host ''
    if (-not $Targets) { $Targets = Select-Targets $Targets }
    if (-not $BuildType) { $BuildType = Select-BuildType $Mode }
} else {
    if (-not $Mode) { $Mode = 'aarch64' }
    if (-not $Targets) { $Targets = 'main' }
    if (-not $BuildType) { $BuildType = 'Release' }
}

$subdir = if ($Mode -eq 'native') { 'native' } elseif ($BuildType -eq 'Debug') { 'aarch64-debug' } else { 'aarch64' }
$distDirFull = Join-Path $DistWin $subdir
$logWin = Join-Path (Join-Path $DistWin 'logs') ("build-" + $subdir + ".log")
# 直跑 main 用的配置：默认放在仓库里（Windows 侧直接改），-Config 可指向别处
$cfgWin = if ($Config) { [IO.Path]::GetFullPath($Config) } else { Join-Path (Join-Path $PSScriptRoot 'run') 'config.json' }

$envMap = @{
    WC_MODE        = $Mode
    WC_TARGETS     = $Targets
    WC_BUILD_TYPE  = $BuildType
    WC_JOBS        = $Jobs
    WC_DIST        = (ConvertTo-WslPath $DistWin)
    WC_PROJECT_ROOT = $script:LinkWsl
    WC_VERBOSE     = $(if ($ShowDetail) { '1' } else { '0' })
    WC_DEPLOY_TARGET = $DeployTarget
}

# ---- 摘要 ----
if ($interactive) {
    Write-Host ''
    Write-Sep '='
    Write-Host '   这次要编' -ForegroundColor White
    Write-Host ('   方式   ' + $(if ($Mode -eq 'native') { '原生 x86_64（WSL 内直接跑）' } else { 'RK3568 aarch64（盒子）' })) -ForegroundColor Gray
    Write-Host ('   目标   ' + $Targets) -ForegroundColor Gray
    Write-Host ('   类型   ' + $(if ($Mode -eq 'native' -and $BuildType -eq 'Release') { '快速 (-O0 -g0)' } elseif ($BuildType -eq 'Debug') { 'Debug (-O0 -g)' } else { 'Release (strip)' })) -ForegroundColor Gray
    Write-Host ('   回传   ' + $distDirFull) -ForegroundColor Gray
    Write-Host ('   并行   -j ' + $(if ($Jobs -gt 0) { $Jobs } else { 'auto（按核数）' }) + $(if ($Rebuild) { '   （全量重编，先清构建目录）' } else { '' })) -ForegroundColor Gray
    Write-Sep '='
    Write-Host ''
    $go = Read-Line '  回车开始编译（Ctrl+C 取消）'
}

if ($Check) {
    $null = Invoke-BuildScript $envMap @('--check')
    if ($interactive) { Write-Host ''; $null = Read-Line '  回车退出' }
    exit $(if ($script:State.LastResult -eq 'ok') { 0 } else { 2 })
}

# ---- 开编 ----
Write-Host ''
$buildArgs = @()
if ($Rebuild) { $buildArgs += '--rebuild' }
$rc = Invoke-BuildScript $envMap $buildArgs
$ok = ($script:State.LastResult -eq 'ok')

Write-Host ''
Write-Sep '='
if ($ok) {
    Write-Host ('   编译成功   用时 ' + [Math]::Round($script:State.Stopwatch.Elapsed.TotalSeconds, 1) + 's   ' +
                $script:State.Artifacts.Count + ' 个产物' + $(if ($script:State.Warnings -gt 0) { '   ' + $script:State.Warnings + ' 条警告' } else { '' })) -ForegroundColor Green
    Write-Sep '-'
    foreach ($a in $script:State.Artifacts) {
        Write-Host ('   [{0}] {1,8}  {2}' -f $a.Label, $a.Size, (Format-Path $a.WinPath)) -ForegroundColor Green
    }
    if ($logWin) { Write-Host ('   日志    ' + (Format-Path $logWin)) -ForegroundColor DarkGray }
} else {
    Write-Host ('   编译失败   错误 ' + $script:State.Errors + ' 条   ' + $script:State.Warnings + ' 条警告   用时 ' +
                [Math]::Round($script:State.Stopwatch.Elapsed.TotalSeconds, 1) + 's') -ForegroundColor Red
    Write-Sep '-'
    Write-Host '   完整日志（WSL 里）：' -ForegroundColor Gray
    if ($script:State.Info.ContainsKey('LOG')) {
        Write-Host ('     ' + $script:State.Info['LOG']) -ForegroundColor Gray
    } else {
        Write-Host ('     ' + (Format-Path $logWin)) -ForegroundColor Gray
    }
    Write-Host '   产物未回传（失败时不覆盖上一次的 dist 目录）' -ForegroundColor DarkGray
}
Write-Sep '='
Write-Host ''

function Get-CopyCommand {
    $files = @($script:State.Artifacts | Where-Object { $_.Label -notmatch '^MANIFEST' } | ForEach-Object { '"' + $_.WinPath + '"' })
    if ($files.Count -eq 0) { $files = @(('"' + (Join-Path $distDirFull 'main') + '"')) }
    # 目标可配：-DeployTarget / 环境变量 WC_DEPLOY_TARGET，默认是项目现场那台盒子
    $target = if ($DeployTarget) { $DeployTarget } elseif ($env:WC_DEPLOY_TARGET) { $env:WC_DEPLOY_TARGET } else { 'ztl@192.168.31.248:~/washcar/deployed/' }
    return 'scp ' + ($files -join ' ') + ' ' + $target
}

if ($interactive) {
    $targetsForAction = @($Targets -split '\s+' | Where-Object { $_ })
    $canRun = ($Mode -eq 'native') -and ($targetsForAction -notcontains 'all')
    $isMain = ($Mode -eq 'native') -and ($targetsForAction.Count -eq 1) -and ($targetsForAction[0] -eq 'main')
    while ($true) {
        $hint = '  下一步： [O] 打开产物目录'
        if ($Mode -ne 'native') { $hint += '   [C] 复制拷贝命令' }
        if ($isMain -and $ok) { $hint += '   [R] 直接跑 main   [M] 全链路模拟（mock PLC + broker）   [E] 改配置' }
        elseif ($canRun -and $ok) { $hint += '   [R] 跑一遍' }
        if ($canRun -and $ok) { $hint += '   [G] gdb（调试档才有行号）' }
        $hint += '   [回车] 退出'
        Write-Host $hint -ForegroundColor DarkGray
        $k = Read-Line '  操作'
        if ($null -eq $k) { Write-Host '  (输入结束，退出)'; break }
        $k = $k.Trim().ToUpper()

        switch ($k) {
            'O' {
                if (Test-Path $distDirFull) { Start-Process explorer.exe $distDirFull } else { Write-Host '  还没有产物目录' -ForegroundColor Yellow }
            }
            'C' {
                $c = Get-CopyCommand
                Set-Clipboard -Value $c
                Write-Host '  已复制到剪贴板（在 PowerShell 里执行）：' -ForegroundColor Cyan
                Write-Host ('   ' + $c) -ForegroundColor Gray
            }
            'R' {
                if ($targetsForAction.Count -ne 1) { Write-Host '  目标只有一个时才能直接跑' -ForegroundColor Yellow; break }
                $null = Invoke-BuildScript @{ WC_MODE = $Mode; WC_PROJECT_ROOT = $script:LinkWsl; WC_TARGET_NAME = $targetsForAction[0]; WC_TIME_SCALE = $TimeScale; WC_RUN_MODE = 'direct'; WC_RUN_SECONDS = $RunSeconds; WC_CONFIG = (ConvertTo-WslPath $cfgWin) } @('--action', 'run')
            }
            'M' {
                if (-not $isMain) { Write-Host '  [M] 只对 main 有效（全链路模拟）' -ForegroundColor Yellow; break }
                $null = Invoke-BuildScript @{ WC_MODE = $Mode; WC_PROJECT_ROOT = $script:LinkWsl; WC_TARGET_NAME = 'main'; WC_TIME_SCALE = $TimeScale; WC_RUN_MODE = 'mock' } @('--action', 'run')
            }
            'E' {
                if (-not $isMain) { Write-Host '  [E] 只对 main 有效（直跑用的配置）' -ForegroundColor Yellow; break }
                # 先让 bash 确保文件存在（首跑会现场生成模板），再用编辑器打开 Windows 侧那份
                $null = Invoke-BuildScript @{ WC_MODE = $Mode; WC_PROJECT_ROOT = $script:LinkWsl; WC_CONFIG = (ConvertTo-WslPath $cfgWin) } @('--action', 'config')
                if (Test-Path -LiteralPath $cfgWin) {
                    $ed = if (Get-Command code -ErrorAction SilentlyContinue) { 'code' } else { 'notepad.exe' }
                    try {
                        Start-Process $ed -ArgumentList ('"' + $cfgWin + '"')
                        Write-Host ('  已用 ' + $ed + ' 打开：' + $cfgWin) -ForegroundColor Cyan
                        Write-Host '  改完保存，回这里按 [R] 就是用它跑' -ForegroundColor DarkGray
                    } catch {
                        Write-Host ('  打不开编辑器，手动改这个文件：' + $cfgWin) -ForegroundColor Yellow
                    }
                } else {
                    Write-Host '  配置没生成出来，先编一次 main' -ForegroundColor Yellow
                }
            }
            'G' {
                if ($targetsForAction.Count -ne 1) { Write-Host '  目标只有一个时才能直接调试' -ForegroundColor Yellow; break }
                $null = Invoke-BuildScript @{ WC_MODE = $Mode; WC_PROJECT_ROOT = $script:LinkWsl; WC_TARGET_NAME = $targetsForAction[0] } @('--action', 'gdb')
            }
            '' { break }
            default { Write-Host '  没有这个操作' -ForegroundColor Red }
        }
        if ($k -eq '') { break }
        Write-Host ''
    }
    exit $(if ($ok) { 0 } else { 1 })
}

exit $(if ($ok) { 0 } else { 1 })
