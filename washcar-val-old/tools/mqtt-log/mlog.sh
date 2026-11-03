#!/usr/bin/env bash
# ============================================================
# mlog.sh —— 让 mosquitto 日志"人看得懂"
#
# 作用：
#   1) 把 epoch 秒（如 1793236791）转成本地时间（2026-10-29 09:19:51）
#   2) 过滤掉每 30 分钟一次的 "Saving in-memory database" 噪音
#   3) 只留你关心的事件（连接 / 断开 / 订阅 / 发布）
#
# 用法：
#   sudo ./mlog.sh              # 【默认】实时跟踪，只看连接与断开
#   sudo ./mlog.sh all          # 实时跟踪，显示全部行（仍转时间）
#   sudo ./mlog.sh sub          # 只看订阅相关（谁订了什么主题）
#   sudo ./mlog.sh pub          # 只看 PUBLISH 转发明细
#   sudo ./mlog.sh hist         # 不回放跟踪，只把现有日志整理成可读格式打印
#   sudo ./mlog.sh hist conn    # 整理现有日志，只要连接相关
#   sudo ./mlog.sh -n 300       # 先补最近 300 行再跟踪（默认 30）
#
# 环境变量：
#   MLOG_FILE   日志路径，默认 /var/log/mosquitto/mosquitto.log
#   MLOG_LINES  初始回看行数，默认 30
#
# 说明：mosquitto 1.6.x 没有 log_timestamp_format（2.0 才加），
#       所以时间只能在"读日志"这一侧转换，本脚本就是干这个的。
# ============================================================

set -uo pipefail

LOG_FILE="${MLOG_FILE:-/var/log/mosquitto/mosquitto.log}"
LINES="${MLOG_LINES:-30}"

# ---------- 参数解析 ----------
MODE="conn"
HIST=0
while [ $# -gt 0 ]; do
    case "$1" in
        -n) LINES="${2:-30}"; shift 2 ;;
        hist) HIST=1; shift
              [ $# -gt 0 ] && { MODE="$1"; shift; } ;;
        conn|all|sub|pub) MODE="$1"; shift ;;
        -h|--help) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "未知参数: $1（用 --help 看用法）" >&2; exit 2 ;;
    esac
done

[ -r "$LOG_FILE" ] || { echo "读不了 $LOG_FILE，请用 sudo 运行，或用 MLOG_FILE 指定路径" >&2; exit 1; }

# ---------- 各模式的匹配规则 ----------
case "$MODE" in
    conn) PAT='New connection|New client connected|disconnected|Socket error|Connection lost|keepalive|refused|not authori[sz]ed|Invalid' ;;
    sub)  PAT='SUBSCRIBE|UNSUBSCRIBE|Subscribed|subscribe' ;;
    pub)  PAT='PUBLISH|Sending|Received|publish' ;;
    all)  PAT='' ;;   # 空 = 全放行
    *)    echo "未知模式: $MODE（conn|all|sub|pub）" >&2; exit 2 ;;
esac

# ---------- epoch 前缀 -> 本地时间 ----------
fmt_line() {
    local line="$1"
    local head="${line%%:*}"
    # 前 9 位以上纯数字才当 epoch（"1793236791: New connection..."）
    if [[ "$head" =~ ^[0-9]{9,}$ ]]; then
        printf '%s %s\n' "$(date -d "@$head" '+%Y-%m-%d %H:%M:%S')" "${line#*: }"
    else
        printf '%s\n' "$line"
    fi
}

filter_line() {
    local line="$1"
    # 噪音：每 30 分钟一次的内存数据库落盘（除非 explicitly all）
    if [ "$MODE" != "all" ]; then
        case "$line" in
            *"Saving in-memory database"*) return 1 ;;
            *"mosquitto version"*)         return 1 ;;
        esac
    fi
    [ -z "$PAT" ] && return 0
    printf '%s' "$line" | grep -Eq "$PAT"
}

# ---------- hist：整理现有日志 ----------
if [ "$HIST" -eq 1 ]; then
    echo "===== $LOG_FILE  模式=$MODE  ====="
    while IFS= read -r l; do
        filter_line "$l" || continue
        fmt_line "$l"
    done < "$LOG_FILE"
    exit 0
fi

# ---------- 实时跟踪 ----------
echo "===== 跟踪 $LOG_FILE  模式=$MODE  （Ctrl-C 退出）====="
tail -n "$LINES" -F "$LOG_FILE" 2>/dev/null | while IFS= read -r l; do
    filter_line "$l" || continue
    fmt_line "$l"
done
