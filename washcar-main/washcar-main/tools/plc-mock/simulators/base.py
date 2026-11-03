"""机型扩展接口。

一个洗车机机型 = 一份梯形图 + 一套现场物理反馈 + 一张工序表。新机型照
`_template/` 复制一份、改四个东西就能接进来（见 docs/接入标准.md §7）：

    ladder_path     梯形图 CSV（GX Works 导出）
    build()         造一台虚拟 PLC（现场常态输入、只能外部给的参数）
    plant()         输出 Y → 机构动作 → 反馈 X，每个扫描周期跑一次
    flow()          工序表：D142（或别的步号软元件）的每个值叫什么、往哪个方向走

其余（扫描循环、串口桩、MQTT、看板）都是机型无关的，不用动。
"""

import os

from core.ladder import Ladder

# 机型资料目录（文档 + 梯形图 + 原始件）统一放在 tools/plc-mock/docs/<机型>/
# 机型包只放代码；ladder_path 指到 docs 里，保证"打开 docs/<机型> 就是完整资料"。
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def docs_path(*parts):
    return os.path.join(ROOT, 'docs', *parts)


class SimulatorBase:
    key = ''                 # 命令行/URL 用的短名
    title = ''               # 看板标题
    note = ''                # 一句话说明
    ladder_path = ''         # 梯形图 CSV 绝对路径
    transport = 'fx-serial'  # 通讯层：fx-serial（PTY 冒充 FX 编程口）；别的要自己在 core 里实现
    step_device = 'D142'     # 表示"当前工序"的软元件
    fault_device = 'D2'      # 故障码软元件（没有就留空）
    # 云端三类命令落到哪个软元件（看板"试跑一轮"用它模拟复位+启动）
    services = {}            # {'start','stop','clearStop','reset'} -> 软元件名
    ready_signal = ''        # 启动准入的代表信号（只用于展示/测试脚本等待）
    preconditions = ()       # 启动前置检查表，见 haitian/machine.py
    # 看板"关键点位"条固定展示的软元件（工序号/故障码会自动加在最前面）
    key_devices = ()

    # ---------------------------------------------------------------- 基础
    @property
    def ladder_name(self):
        return os.path.basename(self.ladder_path)

    def build(self, preset=None):
        """造一台虚拟 PLC。默认只装载梯形图，机型再补现场常态输入。"""
        ladder = Ladder(self.ladder_path)
        ladder.preset = None
        return ladder

    def new_state(self):
        """物理模型的内部状态（台车位置、刷子行程……）。"""
        return {}

    def plant(self, ladder, state):
        """每个扫描周期：把程序输出变成机构动作，再写回反馈输入 X。"""

    # ---------------------------------------------------------------- 工序
    def flow(self):
        """[{value, title, motion, sStep, detail}, ...]，按洗车先后顺序。"""
        return []

    def current_step(self, ladder):
        if not self.step_device:
            return None
        # 上电时梯形图还没写过这个寄存器，按 0 处理（海天 0=初始步）
        return ladder.D.get(int(self.step_device[1:]), 0)

    def step_title(self, value):
        for item in self.flow():
            if item['value'] == value:
                return item['title']
        return '未知(%s)' % value

    def flow_step(self, value):
        steps = self.flow()
        for index, item in enumerate(steps):
            if item['value'] == value:
                merged = dict(item)
                merged['index'] = index
                return merged
        return {'value': value, 'index': -1,
                'title': '未开始' if value is None else '未知(%s)' % value,
                'motion': 'idle', 'sStep': '-', 'detail': ''}

    # ---------------------------------------------------------------- 其它
    def alarms(self, ladder):
        """故障码。空字典 = 无故障。"""
        if not self.fault_device:
            return {}
        code = ladder.D.get(int(self.fault_device[1:]), 0) or 0
        if not code:
            return {}
        return {'code': code, 'name': self.fault_name(code), 'device': self.fault_device}

    def fault_name(self, code):
        return ''

    def apply_precondition(self, action, runtime):
        """执行一项前置检查的"外部动作"（看板按钮）。返回是否执行。

        只允许改现场输入（X/M 的常态值、plant 状态），**不许写命令位**
        （services 里的 start/stop/clearStop/reset 一律不碰）。
        动作名由 preconditions 里的 action 字段给出，机型自己解释。
        """
        return False

    def device_notes(self):
        """软元件注解 {名字: 说明}，看板里显示在频率表后面。"""
        return {}

    def key_points(self):
        """看板顶部"关键点位"要一直盯着的软元件（工序号、故障码排最前）。"""
        names = []
        for name in (self.step_device, self.fault_device) + tuple(self.key_devices):
            if name and name not in names:
                names.append(name)
        return names
