#!/usr/bin/env python3
"""洗车机 PLC 模拟器 —— 唯一启动入口。

三样东西一起起来：

    虚拟 PLC     梯形图 + 现场物理模型，用 PTY 冒充 FX 编程口给盒子主程序用
    测试服务器   自建 MQTT broker，看板里可以手动下发任意主题/负载
    网页看板     洗车流程图（高亮当前工序）+ 软元件频率表 + MQTT 控制台

常用姿势：

    python3 start.py                          # 起模拟器 + broker + 看板
    python3 start.py --main ~/washcar-build-native/src/main
                                              # 连盒子主程序跑全链路
    python3 start.py --time-scale 25          # 25 倍速（虚拟步长仍是 50ms）
    python3 start.py --preset stall-s3-t99    # 复现现场"卡在洗车头"的故障
    python3 start.py --list                   # 看有哪些机型

看板地址默认 http://127.0.0.1:18081/（盒子主程序自己占 18080，别撞）。
"""

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import simulators                                              # noqa: E402
from core.broker import MqttBroker                             # noqa: E402
from core.dashboard import DashboardServer                     # noqa: E402
from core.runtime import PlcRuntime                            # noqa: E402
from core.serial_stub import SerialStub                        # noqa: E402

DEFAULT_HTTP_PORT = 18081
DEFAULT_BROKER_PORT = 1883
DEFAULT_PTY = '/tmp/vplc-pty'


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description='洗车机 PLC 模拟器（虚拟 PLC + 自建 MQTT broker + 网页看板）',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument('--plc', default='haitian', help='机型 key（--list 看可选）')
    parser.add_argument('--list', action='store_true', help='列出所有可用机型')
    parser.add_argument('--stop', action='store_true', help='收掉还在跑的模拟器实例（含占端口的）')
    parser.add_argument('--preset', default=None, help='现场预设（例如 stall-s3-t99）')
    parser.add_argument('--time-scale', type=float, default=1.0, help='虚拟时钟倍速（默认 1 倍）')
    parser.add_argument('--scan-ms', type=int, default=None,
                        help='扫描周期(ms)；默认按倍速算成 50/倍速，保证虚拟步长恒为 50ms')
    parser.add_argument('--run-dir', default=os.environ.get('PLC_MOCK_DIR') or os.path.expanduser('~/plcmock-run'),
                        help='运行目录（日志、主程序配置都落这里）')
    parser.add_argument('--pty', default=DEFAULT_PTY, help='PTY 符号链接路径')
    parser.add_argument('--broker-port', type=int, default=DEFAULT_BROKER_PORT)
    parser.add_argument('--http-port', type=int, default=DEFAULT_HTTP_PORT)
    parser.add_argument('--http-host', default='127.0.0.1', help='看板监听地址（0.0.0.0 可给同网段看）')
    parser.add_argument('--no-serial', action='store_true', help='不起串口桩（只看看板/调梯形图）')
    parser.add_argument('--no-broker', action='store_true', help='不起 MQTT broker')
    parser.add_argument('--no-dashboard', action='store_true', help='不起网页看板')
    parser.add_argument('--main', default=None, help='盒子主程序路径（native 编译产物 main）')
    parser.add_argument('--main-config', default=os.path.join(HERE, 'config', 'main.json'),
                        help='给主程序用的配置模板')
    parser.add_argument('--no-tune-main', action='store_true',
                        help='不改主程序配置的采样节奏（默认按 mock 调密）')
    parser.add_argument('--box-no', default=None, help='覆盖盒号（默认取配置里的 box.boxNo）')
    parser.add_argument('--open', action='store_true', help='起来后自动打开浏览器')
    parser.add_argument('--check', action='store_true', help='只做自检：装载机型、解析梯形图后退出')
    parser.add_argument('--print-ladder', action='store_true',
                        help='打印机型的梯形图 CSV 绝对路径后退出（脚本取路径用）')
    return parser.parse_args(argv)


def log(message):
    print('[%s] %s' % (time.strftime('%H:%M:%S'), message), flush=True)


def find_simulator_processes():
    """找出还在跑的模拟器进程。

    按 **cwd + cmdline** 认，不按完整路径 —— 因为大家一般是
    `cd tools/plc-mock && python3 start.py`，命令行里根本没有 "plc-mock/start.py"，
    用 `pkill -f plc-mock/start.py` 会一个都杀不掉（现场踩过）。
    """
    found = []
    if os.name != 'posix' or not os.path.isdir('/proc'):
        return found
    mine = os.getpid()
    for entry in os.listdir('/proc'):
        if not entry.isdigit() or int(entry) == mine:
            continue
        try:
            raw = open(os.path.join('/proc', entry, 'cmdline'), 'rb').read()
        except OSError:
            continue
        parts = [item.decode('utf-8', 'replace') for item in raw.split(b'\0') if item]
        if len(parts) < 2:
            continue
        # 必须是「python 解释器 + 一个叫 start.py 的脚本参数」。
        # 不能只做字符串包含：`bash -lc "cd x && python3 start.py"` 的命令行里也有 start.py，
        # 那样会把执行 --stop 的 shell 自己也算进去（踩过，把自己 SIGTERM 了）。
        if not os.path.basename(parts[0]).startswith('python'):
            continue
        if not any(os.path.basename(arg) == 'start.py' for arg in parts[1:]):
            continue
        try:
            cwd = os.readlink(os.path.join('/proc', entry, 'cwd'))
        except OSError:
            cwd = ''
        script = next(arg for arg in parts[1:] if os.path.basename(arg) == 'start.py')
        if 'plc-mock' not in cwd and 'plc-mock' not in script:
            continue
        found.append({'pid': int(entry), 'cmd': ' '.join(parts), 'cwd': cwd})
    return found


def stop_instances():
    """`--stop`：把还在跑的模拟器实例收掉（先 SIGTERM，6 秒后还在就 SIGKILL）。"""
    if os.name != 'posix':
        print('--stop 只在 Linux/WSL 上可用（靠 /proc 找进程）；Windows 请直接关掉那个终端窗口')
        return 1
    running = find_simulator_processes()
    if not running:
        print('没有在跑的模拟器进程')
        return 0
    for item in running:
        print('停止 pid=%-6d %s   (cwd %s)' % (item['pid'], item['cmd'], item['cwd']))
        try:
            os.kill(item['pid'], signal.SIGTERM)
        except OSError as exc:
            print('  SIGTERM 失败: %s' % exc)
    deadline = time.time() + 6
    while time.time() < deadline:
        if not any(os.path.exists('/proc/%d' % item['pid']) for item in running):
            break
        time.sleep(0.3)
    for item in running:
        if os.path.exists('/proc/%d' % item['pid']):
            print('  强杀 pid=%d（6 秒内没退）' % item['pid'])
            try:
                os.kill(item['pid'], signal.SIGKILL)
            except OSError:
                pass
    print('已清理 %d 个实例' % len(running))
    return 0


def port_busy(host, port):
    """端口上有没有人在听。用来在起组件前把"上一个实例没退干净"挡下来。"""
    target = '127.0.0.1' if host in ('', '0.0.0.0') else host
    probe = socket.socket()
    probe.settimeout(0.4)
    try:
        probe.connect((target, port))
        return True
    except OSError:
        return False
    finally:
        probe.close()


def prepare_main_config(args, run_dir):
    """把主程序配置复制到运行目录，并按本地 mock 的口径调采样节奏。

    三个改动都只影响运行目录的副本，仓库/盒子配置不动：
      · plcSerial.port 指到我们的 PTY
      · baudRate 9600→115200：主程序每帧后的等待按波特率缩放（10ms→0.83ms），
        否则 47 个采集点一轮 470ms，高倍速下 D142=2/4 这种短工序会被整轮跳过
      · washcarMonitorIntervalMs / acquisitionIntervalMs 调密
    """
    with open(args.main_config, encoding='utf-8') as handle:
        config = json.load(handle)
    if not args.no_tune_main:
        config.setdefault('timing', {})
        config['timing']['washcarMonitorIntervalMs'] = 50
        config['timing']['acquisitionIntervalMs'] = 20
        config.setdefault('plcSerial', {})
        config['plcSerial']['baudRate'] = 115200
        config['plcSerial']['port'] = args.pty
        config.setdefault('mqtt', {})
        config['mqtt']['brokerIp'] = '127.0.0.1'
        config['mqtt']['port'] = args.broker_port
    if args.box_no:
        config.setdefault('box', {})
        config['box']['boxNo'] = args.box_no
    path = os.path.join(run_dir, 'config_main.json')
    with open(path, 'w', encoding='utf-8') as handle:
        json.dump(config, handle, ensure_ascii=False, indent=2)
    return path, config


def read_box_info(path, box_no=None):
    """只读配置里的盒号和主题 —— 不起主程序时，看板的 MQTT 控制台也要能填对主题。"""
    try:
        with open(path, encoding='utf-8') as handle:
            config = json.load(handle)
    except (OSError, ValueError):
        return {}
    return {
        'boxNo': box_no or config.get('box', {}).get('boxNo', ''),
        'topics': config.get('mqtt', {}).get('topics', {}),
        'config': path,
    }


class MainProcess:
    """把盒子主程序当子进程带着跑；它挂了模拟器还在，看板上能看到退出码。"""

    def __init__(self, path, config_path, run_dir):
        self.path = path
        self.config_path = config_path
        self.log_path = os.path.join(run_dir, 'main.log')
        self.proc = None
        self.returncode = None
        self.error = ''

    def start(self):
        environment = os.environ.copy()
        environment['WASHCAR_CONFIG'] = self.config_path
        environment['PLC_MOCK_DIR'] = os.path.dirname(self.config_path)
        handle = open(self.log_path, 'w', buffering=1)
        try:
            self.proc = subprocess.Popen([self.path], cwd=os.path.dirname(self.config_path),
                                         env=environment, stdout=handle, stderr=subprocess.STDOUT)
        except OSError as exc:
            self.error = str(exc)
            handle.close()
            return False
        return True

    def status(self):
        running = self.proc is not None and self.proc.poll() is None
        if self.proc is not None and not running:
            self.returncode = self.proc.returncode
        return {
            'enabled': True,
            'running': running,
            'pid': self.proc.pid if self.proc else None,
            'returncode': self.returncode,
            'log': self.log_path,
            'error': self.error,
        }

    def stop(self):
        if self.proc is None:
            return
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def main(argv=None):
    args = parse_args(argv)

    if args.stop:
        return stop_instances()

    if args.list:
        available = simulators.discover()
        if not available:
            print('没有可用机型（simulators/ 下没有暴露 SIMULATOR 的包）')
            return 1
        for key, simulator in available.items():
            print('%-10s %-14s 梯形图: %s' % (key, simulator.title, simulator.ladder_name))
        return 0

    try:
        simulator = simulators.load(args.plc)
    except KeyError as exc:
        print(exc.args[0], file=sys.stderr)
        return 2

    if args.print_ladder:
        print(simulator.ladder_path)
        return 0

    run_dir = os.path.abspath(os.path.expanduser(args.run_dir))
    os.makedirs(run_dir, exist_ok=True)

    time_scale = max(0.1, args.time_scale)
    scan_ms = args.scan_ms if args.scan_ms else max(1, int(round(50.0 / time_scale)))

    log('装载机型 %s（%s）' % (simulator.title, args.plc))
    target = _PresetSimulator(simulator, args.preset) if args.preset else simulator
    try:
        runtime = PlcRuntime(target, scan_ms=scan_ms, time_scale=time_scale)
    except KeyError as exc:
        print(exc.args[0], file=sys.stderr)
        return 2
    except Exception as exc:
        print('装载失败: %s: %s' % (type(exc).__name__, exc), file=sys.stderr)
        return 3

    log('梯形图: %s（%d 条指令，%d 个软元件）'
        % (simulator.ladder_name, len(runtime.ladder.main), len(runtime.catalog.entries)))

    if args.check:
        status = runtime.status()
        print('自检通过: 机型=%s 指令=%d 软元件=%d 工序=%d'
              % (simulator.key, len(runtime.ladder.main), len(runtime.catalog.entries),
                 len(status['flow'])))
        return 0

    # ---- 端口预检：别的实例占着端口时直接失败，别连着别人的 broker 跑出一份假结果 ----
    conflicts = []
    if not args.no_broker and port_busy('127.0.0.1', args.broker_port):
        conflicts.append('MQTT %d' % args.broker_port)
    if not args.no_dashboard and port_busy(args.http_host, args.http_port):
        conflicts.append('看板 %d' % args.http_port)
    if conflicts:
        print('端口已被占用: %s' % '、'.join(conflicts), file=sys.stderr)
        holders = find_simulator_processes()
        if holders:
            print('占用者（还在跑的模拟器）:', file=sys.stderr)
            for item in holders:
                print('  pid=%-6d %s   (cwd %s)' % (item['pid'], item['cmd'], item['cwd']),
                      file=sys.stderr)
            print('收掉它们: python3 start.py --stop', file=sys.stderr)
        else:
            print('没找到模拟器进程，可能是别的程序占着（netstat/ss 查一下）', file=sys.stderr)
            print('或换端口: --http-port 18082 --broker-port 1884', file=sys.stderr)
        return 4

    # ---- 串口桩 ----
    serial_stub = None
    if not args.no_serial:
        if simulator.transport != 'fx-serial':
            log('机型 %s 的通讯层是 %s，本版本只实现了 fx-serial，跳过串口桩'
                % (simulator.key, simulator.transport))
        else:
            serial_stub = SerialStub(runtime.ladder, link=args.pty,
                                     log_path=os.path.join(run_dir, 'plc_stub.log'))
            serial_stub.start()
            log('串口桩: %s -> %s' % (args.pty, serial_stub.stats()['slave']))

    # ---- MQTT 测试服务器 ----
    broker = None
    if not args.no_broker:
        broker = MqttBroker(port=args.broker_port,
                            log_path=os.path.join(run_dir, 'mqtt_broker.log'))
        if broker.start():
            log('MQTT broker: 0.0.0.0:%d（自建，测试服务器）' % args.broker_port)
        else:
            log('MQTT broker 启动失败: %s' % broker.error)

    # ---- 主程序（可选）----
    box_info = {}
    main_process = None
    if args.main:
        config_path, config = prepare_main_config(args, run_dir)
        box_info = {
            'boxNo': config.get('box', {}).get('boxNo', ''),
            'topics': config.get('mqtt', {}).get('topics', {}),
            'config': config_path,
        }
        main_process = MainProcess(os.path.abspath(os.path.expanduser(args.main)), config_path, run_dir)
        if main_process.start():
            log('主程序: %s（pid=%d，日志 main.log）' % (args.main, main_process.proc.pid))
        else:
            log('主程序启动失败: %s' % main_process.error)
    else:
        # 不连主程序时也读一下配置：看板要知道盒号，才能拼出真的 MQTT 主题
        box_info = read_box_info(args.main_config, args.box_no)

    runtime.start()

    # ---- 看板 ----
    dashboard = None
    if not args.no_dashboard:
        dashboard = DashboardServer(
            runtime, broker, serial_stub, os.path.join(HERE, 'web'),
            host=args.http_host, port=args.http_port,
            main_status=(main_process.status if main_process else None),
            box_info=box_info, simulator=simulator, run_dir=run_dir)
        if dashboard.start():
            log('看板: http://%s:%d/' % (args.http_host, args.http_port))
        else:
            log('看板启动失败: %s' % dashboard.error)

    print('=' * 66)
    print(' 洗车机 PLC 模拟器')
    print(' 机型      %s（%s）' % (simulator.title, simulator.key))
    print(' 梯形图    %s' % simulator.ladder_name)
    print(' 扫描      %d ms / 周期，虚拟倍速 %.1fx（虚拟步长 %.0f ms）'
          % (scan_ms, time_scale, scan_ms * time_scale))
    print(' 串口      %s' % (args.pty if serial_stub else '未启用'))
    print(' MQTT      0.0.0.0:%d%s' % (args.broker_port, '' if broker and broker.running else '（未启用）'))
    print(' 看板      %s' % ('http://%s:%d/' % (args.http_host, args.http_port)
                            if dashboard else '未启用'))
    print(' 运行目录  %s' % run_dir)
    if box_info:
        print(' 盒号      %s' % (box_info.get('boxNo') or '-'))
    print(' READY http://%s:%d/' % (args.http_host, args.http_port))
    print('=' * 66, flush=True)

    if args.open:
        try:
            webbrowser.open('http://%s:%d/' % (args.http_host, args.http_port))
        except Exception:
            pass

    stopping = {'flag': False}

    def shutdown(signum=None, frame=None):
        if stopping['flag']:
            return
        stopping['flag'] = True
        log('收工中…')
        runtime.stop()
        if main_process:
            main_process.stop()
        if dashboard:
            dashboard.stop()
        if broker:
            broker.stop()
        if serial_stub:
            serial_stub.stop()

    signal.signal(signal.SIGINT, shutdown)
    try:
        signal.signal(signal.SIGTERM, shutdown)
    except (AttributeError, ValueError):
        pass

    # 主程序退出不带走模拟器：现场调的时候经常只重启盒子程序
    while not stopping['flag']:
        time.sleep(0.3)

    return 0


class _PresetSimulator:
    """把现场预设钉进机型，其它行为原样转发（未定义的属性一律透传）。"""

    def __init__(self, inner, preset):
        self._inner = inner
        self._preset = preset

    def __getattr__(self, name):
        return getattr(self._inner, name)

    def build(self):
        return self._inner.build(preset=self._preset)


if __name__ == '__main__':
    sys.exit(main())
