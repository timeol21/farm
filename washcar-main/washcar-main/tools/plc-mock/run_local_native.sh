#!/bin/bash
# =============================================================================
# WSL 本机全链路闭环：梯形图虚拟 PLC + 自建 MQTT broker + 看板 + 主程序(main)
#
# 这是给 wsl-cross（一键编译的"跑一遍"）和 verify.sh 用的**验收脚本**，
# 不做别的：起 start.py → 等主程序订阅 → 注入复位/启动 → 等洗完 → 打印证据。
#
# 前置: bash wsl-cross/build-native.sh   (编出 ~/washcar-build-native/src/main)
#
# 用法:
#   bash tools/plc-mock/run_local_native.sh
#   TIME_SCALE=50 bash tools/plc-mock/run_local_native.sh   # 50 倍速，约 10 秒一轮
#   KEEP_RUNNING=1 bash tools/plc-mock/run_local_native.sh  # 跑完留着进程看板
#
# 日志全落在 $RUN_DIR，不动仓库目录。看板地址见输出里的 DASHBOARD 行。
# =============================================================================
set -u

MOCK_SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="${RUN_DIR:-$HOME/plcmock-run-native}"
BUILD_DIR="${BUILD_DIR:-$HOME/washcar-build-native/src}"
MAIN_BIN="${MAIN_BIN:-$BUILD_DIR/main}"
TIME_SCALE="${TIME_SCALE:-1}"
PLC="${PLC:-haitian}"          # 机型 key，换机型只改这个

# 默认端口被别的实例占着时，本轮自动换一个空闲端口：
# 手动开的看板不该把"跑一遍"堵死，也不该让本轮连到别人的 broker 上跑出假结果。
port_busy() {
    python3 - "$1" <<'PY'
import socket, sys
probe = socket.socket()
probe.settimeout(0.3)
try:
    probe.connect(('127.0.0.1', int(sys.argv[1])))
    print('busy')
except OSError:
    print('free')
finally:
    probe.close()
PY
}
free_port() {
    python3 - <<'PY'
import socket
probe = socket.socket()
probe.bind(('127.0.0.1', 0))
print(probe.getsockname()[1])
probe.close()
PY
}

if [ -z "${HTTP_PORT:-}" ]; then
    if [ "$(port_busy 18081)" = "busy" ]; then HTTP_PORT=$(free_port); else HTTP_PORT=18081; fi
fi
if [ -z "${BROKER_PORT:-}" ]; then
    if [ "$(port_busy 1883)" = "busy" ]; then BROKER_PORT=$(free_port); else BROKER_PORT=1883; fi
fi
# PTY 也按运行目录分开，避免把别人正在用的 /tmp/vplc-pty 顶掉
PTY_LINK="${PTY_LINK:-/tmp/vplc-pty-$(basename "$RUN_DIR")}"

# 没显式给 SCAN_MS 时按倍速算：虚拟步长恒为 50ms（25 倍→2ms，50 倍→1ms）。
if [ -z "${SCAN_MS:-}" ]; then
    SCAN_MS=$((50 / TIME_SCALE))
    [ "$SCAN_MS" -ge 1 ] || SCAN_MS=1
fi

mkdir -p "$RUN_DIR"
rm -f "$RUN_DIR"/plc_stub.log "$RUN_DIR"/mqtt_broker.log "$RUN_DIR"/main.log \
      "$RUN_DIR"/start.log "$RUN_DIR"/config_main.json

if [ ! -x "$MAIN_BIN" ]; then
    echo "ERROR: 找不到 $MAIN_BIN，先跑: bash wsl-cross/build-native.sh" >&2
    exit 1
fi

cleanup() {
    if [ "${KEEP_RUNNING:-0}" = "1" ]; then
        echo ">>> 进程留着，手动收: kill ${START_PID:-0}"
        return
    fi
    [ -n "${START_PID:-}" ] && kill "$START_PID" 2>/dev/null
    # start.py 收到 SIGTERM 会带走 main；等它退干净，免得下一轮抢端口
    [ -n "${START_PID:-}" ] && wait "$START_PID" 2>/dev/null
    sleep 1
    # 兜底只针对本轮运行目录，别顺手杀掉别人手动开的实例
    pkill -f -- "--run-dir $RUN_DIR" 2>/dev/null
}
trap cleanup EXIT

# 1) 起模拟器（PLC 桩 + broker + 看板 + 主程序）
#    start.py 会把 config/main.json 复制到运行目录，并把串口指到 PTY、采样节奏调密
python3 "$MOCK_SRC/start.py" \
    --plc "$PLC" \
    --time-scale "$TIME_SCALE" --scan-ms "$SCAN_MS" \
    --run-dir "$RUN_DIR" \
    --http-port "$HTTP_PORT" --broker-port "$BROKER_PORT" --pty "$PTY_LINK" \
    --main "$MAIN_BIN" \
    --main-config "$MOCK_SRC/config/main.json" \
    > "$RUN_DIR/start.log" 2>&1 &
START_PID=$!

WAITED=0
while [ "$WAITED" -lt 90 ]; do
    if grep -q 'READY http' "$RUN_DIR/start.log" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$START_PID" 2>/dev/null; then
        echo "ERROR: start.py 启动失败" >&2
        tail -20 "$RUN_DIR/start.log" >&2
        exit 1
    fi
    sleep 1
    WAITED=$((WAITED + 1))
done
if ! grep -q 'READY http' "$RUN_DIR/start.log" 2>/dev/null; then
    echo "ERROR: 等 start.py 就绪超时（${WAITED}s）" >&2
    tail -20 "$RUN_DIR/start.log" >&2
    exit 1
fi

echo "DASHBOARD http://127.0.0.1:${HTTP_PORT}/  (MQTT ${BROKER_PORT}, PTY ${PTY_LINK})"

# 组件起不来（端口被占、依赖缺失）时必须马上失败：
# 否则会连着上一个实例的 broker 跑，最后打印一份"0 工序"的假结果。
if grep -aq '启动失败' "$RUN_DIR/start.log"; then
    echo "ERROR: 有组件没起来：" >&2
    grep -a '启动失败' "$RUN_DIR/start.log" >&2
    exit 1
fi

publish() {
    python3 - "$HTTP_PORT" "$1" "$2" <<'PY'
import json, sys, urllib.request
port, topic, payload = sys.argv[1], sys.argv[2], sys.argv[3]
body = json.dumps({'topic': topic, 'payload': payload, 'qos': 0}).encode('utf-8')
request = urllib.request.Request('http://127.0.0.1:%s/api/mqtt/publish' % port,
                                 data=body, headers={'Content-Type': 'application/json'})
try:
    print(json.load(urllib.request.urlopen(request, timeout=10)).get('delivered'))
except Exception as exc:
    print('ERR %s' % exc)
    sys.exit(1)
PY
}

ready_signal() {
    curl -s "http://127.0.0.1:${HTTP_PORT}/api/bootstrap" 2>/dev/null | python3 -c         'import json,sys; print((json.load(sys.stdin).get("sim") or {}).get("readySignal") or "")' 2>/dev/null
}

wait_ready() {
    # 等机型声明的"启动就绪"信号（startChecks 的第一条，海天是 M187）。
    # 信号名从 /api/bootstrap 的 sim.readySignal 取，换机型不用改脚本。
    SIGNAL="${SIGNAL_OVERRIDE:-$(ready_signal)}"
    [ -n "$SIGNAL" ] || { echo "     机型没声明 readySignal，跳过等待"; return 0; }
    python3 - "$HTTP_PORT" "$1" "$SIGNAL" <<'PY'
import json, sys, time, urllib.request
port, timeout, signal = sys.argv[1], float(sys.argv[2]), sys.argv[3]
deadline = time.time() + timeout
since = None
while time.time() < deadline:
    body = json.dumps({'watch': [signal]}).encode('utf-8')
    request = urllib.request.Request('http://127.0.0.1:%s/api/state' % port, data=body,
                                     headers={'Content-Type': 'application/json'})
    try:
        values = json.load(urllib.request.urlopen(request, timeout=3)).get('values', {})
        if values.get(signal) == 1:
            # 连续 1 秒为 1 再发启动：盒子读的是自己的采集缓存，会晚几百毫秒
            since = since or time.time()
            if time.time() - since >= 1.0:
                sys.exit(0)
        else:
            since = None
    except Exception:
        since = None
    time.sleep(0.2)
sys.exit(1)
PY
}

# 2) 等主程序连上并订阅（比死等固定秒数稳）
SUB_WAIT=0
while [ "$SUB_WAIT" -lt 40 ]; do
    grep -aq 'SUBSCRIBE' "$RUN_DIR/mqtt_broker.log" 2>/dev/null && break
    sleep 1
    SUB_WAIT=$((SUB_WAIT + 1))
done

echo "===== 1. mock PLC 就绪 ====="
grep -a -E '串口桩|梯形图:' "$RUN_DIR/start.log" | head -2
echo "===== 2. broker 就绪 ====="
grep -a -E 'MQTT broker|broker listening' "$RUN_DIR/start.log" | head -2
echo "===== 3. 主程序连上虚拟 PLC(连接成功次数) ====="
grep -a -c '连接成功' "$RUN_DIR/main.log" 2>/dev/null || true
echo "===== 4. 主程序订阅了什么 ====="
grep -a 'SUBSCRIBE' "$RUN_DIR/mqtt_broker.log" | head -5
echo "===== 5. mock 收到的读请求(前 5 条) ====="
grep -a 'read ' "$RUN_DIR/plc_stub.log" | head -5
echo "===== 6. mock 收到的读请求总数 ====="
grep -a -c 'read ' "$RUN_DIR/plc_stub.log" || true
echo "===== 7. 上行报文(前 5 条) ====="
# 上电后要等第一次属性变化才会上报，别抢在它前面打印空白
UP_WAIT=0
while [ "$UP_WAIT" -lt 10 ]; do
    grep -aq 'PUB ' "$RUN_DIR/mqtt_broker.log" 2>/dev/null && break
    sleep 1
    UP_WAIT=$((UP_WAIT + 1))
done
grep -a 'PUB ' "$RUN_DIR/mqtt_broker.log" | head -5

# 盒号要从配置里取：主程序订阅的是 flexem/<boxNo>/command，
# 写死别的盒号，命令会投到没人订阅的 topic 上，看着像"命令没反应"。
BOX_NO=$(python3 -c "import json;print(json.load(open('$RUN_DIR/config_main.json'))['box']['boxNo'])")
echo "===== 8. 主程序读到的关键点位 ====="
grep -a -E 'D142|M187|Y74|fault' "$RUN_DIR/main.log" | tail -4

echo "===== 9. 注入云端复位命令 ====="
echo "     盒号=$BOX_NO, 注入 topic=flexem/$BOX_NO/command"
DELIVERED=$(publish "flexem/$BOX_NO/command" '{"reset":1,"plcId":"fx_plc_01"}')
case "$DELIVERED" in
    ''|*[!0-9]*|0)
        echo "ERROR: 复位命令没送达任何订阅者（delivered=$DELIVERED，主程序没连上 broker？）" >&2
        exit 1
        ;;
esac

# 等 M187=1（车辆到位、T61 三秒确认完成）再发启动；等不到也发，让盒子给出拒绝原因
if wait_ready "${POST_CMD_WAIT:-12}"; then
    echo "     ${SIGNAL:-就绪信号}=1（启动前置条件满足），继续下发启动"
else
    echo "     ⚠ ${POST_CMD_WAIT:-12}s 内没等到 ${SIGNAL:-就绪信号}=1，仍下发启动（看盒子返回的拒绝原因）"
fi

echo "===== 10. 注入云端启动命令 ====="
publish "flexem/$BOX_NO/command" '{"Manual start":1,"plcId":"fx_plc_01"}' >/dev/null

# 等洗车跑完：不死等固定秒数，云端收到 D142=10 就走，最多等 WASH_WAIT_MAX 秒。
# 上限算法：一轮 ≈ 403 虚拟秒，虚拟步长 50ms，单次扫描按 3ms 保守估，墙钟 = 403×(3+SCAN_MS)/50。
WASH_WAIT_MAX="${WASH_WAIT_MAX:-$(awk -v ts="$TIME_SCALE" 'BEGIN{printf "%d", 403*(3+50/ts)/50 + 25}')}"
WAITED=0
while [ "$WAITED" -lt "$WASH_WAIT_MAX" ]; do
    grep -aq '"D142":10' "$RUN_DIR/mqtt_broker.log" 2>/dev/null && break
    sleep 1
    WAITED=$((WAITED + 1))
done
echo "     (等洗车结束用了 ${WAITED}s, 上限 ${WASH_WAIT_MAX}s)"
echo "===== 11. 命令后的上行报文 ====="
grep -a 'PUB ' "$RUN_DIR/mqtt_broker.log" | tail -5
echo "===== 12. 主程序命令日志 ====="
grep -a -E 'WASHCAR_START|startWashCar|Manual start|命令' "$RUN_DIR/main.log" | tail -5
echo "===== 13. mock 收到的写操作 ====="
grep -a 'write ' "$RUN_DIR/plc_stub.log" | tail -5
echo "===== 14. 盒子云端看到的工序轨迹(D142) ====="
python3 - "$RUN_DIR/mqtt_broker.log" <<'PY'
import json, re, sys

trail = []
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    if 'PUB' not in line:
        continue
    for token in re.findall(r'"D142"\s*:\s*(\d+)', line):
        if not trail or trail[-1] != token:
            trail.append(token)
print('D142 轨迹:', ' → '.join(trail) if trail else '(没采到 D142)')
print('工序数:', len(trail))
PY
echo "===== 15. 梯形图侧的实际步序 ====="
grep -a -E 'D142=' "$RUN_DIR/main.log" | tail -3
echo "===== 结束 ====="
