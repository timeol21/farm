"""新机型的最小实现（复制后按 TODO 改）。

需要提供四样东西：
    1. ladder_path  梯形图 CSV（GX Works 导出，UTF-16 TSV）
    2. build()      造一台虚拟 PLC：装载梯形图 + 补齐现场常态输入
    3. plant()      输出 Y → 机构动作 → 反馈 X（每个扫描周期一次）
    4. flow()       工序表：工序号的每个取值叫什么、往哪个方向走

`transport` 说明这台机器怎么跟盒子通讯：
    'fx-serial'   三菱 FX 编程口（PTY 冒充串口），core/serial_stub.py 已实现
    'modbus-tcp'  需要自己实现 core 里的 Modbus 桩，然后在 start.py 里接上
"""

from simulators.base import SimulatorBase, docs_path


class TemplateSimulator(SimulatorBase):
    key = 'template'                       # TODO 机型短名（命令行 --plc 用它）
    title = '模板机型'                      # TODO 看板标题
    note = '复制 _template 后改这里'
    ladder_path = docs_path('<机型目录>', '梯形图.csv')   # TODO 资料放 docs/<机型目录>/
    transport = 'fx-serial'
    step_device = 'D142'                   # TODO 表示"当前工序"的软元件
    fault_device = 'D2'                    # TODO 故障码软元件，没有就设成 ''

    def build(self, preset=None):
        # TODO 装载梯形图，并把现场常态输入写进 ladder.X / ladder.M / ladder.D
        #     参考 simulators/haitian/plant.py 的 build()
        ladder = super().build(preset)
        return ladder

    def new_state(self):
        # TODO 物理模型的内部状态（台车位置、刷子行程……）
        return {}

    def plant(self, ladder, state):
        # TODO 把 ladder.Y 的动作翻译成 ladder.X 的反馈
        #     不写也能跑，但机器会在第一个"等反馈"的地方停住
        pass

    def flow(self):
        # TODO 按洗车先后顺序列全，value 必须和 step_device 里出现的值一致
        return [
            {'value': 0, 'title': '初始', 'motion': 'idle', 'sStep': 'S0', 'detail': ''},
            {'value': 1, 'title': '就绪', 'motion': 'idle', 'sStep': 'S1', 'detail': ''},
            {'value': 10, 'title': '洗车结束', 'motion': 'end', 'sStep': 'S15', 'detail': ''},
        ]

    def fault_name(self, code):
        # TODO 故障码 -> 说明（可留空，看板会显示"未收录"）
        return ''

    def apply_precondition(self, action, runtime):
        # TODO 前置检查按钮对应的"外部动作"（只改现场输入，不写命令位）
        # action 名字来自 preconditions 里的 action 字段
        return False

    def device_notes(self):
        # TODO 关键软元件注解 {名字: 说明}
        return {'D142': '洗车工序号'}


SIMULATOR = TemplateSimulator
