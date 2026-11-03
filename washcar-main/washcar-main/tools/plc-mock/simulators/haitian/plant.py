"""海天洗车机的现场物理模型（补梯形图里没有的那一半）。

**为什么需要它**：梯形图只描述控制逻辑，机构的物理反馈（车到位、刷子行程）在程序里
没有软元件能表示。裸跑梯形图时这些反馈永远不成立，机器只能走到 D142=3 就停。
这里按「输出 Y → 机构动作 → 反馈输入 X」补上，顺序和真机一致：先算物理，再扫梯形图。

每一条 Y↔X 配对都是**从梯形图反推**的，不是照 IO 表猜的（位置都是主程序 CSV 指令序号）：

刷子开合（三处互相印证）
    pc=1417-1451  Y014 ← M61 & !X010 & !Y015   （开左刷，直到 X010 开位到）
                  Y015 ← M62 & !X011 & !Y014   （合左刷，直到 X011 合位到）
                  Y020 ← M65 & !X014 & !Y021   （开右刷，直到 X014 开位到）
                  Y021 ← M66 & !X015 & !Y020   （合右刷，直到 X015 合位到）
    子程序 P3     同一条链的另一种写法（`AND ANI X010`）
    故障判定 pc=4677-4688  D2=32 ← (T102 & X010) | (T103 & X014) | (T110 & X011) | (T111 & X015)
    ⇒ 合左刷（Y015）期间 X010 必须为 0；开左刷（Y014）期间 X011 必须为 0。
      开阀就要立刻脱离原位，否则 5 秒后报故障码 25。

顶刷
    pc=1042-1045  Y007 ← M350 & !T124 ；pc=1050-1053 T124 ← (M350|T124) & X005
    pc=1082-1086  Y011 ← M344 & !T121 & !X006 ；pc=1091-1094 T121 ← ... & X006
    pc=4315-4321  X005 → RST C222；C222 数 X007 的每个边沿
    ⇒ X005=顶刷上位，X006=顶刷下限位，X007=顶刷上下脉冲

台车
    pc=4332-4334  `LDP X002 / ORF X002 / OUT C220 D534`  ⇒ 行走脉冲两个边沿都计数
    pc=4335-4340  `LDP X075 / ANI Y030 / ANI S2 / ANI S3 / ANI S20 / RST C220`
    pc=2653-2654  `LDD>= C220 D30 / OUT M338`（S6 步 Y031 的使能）
"""

from core.ladder import Ladder

from . import machine

# 台车轨道：前位在第 1100 个行走脉冲处（pc=1639 `MOV K1100 D34`，pc=2282 `LDD>= C220 D34`）。
# **X001（前位）在本模拟里不置位**：梯形图对它有两处互斥约束
#   pc=4703-4705  前位闭合且 C220>=850 ⇒ 报故障 34
#   pc=2361-2379  前位闭合且 C220<=630  ⇒ 同样报故障 34
#   ⇒ 只允许 630<C220<850 闭合；而 S4 要走到 C220>=D34=1100 才转 S5，
#     两者在梯形图里本身就不自洽。现场需确认真实安装位置。
FRONT_LIMIT = 999999

# D30 = 洗车头位置的行走脉冲数，取 2（本模型 S3 结束时 C220=2）。
#   依据 pc=2730-2732 `LDD<= C220 D30 / AND M242 / OUT S23`（S6→S23 的门槛）
#         pc=4149-4152 S23→S10 也用 `C220<=D30`
#         pc=2653-2654 M338 靠它，D30 必须 >0，否则 S6 退到后限位后 C220=0 仍满足
D_HEAD_POSITION = 2

# 光电A（X003）：车头越过光电后为 1。下限由梯形图给：pc=4645-4657 在 S4 步里
# C220<150 时 X003 必须还是 0，否则报故障码 26。取 150（刚好卡在门槛外）。
X003_FRONT = 150

# 顶刷行程：C222 预设定 K50、S3→S4 要求 C222>38，取 50 与预设定一致。
TOP_DEPTH = 50

# 侧刷行程（扫描数）。开阀后反馈位到位需要时间，期间"原位"反馈必须消失，
# 否则 pc=4677-4688 的 T102/T110 满 5 秒报 D2=32。5 秒 = 100 扫描，取 10（0.5 秒）留余量。
BRUSH_TRAVEL = 10

# 左刷：Y014 开 / Y015 合 / X010 开位 / X011 合位
# 右刷：Y020 开 / Y021 合 / X014 开位 / X015 合位
SIDE_BRUSH = (
    ('left', 14, 15, 10, 11),
    ('right', 20, 21, 14, 15),
)

# ---------------------------------------------------------------- 现场预设
# 复现 2026-10-22 现场那台「进洗车头后卡住 → T99 原地停留 40 秒 → D2=25 → S15」的机器。
#   D32 = D30 + 70（梯形图 P6#1 `DADD D30 K70 D32`），现场 D30=2 ⇒ 72。
#   X006 = 顶刷下限位取 0：pc=1078 `ANDD< C222 K40` 把顶刷下行限在 40 个脉冲，
#   顶刷下不到位 ⇒ S3 出口的另一半条件 `pc=2027 LD X006` 不成立。
FIELD_PRESETS = {
    'stall-s3-t99': {
        'desc': '复现现场故障：PLC 进洗车头（D142=2）后卡住 → T99 原地停留 40 秒 → D2=25',
        'D': {32: 72},
        'X': {6: 0},
        'M': {},
        'rule': None,
    },
}


def resolve_preset(preset):
    """None → 空预设；字符串 → FIELD_PRESETS 里的预设；dict → 原样用。"""
    if preset is None:
        return {}
    if isinstance(preset, str):
        if preset not in FIELD_PRESETS:
            raise KeyError('没有现场预设 %r，可选: %s' % (preset, ', '.join(FIELD_PRESETS)))
        return FIELD_PRESETS[preset]
    return preset


def build(csv_path, preset=None):
    """按现场条件造一台 PLC：常态输入按现场表，只能外部给的 M/D 直接预置。"""
    preset = resolve_preset(preset)
    ladder = Ladder(csv_path)
    ladder.preset = preset
    for number in range(128):
        ladder.X[number] = 0
    for number, value in machine.X_IDLE.items():
        ladder.X[number] = value
    for number in machine.X_FAULT:
        ladder.X[number] = 0
    for number, value in machine.M_CONFIG.items():
        ladder.M[number] = value
    for number, value in machine.INJECT_M.items():
        ladder.M[number] = value
    for number, value in machine.DEFAULTS_D.items():
        ladder.D[number] = value
    # 现场模式：外部参数覆盖（最后一道，盖掉上面的默认值）
    for number, value in preset.get('D', {}).items():
        ladder.D[number] = value
    for number, value in preset.get('M', {}).items():
        ladder.M[number] = value
    return ladder


def new_state():
    return {'pulse': 0, 'pos': 0, 'brush_pulse': 0, 'brush_pos': 0,
            'flow_tick': 0, 'flow_pulse': 0,
            'left': 'open', 'left_t': 0, 'right': 'open', 'right_t': 0}


def _side_brush(ladder, state, key, open_y, close_y, open_x, close_x):
    """侧刷：开阀 → 开位反馈，合阀 → 合位反馈。

    两条约束都来自梯形图：
      · 开阀要**立刻**脱离原位反馈，否则 pc=4677-4688 的 T102/T110 满 5 秒报 D2=32；
      · 两个位置反馈不能同时为 1，否则 pc=4669-4676 报 D2=28。
    """
    if ladder.Y.get(open_y, 0):
        ladder.X[close_x] = 0
        if state[key] != 'open':
            state[key + '_t'] += 1
            if state[key + '_t'] >= BRUSH_TRAVEL:
                state[key] = 'open'
                state[key + '_t'] = 0
                ladder.X[open_x] = 1
    elif ladder.Y.get(close_y, 0):
        ladder.X[open_x] = 0
        if state[key] != 'closed':
            state[key + '_t'] += 1
            if state[key + '_t'] >= BRUSH_TRAVEL:
                state[key] = 'closed'
                state[key + '_t'] = 0
                ladder.X[close_x] = 1


def plant(ladder, state):
    """现场物理反馈：程序输出 Y → 机构动作 → 反馈输入 X。每个扫描周期调用一次。"""
    forward = ladder.Y.get(30, 0)
    backward = ladder.Y.get(31, 0)

    # 台车移动 → 行走脉冲。X002 是方波：走一步翻转一次，两个边沿都进 C220。
    # 后限位（X075）是硬限位：贴到后端时电机被挡住，不再出脉冲（否则 C220 立刻从 0 重计）。
    if forward or backward:
        if backward and state['pos'] <= 0:
            ladder.X[2] = 0
        else:
            state['pulse'] ^= 1
            ladder.X[2] = state['pulse']
            state['pos'] += 1 if forward else -1
            if state['pos'] < 0:
                state['pos'] = 0
    else:
        ladder.X[2] = 0

    ladder.X[1] = 1 if state['pos'] >= FRONT_LIMIT else 0
    ladder.X[75] = 1 if state['pos'] <= 0 else 0

    # 车的光电：X003 车头越过光电A 才置 1；X024 车身检测，车离开装载位就没了。
    ladder.X[3] = 1 if state['pos'] >= X003_FRONT else 0
    ladder.X[24] = 1 if state['pos'] <= 0 else 0

    # 顶刷：Y011 下行 / Y007 上行，到位（X006 / X005）才停；运动中 X007 每扫描翻转。
    if ladder.Y.get(11, 0) and state['brush_pos'] < TOP_DEPTH:
        state['brush_pos'] += 1
        state['brush_pulse'] ^= 1
        ladder.X[7] = state['brush_pulse']
    elif ladder.Y.get(7, 0) and state['brush_pos'] > 0:
        state['brush_pos'] -= 1
        state['brush_pulse'] ^= 1
        ladder.X[7] = state['brush_pulse']
    ladder.X[5] = 1 if state['brush_pos'] == 0 else 0
    ladder.X[6] = 1 if state['brush_pos'] >= TOP_DEPTH else 0

    # 侧刷开合位（Y014/Y015/Y020/Y021 ↔ X010/X011/X014/X015）
    for key, open_y, close_y, open_x, close_x in SIDE_BRUSH:
        _side_brush(ladder, state, key, open_y, close_y, open_x, close_x)

    # 轮刷开位（气阀把轮刷架推开到位）
    if ladder.Y.get(46, 0):
        ladder.X[30] = 1
        ladder.X[31] = 1

    # 流量计：X000 是脉冲输入（程序用 SPD X000 测周期存 D3000）。
    # 有水（泵或水伐开）才有流水，脉冲周期 2 秒（20 扫描翻转一次）。
    if (ladder.Y.get(16, 0) or ladder.Y.get(17, 0) or ladder.Y.get(40, 0)
            or ladder.Y.get(41, 0) or ladder.Y.get(42, 0) or ladder.Y.get(47, 0)):
        state['flow_tick'] += 1
        if state['flow_tick'] >= 20:
            state['flow_tick'] = 0
            state['flow_pulse'] ^= 1
        ladder.X[0] = state['flow_pulse']
    else:
        ladder.X[0] = 0
        state['flow_tick'] = 0

    # M99 / D14 全工程只有读、没有写入点，只能外部给：
    #   M99 —— 2 处引用全是读。
    #   D14 —— 唯一写入点在 I680 子程序，但 I680 全工程没有 CALL。
    #   D14 必须 >=10（否则 pc=4549-4559 的 T160 会报 D2=37），且 <D220（pc=919）。
    #   取 90：P1 子程序 `ADD D14 D224 D220` 会把 D220 算回 100（D224=10）。
    ladder.M[99] = 1
    ladder.D[14] = 90

    # M3 全工程没有写入点，只能外部给，但洗车必须为 1：
    #   pc=2222-2230（S4 步）侧刷反转 Y013 的锁存条件化简后就是 M3；
    #   Y013 是解除 M367（封锁前进 Y030）的唯一途径。
    #   P0 子程序 `ZRST M1 M131` 进 S1 就绪步会清掉 M3，所以必须**每扫描重写**。
    ladder.M[3] = 1

    # 现场模式：外部输入覆盖（现场传感器/开关的值比模拟器推出来的更权威）
    preset = getattr(ladder, 'preset', None)
    if preset:
        for number, value in preset.get('M', {}).items():
            ladder.M[number] = value
        for number, value in preset.get('X', {}).items():
            ladder.X[number] = value
        rule = preset.get('rule')
        if rule is not None:
            ladder.X.update(rule(ladder, state))
