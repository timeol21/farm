"""海天洗车机机型定义：把梯形图、物理模型、工序表拼成一个模拟器。"""

from simulators.base import SimulatorBase, docs_path

from . import machine, plant


class HaitianSimulator(SimulatorBase):
    key = 'haitian'
    title = '海天洗车机'
    note = 'FX3U 梯形图，对应 GX Works2 工程「金达路一号停车场洗车机」'
    ladder_path = docs_path('海天', '海天洗车机.csv')
    step_device = 'D142'
    fault_device = 'D2'
    services = machine.SERVICES
    key_devices = machine.KEY_DEVICES
    ready_signal = machine.READY_SIGNAL
    preconditions = machine.PRECONDITIONS

    def __init__(self):
        self._flow = [
            {
                'value': value,
                'title': machine.STEP_NAME[value],
                'motion': machine.STEP_MOTION.get(value, 'idle'),
                'sStep': machine.STEP_TABLE.get(value, 'S0'),
                'detail': machine.STEP_DETAIL.get(value, ''),
            }
            for value in sorted(machine.STEP_NAME)
        ]

    # ---------------------------------------------------------------- 装配
    def build(self, preset=None):
        return plant.build(self.ladder_path, preset=preset)

    def new_state(self):
        return plant.new_state()

    def plant(self, ladder, state):
        plant.plant(ladder, state)

    def flow(self):
        return self._flow

    def fault_name(self, code):
        return machine.FAULT_CODES.get(int(code), '')

    def apply_precondition(self, action, runtime):
        """车辆到位 / 松开防撞杆 / 车高达标 —— 只改现场输入。"""
        if action == 'carArrive':
            with runtime.lock:
                for key in ('pos', 'pulse', 'brush_pos'):
                    if key in runtime.plant_state:
                        runtime.plant_state[key] = 0
                runtime._write('M', 515, 1)
            for name in ('X24', 'X3', 'X75'):
                runtime.clear_overrides(name)
            return True
        if action in ('barLeft', 'barRight', 'heightOk'):
            device = {'barLeft': 'X62', 'barRight': 'X63', 'heightOk': 'X46'}[action]
            runtime.clear_overrides(device)
            with runtime.lock:
                runtime._write(*runtime._parse_name(device), 1)
            return True
        return False

    def device_notes(self):
        return machine.DEVICE_NOTES


SIMULATOR = HaitianSimulator
