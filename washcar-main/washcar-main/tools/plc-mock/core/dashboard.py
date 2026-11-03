"""网页看板 + JSON API。

看板是纯静态的（web/ 三个文件，无构建步骤），数据从这几个接口拿：

    GET  /api/bootstrap            机型、盒号、主题、软元件目录（首屏拉一次）
    POST /api/state                运行状态 + 指定软元件的当前值（看板每 300ms 拉）
    GET  /api/mqtt?since=<id>      broker 收发记录（增量）
    POST /api/mqtt/publish         手动下发任意主题/负载
    POST /api/control              暂停 / 单步 / 调速 / 强制写入 / 清轨迹

服务用标准库 http.server，不引第三方依赖 —— 现场盒子只要能跑 python3 就能开会话。
"""

import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

CONTENT_TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'application/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon',
    '.json': 'application/json; charset=utf-8',
}


class DashboardServer:
    def __init__(self, runtime, broker, serial_stub, web_dir, host='127.0.0.1', port=18081,
                 main_status=None, box_info=None, simulator=None, run_dir=''):
        self.runtime = runtime
        self.broker = broker
        self.serial_stub = serial_stub
        self.web_dir = os.path.abspath(web_dir)
        self.host = host
        self.port = port
        self.main_status = main_status or (lambda: {'enabled': False})
        self.box_info = box_info or {}
        self.simulator = simulator or runtime.simulator
        self.run_dir = run_dir
        self.httpd = None
        self.error = ''
        self._thread = None

    # ---------------------------------------------------------------- 启动
    def start(self):
        app = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.1'

            def log_message(self, fmt, *args):        # 静音：访问日志会把控制台刷满
                pass

            def do_GET(self):
                app._handle(self, 'GET')

            def do_POST(self):
                app._handle(self, 'POST')

        try:
            self.httpd = ThreadingHTTPServer((self.host, self.port), Handler)
        except OSError as exc:
            self.error = '看板端口 %d 绑定失败: %s' % (self.port, exc)
            return False
        self._thread = threading.Thread(target=self.httpd.serve_forever, name='dashboard', daemon=True)
        self._thread.start()
        return True

    def stop(self):
        if self.httpd:
            self.httpd.shutdown()

    # ---------------------------------------------------------------- 路由
    def _handle(self, handler, method):
        parsed = urlparse(handler.path)
        path = parsed.path
        query = parse_qs(parsed.query)
        try:
            if method == 'GET' and path in ('/', '/index.html'):
                return self._send_file(handler, 'index.html')
            if method == 'GET' and path.startswith('/static/'):
                return self._send_file(handler, path[len('/static/'):])
            if method == 'GET' and path == '/api/bootstrap':
                return self._json(handler, self._bootstrap())
            if method == 'GET' and path == '/api/state':
                return self._json(handler, self._state({}))
            if method == 'POST' and path == '/api/state':
                return self._json(handler, self._state(self._read_json(handler)))
            if method == 'GET' and path == '/api/mqtt':
                return self._json(handler, self._mqtt(query))
            if method == 'POST' and path == '/api/mqtt/publish':
                return self._json(handler, self._publish(self._read_json(handler)))
            if method == 'POST' and path == '/api/control':
                return self._json(handler, self._control(self._read_json(handler)))
            if path == '/api/health':
                return self._json(handler, {'ok': True, 'ts': time.time()})
            return self._json(handler, {'ok': False, 'error': '没有这个接口: %s' % path}, 404)
        except Exception as exc:                      # 一个接口出错不该让看板白屏
            return self._json(handler, {'ok': False, 'error': '%s: %s' % (type(exc).__name__, exc)}, 500)

    # ---------------------------------------------------------------- 数据
    def _state(self, body):
        status = self.runtime.status()
        watch = body.get('watch') if isinstance(body, dict) else None
        if watch:
            status['values'] = self.runtime.values([name for name in watch if name])
        status['serial'] = self.serial_stub.stats() if self.serial_stub else {'link': '', 'reads': 0, 'writes': 0}
        if self.broker:
            status['broker'] = self.broker.stats()
            status['broker']['clientList'] = self.broker.clients()
        else:
            status['broker'] = {'running': False, 'clientList': []}
        status['main'] = self.main_status()
        status['box'] = self.box_info
        status['ts'] = round(time.time(), 3)
        return status

    def _bootstrap(self):
        notes = self.simulator.device_notes()
        return {
            'sim': {
                'key': self.simulator.key,
                'title': self.simulator.title,
                'note': self.simulator.note,
                'ladder': self.simulator.ladder_name,
                'stepDevice': self.simulator.step_device,
                'faultDevice': self.simulator.fault_device,
                'readySignal': getattr(self.simulator, 'ready_signal', ''),
            },
            'box': self.box_info,
            'runDir': self.run_dir,
            'pid': os.getpid(),
            'keyPoints': [{'name': name, 'note': notes.get(name, '')}
                          for name in self.simulator.key_points()],
            'services': getattr(self.simulator, 'services', {}) or {},
            'devices': self.runtime.catalog.dump(),
            'deviceSummary': self.runtime.catalog.summary(),
            'flow': self.simulator.flow(),
            'serial': self.serial_stub.stats() if self.serial_stub else {},
            'broker': self.broker.stats() if self.broker else {'running': False},
            'main': self.main_status(),
        }

    def _mqtt(self, query):
        def first(name, default=''):
            values = query.get(name)
            return values[0] if values else default

        since = int(first('since', '0') or 0)
        limit = min(int(first('limit', '200') or 200), 800)
        direction = first('direction', '')
        keyword = first('q', '').strip()
        return {
            'messages': self.broker.messages(since=since, limit=limit,
                                             direction=direction or None,
                                             keyword=keyword or None),
            'topics': self.broker.topics(),
            'clients': self.broker.clients(),
            'stats': self.broker.stats(),
        }

    def _publish(self, body):
        topic = str(body.get('topic', '')).strip()
        payload = body.get('payload', '')
        qos = int(body.get('qos', 0) or 0)
        if not topic:
            return {'ok': False, 'error': '主题不能为空'}
        if isinstance(payload, (dict, list)):
            payload = json.dumps(payload, ensure_ascii=False)
        delivered = self.broker.publish(topic, str(payload), qos=qos, source='dashboard')
        return {'ok': True, 'delivered': delivered, 'topic': topic, 'qos': qos}

    # ---------------------------------------------------------------- 命令路由
    # 盒子在跑（broker 上有订阅者）时，按钮必须走盒子的命令入口（MQTT），
    # 否则就是绕过准入直接写 PLC 点 —— 和项目行为不一致。
    # 没有盒子时才退化为"直接写点"，并在返回值里标明 via=plc。
    def _command_topic(self):
        topics = (self.box_info or {}).get('topics') or {}
        template = topics.get('command') or ''
        box_no = (self.box_info or {}).get('boxNo') or ''
        return template.replace('{boxNo}', box_no)

    def _box_online(self):
        if not self.broker:
            return False
        try:
            return any(client.get('subscriptions') for client in self.broker.clients())
        except Exception:
            return False

    def _publish_command(self, payload):
        topic = self._command_topic()
        if not topic:
            return 0
        return self.broker.publish(topic, json.dumps(payload, ensure_ascii=False), source='dashboard')

    def _demo_via_mqtt(self, action):
        def run():
            if action == 'stop':
                self._publish_command({'stop': 1})
            elif action == 'reset':
                self._publish_command({'reset': 1})
            elif action == 'start':
                self._publish_command({'Manual start': 1})

        threading.Thread(target=run, name='demo-' + action, daemon=True).start()

    def _control(self, body):
        action = str(body.get('action', ''))
        if action == 'pause':
            self.runtime.pause()
        elif action == 'resume':
            self.runtime.resume()
        elif action == 'step':
            self.runtime.step_once()
        elif action == 'speed':
            self.runtime.set_speed(float(body.get('timeScale', 1)))
        elif action == 'override':
            self.runtime.set_override(str(body.get('device', '')), int(body.get('value', 0)))
        elif action == 'clear-overrides':
            self.runtime.clear_overrides(body.get('device'))
        elif action == 'clear-trail':
            self.runtime.reset_trail()
        elif action in ('demo-start', 'demo-stop', 'demo-reset'):
            if self._box_online() and self._command_topic():
                self._demo_via_mqtt(action.split('-')[1])
                return {'ok': True, 'action': action, 'via': 'mqtt',
                        'status': self.runtime.status()}
            if action == 'demo-start':
                ok = self.runtime.demo_start()
            elif action == 'demo-stop':
                ok = self.runtime.demo_stop()
            else:
                ok = self.runtime.demo_reset()
            return {'ok': bool(ok), 'action': action, 'via': 'plc',
                    'error': '' if ok else '该机型没有定义这条命令的点位（services）',
                    'status': self.runtime.status()}
        elif action == 'precondition':
            ok = self.runtime.apply_precondition(str(body.get('key', '')))
            return {'ok': bool(ok), 'action': action,
                    'preconditions': self.runtime.preconditions(),
                    'status': self.runtime.status()}
        else:
            return {'ok': False, 'error': '没有这个操作: %s' % action}
        return {'ok': True, 'action': action, 'status': self.runtime.status()}

    # ---------------------------------------------------------------- 响应
    def _read_json(self, handler):
        length = int(handler.headers.get('Content-Length') or 0)
        if not length:
            return {}
        raw = handler.rfile.read(length)
        try:
            return json.loads(raw.decode('utf-8'))
        except (ValueError, UnicodeDecodeError):
            return {}

    def _json(self, handler, payload, status=200):
        data = json.dumps(payload, ensure_ascii=False).encode('utf-8')
        try:
            handler.send_response(status)
            handler.send_header('Content-Type', CONTENT_TYPES['.json'])
            handler.send_header('Content-Length', str(len(data)))
            handler.send_header('Cache-Control', 'no-store')
            handler.end_headers()
            handler.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _send_file(self, handler, relative):
        relative = relative.replace('\\', '/').lstrip('/')
        target = os.path.normpath(os.path.join(self.web_dir, relative))
        if not target.startswith(self.web_dir + os.sep) or not os.path.isfile(target):
            return self._json(handler, {'ok': False, 'error': '文件不存在: %s' % relative}, 404)
        extension = os.path.splitext(target)[1].lower()
        content_type = CONTENT_TYPES.get(extension, 'application/octet-stream')
        try:
            with open(target, 'rb') as handle:
                data = handle.read()
        except OSError:
            return self._json(handler, {'ok': False, 'error': '读不到: %s' % relative}, 500)
        try:
            handler.send_response(200)
            handler.send_header('Content-Type', content_type)
            handler.send_header('Content-Length', str(len(data)))
            handler.send_header('Cache-Control', 'no-cache')
            handler.end_headers()
            handler.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass
