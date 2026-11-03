#!/bin/bash
# 洗车程序开机自启安装脚本（RK3568 / Ubuntu 20.04 / systemd）
# 用法：ssh ztl@<box> 'bash -s' < install.sh
#
# 幂等：重复执行只会覆盖 unit 文件并重新加载，不会重复创建进程。

set -euo pipefail

APP_DIR=/home/ztl/washcar/deployed
APP_BIN="${APP_DIR}/main"
LOCK_FILE=/home/ztl/washcar/washcar.lock
UNIT_PATH=/etc/systemd/system/washcar.service
UNIT_SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/washcar.service"

echo "==> 检查前置条件"
if [ "$(ps -p 1 -o comm=)" != "systemd" ]; then
    echo "错误：init 不是 systemd，本脚本不适用" >&2
    exit 1
fi
[ -x "$APP_BIN" ] || { echo "错误：找不到 $APP_BIN，先部署程序再装服务" >&2; exit 1; }
command -v flock >/dev/null || { echo "错误：缺少 flock（util-linux）" >&2; exit 1; }
id -u ztl >/dev/null 2>&1 || { echo "错误：不存在 ztl 用户" >&2; exit 1; }

echo "==> 安装 unit：$UNIT_PATH"
sudo -n cp "$UNIT_SRC" "$UNIT_PATH"
sudo -n systemctl daemon-reload
sudo -n systemctl enable washcar.service

echo "==> 停掉手工启动的旧进程，交给 systemd 接管"
# flock 是排他锁，先确认没有手工实例占着，否则会杀掉正在跑的现场程序。
if pgrep -f "$APP_BIN" >/dev/null 2>&1; then
    echo "检测到手工运行实例："
    pgrep -af "$APP_BIN"
    echo "如需让服务接管，请确认后手工执行：pkill -f $APP_BIN"
    exit 2
fi

sudo -n systemctl restart washcar.service
sleep 2

echo "==> 当前状态"
systemctl is-enabled washcar.service
systemctl is-active washcar.service

cat <<'EOF'

安装完成。验证步骤：

  1) 开机自启：   sudo reboot，登录后 systemctl status washcar.service 应为 active (running)
  2) 崩溃自愈：   pkill -9 -f /home/ztl/washcar/deployed/main
                  等 5 秒后 systemctl status 应重新变成 active (running)
  3) 日志：       journalctl -u washcar.service -f              实时
                  journalctl -u washcar.service --since today  当天
  4) 单实例：     另开一个终端跑 /home/ztl/washcar/deployed/main
                  服务已在跑时，这条会阻塞等锁而不是抢端口（预期行为）
  5) outbox：     sqlite3 /home/ztl/washcar/deployed/data/command.db \
                    "SELECT id,msg_type,sent FROM mqtt_messages ORDER BY id DESC LIMIT 10;"
EOF