#!/usr/bin/env python3
"""自建 MQTT broker 的自测（不依赖第三方库，也不依赖盒子）。

覆盖看板依赖的四件事：
    1) 客户端能连上、订阅、收到 SUBACK
    2) 一个客户端发布 → 订阅者收到（QoS0 转发）
    3) QoS1 发布能收到 PUBACK（主程序用的就是这条）
    4) 测试服务器主动下发（看板"下发命令"按钮走这条）能送到订阅者，
       且内存里的报文记录同时有上行和下行

用法: python3 tests/broker_smoke.py     退出码 0 = 全过
"""

import os
import socket
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from core.broker import MqttBroker          # noqa: E402

RESULTS = []


def check(name, ok, detail=''):
    RESULTS.append((name, ok, detail))
    print('%-6s %-30s %s' % ('PASS' if ok else 'FAIL', name, detail))


def free_port():
    probe = socket.socket()
    probe.bind(('127.0.0.1', 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


def encode_length(number):
    out = b''
    while True:
        digit = number % 128
        number //= 128
        if number:
            digit |= 0x80
        out += bytes([digit])
        if not number:
            return out


class MiniClient:
    """够测试用的最小 MQTT 客户端（只实现本用例要用的报文）。"""

    def __init__(self, port, client_id):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=3)
        self.sock.settimeout(3)
        self.buffer = b''
        self.packet_id = 1
        self.client_id = client_id

    def _send(self, header, body):
        self.sock.sendall(bytes([header]) + encode_length(len(body)) + body)

    def _read_exact(self, size):
        while len(self.buffer) < size:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError('broker 断开了连接')
            self.buffer += chunk
        data, self.buffer = self.buffer[:size], self.buffer[size:]
        return data

    def read_packet(self):
        header = self._read_exact(1)[0]
        multiplier, length = 1, 0
        while True:
            digit = self._read_exact(1)[0]
            length += (digit & 127) * multiplier
            if not digit & 0x80:
                break
            multiplier *= 128
        return header, self._read_exact(length) if length else b''

    def expect(self, packet_type, timeout=3):
        """读到指定类型为止，跳过其它报文（PINGRESP 之类）。"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.sock.settimeout(max(0.05, deadline - time.time()))
            try:
                header, body = self.read_packet()
            except socket.timeout:
                return None
            if header & 0xF0 == packet_type:
                return header, body
        return None

    def connect(self):
        body = (b'\x00\x04MQTT' + bytes([4, 0x02]) + struct.pack('!H', 60)
                + struct.pack('!H', len(self.client_id)) + self.client_id.encode())
        self._send(0x10, body)
        return self.expect(0x20) is not None

    def subscribe(self, topic_filter, qos=0):
        body = (struct.pack('!H', self.packet_id)
                + struct.pack('!H', len(topic_filter)) + topic_filter.encode() + bytes([qos]))
        self.packet_id += 1
        self._send(0x82, body)
        return self.expect(0x90) is not None

    def unsubscribe(self, topic_filter):
        body = (struct.pack('!H', self.packet_id)
                + struct.pack('!H', len(topic_filter)) + topic_filter.encode())
        self.packet_id += 1
        self._send(0xA2, body)
        return self.expect(0xB0) is not None

    def publish(self, topic, payload, qos=0):
        # MQTT: 可变头 = 主题 + packet id（QoS>0 时），负载在最后
        body = struct.pack('!H', len(topic)) + topic.encode()
        if qos:
            body += struct.pack('!H', self.packet_id)
            self.packet_id += 1
        body += payload.encode()
        self._send(0x30 | (qos << 1), body)

    def close(self):
        try:
            self._send(0xE0, b'')
            self.sock.close()
        except OSError:
            pass


def parse_publish(body):
    topic_len = struct.unpack('!H', body[0:2])[0]
    topic = body[2:2 + topic_len].decode()
    offset = 2 + topic_len
    qos = 0            # broker 下行统一按 QoS0 发，没有 packet id
    return topic, body[offset:].decode(), qos


def main():
    port = free_port()
    broker = MqttBroker(port=port, host='127.0.0.1')
    if not broker.start():
        print('broker 起不来: %s' % broker.error)
        return 1
    time.sleep(0.2)

    box = MiniClient(port, 'box-300823040333')
    console = MiniClient(port, 'test-console')
    try:
        check('两个客户端连上', box.connect() and console.connect())

        check('订阅 flexem/#（收全部上行）', console.subscribe('flexem/#'))
        check('订阅后 broker 记到订阅表',
              any(item['filter'] == 'flexem/#' for item in broker.topics()['subscribed']))

        # 1) 盒子上行（QoS0）→ 订阅者收到
        box.publish('flexem/300823040333/commandreply', '{"message":"success"}')
        packet = console.expect(0x30)
        topic, payload, _ = parse_publish(packet[1]) if packet else ('', '', 0)
        check('上行 QoS0 转发给订阅者',
              topic == 'flexem/300823040333/commandreply' and payload == '{"message":"success"}',
              '%s %s' % (topic, payload))

        # 2) QoS1 发布 → PUBACK，并且照样转发给订阅者
        box.publish('flexem/300823040333/readdata', '{"D142":3}', qos=1)
        check('上行 QoS1 收到 PUBACK', box.expect(0x40) is not None)
        forwarded = console.expect(0x30)
        topic, payload, _ = parse_publish(forwarded[1]) if forwarded else ('', '', 0)
        check('QoS1 上行也转发（QoS 降为 0）',
              topic == 'flexem/300823040333/readdata' and payload == '{"D142":3}',
              repr(payload))

        # 3) 测试服务器主动下发（看板按钮）→ 订阅者收到
        delivered = broker.publish('flexem/300823040333/command', '{"Manual start":1}')
        packet = console.expect(0x30)
        topic, payload, _ = parse_publish(packet[1]) if packet else ('', '', 0)
        check('看板下发能送到订阅者',
              delivered == 1 and topic == 'flexem/300823040333/command'
              and payload == '{"Manual start":1}',
              'delivered=%d %s %s' % (delivered, topic, payload))

        # 4) 收发都进内存记录（看板的报文列表靠它）
        messages = broker.messages(limit=50)
        ups = [m for m in messages if m['direction'] == 'up']
        downs = [m for m in messages if m['direction'] == 'down']
        check('报文记录分上行/下行', len(ups) >= 2 and len(downs) == 1,
              'up=%d down=%d' % (len(ups), len(downs)))
        check('报文记录带主题和负载',
              all(m['topic'] and m['payload'] for m in messages))

        # 5) 退订后不再收到
        check('退订返回 UNSUBACK', console.unsubscribe('flexem/#'))
        broker.publish('flexem/300823040333/command', '{"stop":1}')
        check('退订后收不到', console.expect(0x30, timeout=0.6) is None)

        check('统计: 收到/下发计数', broker.stats()['received'] >= 2 and broker.stats()['sent'] >= 2,
              str(broker.stats()))
    finally:
        box.close()
        console.close()
        broker.stop()

    failed = [name for name, ok, _ in RESULTS if not ok]
    print()
    print('%d/%d 通过' % (len(RESULTS) - len(failed), len(RESULTS)))
    if failed:
        print('未通过:', ', '.join(failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
