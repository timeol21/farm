"""自建 MQTT 3.1.1 broker —— 本地测试服务器。

为什么不用 mosquitto：这里要的不只是转发，还要**把收发都摊开给人看**：
哪台客户端连上、订阅了什么、上行报文长什么样、手动下发的命令有没有送到。
所以 broker 自己带一个消息环形缓冲 + 订阅表，看板直接读内存。

支持（够洗车机盒子用）：
    CONNECT / CONNACK        SUBSCRIBE / SUBACK        UNSUBSCRIBE / UNSUBACK
    PUBLISH 双向（QoS 0/1）   PINGREQ / PINGRESP         DISCONNECT

不支持（也不打算支持）：retain、持久会话、QoS2、遗嘱消息、用户名密码校验。

下行一律按 QoS0 发：有些客户端对"带 QoS1 却没有 packet id"的报文会直接断链。
"""

import socket
import struct
import threading
import time
from collections import deque


class MqttBroker:
    def __init__(self, port=1883, host='0.0.0.0', log_path=None, logger=None,
                 max_messages=800):
        self.port = port
        self.host = host
        self.log_path = log_path
        self._logger = logger
        self._log_file = open(log_path, 'a', buffering=1) if log_path else None
        self._log_lock = threading.Lock()
        self._lock = threading.RLock()
        self._server = None
        self._thread = None
        self._subs = {}          # topic filter -> {conn: {'qos': n, 'client': {...}}}
        self._clients = {}       # conn -> {'addr': str, 'since': ts, 'clientId': str, 'subs': set()}
        self._messages = deque(maxlen=max_messages)
        self._next_id = 1
        self._next_client = 1
        self.running = False
        self.error = ''
        self.started_at = 0.0
        self.received = 0
        self.sent = 0
        self.last_up_at = 0.0

    # ---------------------------------------------------------------- 生命周期
    def start(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            server.bind((self.host, self.port))
        except OSError as exc:
            self.error = '端口 %d 绑定失败: %s' % (self.port, exc)
            self.log(self.error)
            try:
                server.close()
            except OSError:
                pass
            return False
        server.listen(16)
        self._server = server
        self.running = True
        self.started_at = time.time()
        self._thread = threading.Thread(target=self._accept_loop, name='mqtt-broker', daemon=True)
        self._thread.start()
        self.log('broker listening on %s:%d' % (self.host, self.port))
        return True

    def stop(self):
        self.running = False
        try:
            if self._server:
                self._server.close()
        except OSError:
            pass

    # ---------------------------------------------------------------- 记录
    def log(self, message):
        if self._log_file:
            with self._log_lock:
                try:
                    self._log_file.write('%.3f %s\n' % (time.time(), message))
                except ValueError:
                    pass
        if self._logger:
            self._logger(message)

    def _record(self, direction, topic, payload, qos=0, retain=False, source='', client=''):
        text = payload.decode('utf-8', 'replace') if isinstance(payload, bytes) else str(payload)
        with self._lock:
            message = {
                'id': self._next_id,
                'ts': round(time.time(), 3),
                'direction': direction,          # up = 盒子上行 / down = 测试服务器下发
                'topic': topic,
                'payload': text[:6000] + ('…' if len(text) > 6000 else ''),
                'size': len(payload),
                'qos': qos,
                'retain': bool(retain),
                'source': source,
                'client': client,
            }
            self._next_id += 1
            self._messages.append(message)
            return message

    # ---------------------------------------------------------------- 对外接口
    def publish(self, topic, payload, qos=0, retain=False, source='dashboard'):
        """测试服务器主动下发（等价于"云端发一条"）。返回送达到几个订阅者。"""
        if isinstance(payload, str):
            payload = payload.encode('utf-8')
        delivered = self._deliver(topic, payload)
        self.sent += 1
        self._record('down', topic, payload, qos, retain, source=source, client='test-server')
        self.log('INJECT -> %s %s' % (topic, payload.decode('utf-8', 'replace')))
        return delivered

    def messages(self, since=0, limit=200, direction=None, keyword=None):
        with self._lock:
            items = list(self._messages)
        if since:
            items = [item for item in items if item['id'] > since]
        if direction in ('up', 'down'):
            items = [item for item in items if item['direction'] == direction]
        if keyword:
            keyword = keyword.lower()
            items = [item for item in items
                     if keyword in item['topic'].lower() or keyword in item['payload'].lower()]
        return items[-limit:]

    def topics(self):
        """出现过的主题 + 订阅表，给看板做输入提示。"""
        with self._lock:
            seen = {}
            for item in self._messages:
                entry = seen.setdefault(item['topic'], {'topic': item['topic'], 'up': 0, 'down': 0})
                entry[item['direction']] += 1
            subscribed = [{'filter': filt, 'clients': len(conns)} for filt, conns in self._subs.items()]
        return {'seen': sorted(seen.values(), key=lambda x: -(x['up'] + x['down'])),
                'subscribed': sorted(subscribed, key=lambda x: x['filter'])}

    def clients(self):
        with self._lock:
            return [
                {'id': info['clientId'] or ('#%d' % info['id']),
                 'addr': info['addr'],
                 'since': info['since'],
                 'subscriptions': sorted(info['subs'])}
                for info in self._clients.values()
            ]

    def stats(self):
        with self._lock:
            return {
                'running': self.running,
                'host': self.host,
                'port': self.port,
                'error': self.error,
                'clients': len(self._clients),
                'subscriptions': len(self._subs),
                'received': self.received,
                'sent': self.sent,
                'lastUpAt': self.last_up_at,
                'uptime': round(time.time() - self.started_at, 1) if self.started_at else 0,
            }

    # ---------------------------------------------------------------- 连接处理
    def _accept_loop(self):
        while self.running:
            try:
                conn, addr = self._server.accept()
            except OSError:
                break
            threading.Thread(target=self._handle_client, args=(conn, addr), daemon=True).start()

    @staticmethod
    def _read_exact(sock, size):
        buffer = b''
        while len(buffer) < size:
            chunk = sock.recv(size - len(buffer))
            if not chunk:
                return None
            buffer += chunk
        return buffer

    @classmethod
    def _read_remaining_length(cls, sock):
        multiplier = 1
        value = 0
        while True:
            raw = sock.recv(1)
            if not raw:
                return None
            digit = raw[0]
            value += (digit & 127) * multiplier
            if not (digit & 0x80):
                return value
            multiplier *= 128
            if multiplier > 128 ** 3:
                return None

    @staticmethod
    def _encode_remaining_length(number):
        out = b''
        while True:
            digit = number % 128
            number //= 128
            if number > 0:
                digit |= 0x80
            out += bytes([digit])
            if number == 0:
                return out

    @staticmethod
    def _topic_matches(topic_filter, topic):
        """MQTT 通配符：+ 单层，# 多层。"""
        parts = topic_filter.split('/')
        tokens = topic.split('/')
        index = 0
        while index < len(parts):
            if parts[index] == '#':
                return True
            if index >= len(tokens):
                return False
            if parts[index] != '+' and parts[index] != tokens[index]:
                return False
            index += 1
        return index == len(tokens)

    @staticmethod
    def _parse_client_id(body):
        """从 CONNECT 报文里捞 client id（解析失败就返回空串，不影响连接）。"""
        try:
            name_len = struct.unpack('!H', body[0:2])[0]
            offset = 2 + name_len
            offset += 1                        # protocol level
            offset += 1                        # connect flags
            offset += 2                        # keepalive
            client_len = struct.unpack('!H', body[offset:offset + 2])[0]
            return body[offset + 2:offset + 2 + client_len].decode('utf-8', 'replace')
        except (struct.error, IndexError):
            return ''

    def _send(self, conn, data):
        entry = self._clients.get(conn)
        lock = entry.get('lock') if entry else None
        try:
            if lock:
                with lock:
                    conn.sendall(data)
            else:
                conn.sendall(data)
        except OSError:
            pass

    def _deliver(self, topic, payload):
        with self._lock:
            targets = [(conn, filt) for filt, conns in self._subs.items()
                       if self._topic_matches(filt, topic) for conn in conns]
        for conn, _filt in targets:
            body = struct.pack('!H', len(topic)) + topic.encode('utf-8') + payload
            self._send(conn, bytes([0x30]) + self._encode_remaining_length(len(body)) + body)
        return len(targets)

    def _handle_client(self, conn, addr):
        client = {'id': self._next_client, 'addr': '%s:%s' % (addr[0], addr[1]),
                  'since': round(time.time(), 3), 'clientId': '', 'subs': set(), 'lock': threading.Lock()}
        self._next_client += 1
        with self._lock:
            self._clients[conn] = client
        self.log('connect from %s' % (addr,))
        try:
            while True:
                header = conn.recv(1)
                if not header:
                    break
                packet_type = header[0] & 0xF0
                length = self._read_remaining_length(conn)
                if length is None:
                    break
                body = self._read_exact(conn, length) if length else b''
                if body is None:
                    break

                if packet_type == 0x10:                       # CONNECT
                    client['clientId'] = self._parse_client_id(body)
                    self._send(conn, bytes([0x20, 0x02, 0x00, 0x00]))
                    self.log('CONNECT ok client=%s' % (client['clientId'] or '(无)'))

                elif packet_type == 0x80:                      # SUBSCRIBE
                    packet_id = struct.unpack('!H', body[0:2])[0]
                    index = 2
                    filters = []
                    while index < len(body):
                        topic_len = struct.unpack('!H', body[index:index + 2])[0]
                        index += 2
                        topic_filter = body[index:index + topic_len].decode('utf-8', 'replace')
                        index += topic_len
                        qos = body[index] if index < len(body) else 0
                        index += 1
                        filters.append((topic_filter, qos))
                    with self._lock:
                        for topic_filter, qos in filters:
                            self._subs.setdefault(topic_filter, {})[conn] = {'qos': qos, 'client': client}
                            client['subs'].add(topic_filter)
                    payload = struct.pack('!H', packet_id) + bytes([len(filters)])
                    for topic_filter, qos in filters:
                        payload += struct.pack('!H', len(topic_filter)) + topic_filter.encode() + bytes([qos])
                    self._send(conn, bytes([0x90]) + self._encode_remaining_length(len(payload)) + payload)
                    self.log('SUBSCRIBE %s' % ', '.join(name for name, _ in filters))

                elif packet_type == 0xA0:                      # UNSUBSCRIBE
                    packet_id = struct.unpack('!H', body[0:2])[0]
                    index = 2
                    while index < len(body):
                        topic_len = struct.unpack('!H', body[index:index + 2])[0]
                        index += 2
                        topic_filter = body[index:index + topic_len].decode('utf-8', 'replace')
                        index += topic_len
                        with self._lock:
                            self._subs.get(topic_filter, {}).pop(conn, None)
                            if topic_filter in self._subs and not self._subs[topic_filter]:
                                del self._subs[topic_filter]
                        client['subs'].discard(topic_filter)
                    self._send(conn, bytes([0xB0, 0x02]) + struct.pack('!H', packet_id))
                    self.log('UNSUBSCRIBE')

                elif packet_type == 0x30:                      # PUBLISH（盒子上行）
                    qos = (header[0] & 0x06) >> 1
                    retain = bool(header[0] & 0x01)
                    topic_len = struct.unpack('!H', body[0:2])[0]
                    topic = body[2:2 + topic_len].decode('utf-8', 'replace')
                    offset = 2 + topic_len
                    packet_id = None
                    if qos > 0:
                        if len(body) < offset + 2:
                            break
                        packet_id = struct.unpack('!H', body[offset:offset + 2])[0]
                        offset += 2
                    payload = body[offset:]
                    if qos == 1:
                        self._send(conn, bytes([0x40, 0x02]) + struct.pack('!H', packet_id))
                    self.received += 1
                    self.last_up_at = time.time()
                    self._record('up', topic, payload, qos, retain,
                                 source=client['clientId'] or client['addr'],
                                 client=client['clientId'] or client['addr'])
                    self.log('PUB %s %s' % (topic, payload.decode('utf-8', 'replace')))
                    self._deliver(topic, payload)

                elif packet_type == 0xC0:                      # PINGREQ
                    self._send(conn, bytes([0xD0, 0x00]))

                elif packet_type == 0xE0:                      # DISCONNECT
                    break
        except OSError:
            pass
        finally:
            with self._lock:
                self._clients.pop(conn, None)
                for topic_filter in list(self._subs.keys()):
                    self._subs[topic_filter].pop(conn, None)
                    if not self._subs[topic_filter]:
                        del self._subs[topic_filter]
            try:
                conn.close()
            except OSError:
                pass
            self.log('disconnect %s' % (addr,))
