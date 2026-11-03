#!/usr/bin/env python3
"""洗车流程演练：在虚拟 PLC 上按真实时序跑一遍（上电 → 复位 → 就绪 → 启动 → 9 个工序）。

这是**测试**，不是模拟器本体：模拟器本体在 core/ + simulators/，这里只负责"跑一轮看看对不对"。

    python3 tests/wash_cycle.py                 理想机基线（快速路径）
    python3 tests/wash_cycle.py --field         现场模式：复现 2026-10-22 卡在洗车头那台
    python3 tests/wash_cycle.py --legacy        强制逐条解释器（性能 A/B 对照）
    python3 tests/wash_cycle.py <csv>           换一份梯形图

判定以「S 步轨迹 + 无故障收尾」为准：D142 只是云端能采到的工序号快照，
工序短于一个扫描周期时真机同样采不到，不能拿它当走没走过的判据。

退出码：0 = PASS，2 = PARTIAL（启动链通了但没走完）。
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
for path in (ROOT, HERE):
    if path not in sys.path:
        sys.path.insert(0, path)

from core.ladder import Ladder                             # noqa: E402,F401
from simulators.haitian import machine                     # noqa: E402
from simulators.haitian import plant as field              # noqa: E402
from simulators.haitian.simulator import HaitianSimulator  # noqa: E402
from devtools import diag                                  # noqa: E402

DEFAULT_CSV = HaitianSimulator().ladder_path
CSV = DEFAULT_CSV
PRESETS = field.FIELD_PRESETS
build = field.build
new_state = field.new_state
plant = field.plant

# 工序表（AGENTS.md §3.4）
STEP_NAME = machine.STEP_NAME
STEP_D142 = machine.STEP_D142
WASH_STEPS = machine.WASH_STEPS
EXPECTED = [STEP_D142[step] for step in WASH_STEPS]


def _parse_args(argv):
    args = list(argv)
    legacy = '--legacy' in args
    if legacy:
        args.remove('--legacy')
    field_mode = '--field' in args
    preset_name = None
    if field_mode:
        args.remove('--field')
        if args and not args[0].lower().endswith('.csv'):
            preset_name = args.pop(0)
    csv_path = args[0] if args else CSV
    return csv_path, field_mode, preset_name, legacy


class Trace:
    """记录**实际发生过**的工序，不是"程序里写了哪些 MOV"。

    D142 的映射在主程序的公共代码里（pc=34-53，每扫描都执行），每条 MOV 都被
    `LD S<步>` 条件化，但条件不成立时指令照样会被解释器看到 —— 钩 MOV 会得到假阳性。
    这里改用两个真实信号：
      · 有效 D142：每扫描结束后读 ladder.D[142] 的实际值（盒子/HMI 看到的就是它）
      · 激活过的 S 步：只记线圈真正导通（S[n] 被置 1）的步
    """

    def __init__(self, ladder):
        self.ladder = ladder
        self.d142_trail = []      # [(D142 值, 快照)]，相邻相同的值只记一次
        self.step_trail = []      # [S 步号] 按激活先后
        self._steps = set()
        if getattr(ladder, 'supports_step_hook', False):
            ladder.on_step = self._enter_step
        else:
            original = ladder.exec_one

            def traced(op, args, pc=0, block=None):
                result = original(op, args, pc, block)
                if op in ('SET', 'OUT') and args:
                    target = args[0].upper()
                    if target.startswith('S') and target[1:].isdigit():
                        number = int(target[1:])
                        if ladder.S.get(number) and number not in self._steps:
                            self._steps.add(number)
                            self.step_trail.append(number)
                return result

            ladder.exec_one = traced

    def _enter_step(self, number):
        if number not in self._steps:
            self._steps.add(number)
            self.step_trail.append(number)

    def sample(self, ladder, tag):
        """D142 变了才做快照 —— 每扫描拼一次 snapshot 很贵，而快照只在跳变时用。"""
        value = ladder.D.get(142)
        if not self.d142_trail or self.d142_trail[-1][0] != value:
            self.d142_trail.append((value, snapshot(ladder, tag)))

    @property
    def d142_values(self):
        return [value for value, _ in self.d142_trail]


def snapshot(ladder, tag):
    return ('%s S=%s D142=%s C4=%s C5=%s C220=%s C222=%s M139=%s M141=%s M140=%s M142=%s '
            '| M1=%s M247=%s M7=%s M500=%s M515=%s M14=%s M8=%s D2=%s D136=%s D114=%s '
            'D14=%s D220=%s D76=%s T4=%s T23=%s T61=%s T7=%s Y000=%s Y030=%s') % (
        tag, sorted(n for n, v in ladder.S.items() if v), ladder.D.get(142),
        ladder.C.get(4), ladder.C.get(5), ladder.C.get(220), ladder.C.get(222),
        ladder.M.get(139), ladder.M.get(141), ladder.M.get(140), ladder.M.get(142),
        ladder.M.get(1), ladder.M.get(247), ladder.M.get(7), ladder.M.get(500),
        ladder.M.get(515), ladder.M.get(14), ladder.M.get(8),
        ladder.D.get(2), ladder.D.get(136), ladder.D.get(114),
        ladder.D.get(14), ladder.D.get(220), ladder.D.get(76),
        ladder.T.get(4), ladder.T.get(23),
        ladder.T.get(61), ladder.T.get(7), ladder.Y.get(0), ladder.Y.get(30))


def run(ladder, state, trace, limit=12000, echo=None, on_scan=None):
    """跑完 上电 → 复位 → 就绪 → 启动 → 洗车，返回阶段列表。

    limit 是**扫描次数上限**（卡死保护），不是模拟时长。正常走完 9 个工序约 8000 扫描。
    on_scan(ladder, state, index) 在每扫描**之前**调用，返回真值立刻停机（参数扫描用）。
    """

    def emit(text):
        if echo is not None:
            echo(text)

    phases = []

    # 1) 上电
    for _ in range(4):
        field.plant(ladder, state)
        ladder.scan(0.05)
    trace.sample(ladder, '上电')
    phases.append(('上电', snapshot(ladder, '上电')))

    # 2) 复位（M100 是盒子的 reset 服务写的位）再松开 → S0 → S1 就绪
    ladder.M[100] = 1
    for _ in range(4):
        field.plant(ladder, state)
        ladder.scan(0.05)
    ladder.M[100] = 0
    for _ in range(12):
        field.plant(ladder, state)
        ladder.scan(0.05)
    trace.sample(ladder, '复位后')
    phases.append(('复位后', snapshot(ladder, '复位后')))

    # 3) 启动（X45）
    ladder.X[45] = 1
    for index in range(limit):
        field.plant(ladder, state)
        if on_scan is not None and on_scan(ladder, state, index):
            break
        ladder.scan(0.05)
        before = len(trace.d142_trail)
        trace.sample(ladder, '扫描')
        if len(trace.d142_trail) > before:
            value = trace.d142_trail[-1][0]
            phases.append(('工序 %s' % value, trace.d142_trail[-1][1]))
            emit(trace.d142_trail[-1][1])
        if trace.d142_values and trace.d142_values[-1] == 10:
            break
    ladder.X[45] = 0

    if 1 not in trace.d142_values:
        emit('  [未进入就绪态] D142=%s，现场诊断:' % ladder.D.get(142))
        diag.diagnose(ladder, focus='S0')
    return phases


def _subsequence(expected, actual):
    """expected 是不是 actual 的子序列（保序）。"""
    index = 0
    for value in actual:
        if index < len(expected) and value == expected[index]:
            index += 1
    return index == len(expected)


def main():
    csv_path, field_mode, preset_name, legacy = _parse_args(sys.argv[1:])
    preset = PRESETS[preset_name or 'stall-s3-t99'] if field_mode else None
    ladder = field.build(csv_path, preset=preset)
    ladder.use_fast = not legacy
    state = field.new_state()
    trace = Trace(ladder)
    run(ladder, state, trace, echo=print)

    print()
    print('模式: %s%s' % ('现场模式(%s)' % (preset_name or 'stall-s3-t99') if field_mode else '理想机基线',
                         '  [逐条解释/--legacy]' if legacy else ''))
    print('有效 D142 轨迹(扫描后实际值):',
          ' → '.join(str(v) if v else 'None' for v in trace.d142_values))
    print('激活过的 S 步:', ' → '.join('S%d' % step for step in trace.step_trail))
    print('期望的 9 个洗车工序:',
          ' → '.join('S%d(D142=%d)' % (step, STEP_D142[step]) for step in WASH_STEPS))

    steps_ok = _subsequence(WASH_STEPS, trace.step_trail)
    d142_ok = _subsequence(EXPECTED, trace.d142_values)
    missing_steps = [step for step in WASH_STEPS if step not in trace.step_trail]
    missing_d142 = [value for value in EXPECTED if value not in trace.d142_values]
    fault = ladder.D.get(2)
    finished = 15 in trace.step_trail and not fault

    print()
    print('  · S 步轨迹覆盖 9 个洗车工序   %s' % ('OK' if steps_ok else '未达成'))
    print('  · 洗车走到 S15 结束且无故障   %s' % ('OK' if finished else '未达成'
          + (' (末态 D2=%s)' % fault if fault else '')))
    print('  · 云端可见的 D142 覆盖       %s' % ('OK' if d142_ok else '未达成'))
    if missing_steps:
        print('  · 没激活的 S 步: %s' % ', '.join('S%d' % step for step in missing_steps))
    if missing_d142:
        print('  · D142 没采到的工序值: %s' % ', '.join(str(value) for value in missing_d142))
        print('    (工序在一次扫描内就走完 -> 公共代码里的 D142 映射来不及写它。'
              '真 PLC 同样如此, 云端按采样周期也看不到这个工序号)')
    print('  · 末态 %s' % snapshot(ladder, '结束'))

    if field_mode:
        # 现场模式的判据是"现场症状复现"，不是"洗车成功"：
        # 进过 S3 / 没走出去 / D2=25 / 收在 S15
        entered_s3 = 3 in trace.step_trail
        left_s3 = 4 in trace.step_trail
        d2_25 = fault == 25
        ended_s15 = 15 in trace.step_trail
        ok = entered_s3 and not left_s3 and d2_25 and ended_s15
        print()
        print('  · 进过洗车头 S3(D142=2)      %s' % ('OK' if entered_s3 else '未达成'))
        print('  · 卡在洗车头没走出去           %s' % ('OK' if not left_s3 else '未达成'))
        print('  · D2=25(T99 原地停留 40 秒)   %s' % ('OK' if d2_25 else '未达成'))
        print('  · 最终收在 S15(洗车结束)      %s' % ('OK' if ended_s15 else '未达成'))
        print()
        print('判定: %s' % ('PASS(现场症状已复现)' if ok else 'PARTIAL(现场症状没复现)'))
        return 0 if ok else 2

    ok = steps_ok and finished
    print()
    print('判定: %s' % ('PASS(9 个洗车工序全部走通, 无故障收尾)' if ok
                        else 'PARTIAL(启动链已走通,未走完全部工序)'))
    return 0 if ok else 2


if __name__ == '__main__':
    sys.exit(main())
