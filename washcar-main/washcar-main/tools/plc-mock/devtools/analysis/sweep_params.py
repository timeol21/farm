#!/usr/bin/env python3
# 现场故障反推:扫描"梯形图里只读不写"的外部参数,找出能复现现场症状的组合。
#
# 现场症状(2026-10-22 真机日志,唯一判据):
#   云端 start 被接受 → PLC 进洗车头(D142=2) → **卡住 2 分 08 秒** →
#   FaultReport fault=25 → D142 归 0 → D142=10(S15 洗车结束)。
#
# 复现判据(三条同时满足才算复现):
#   1) PLC 进过 S3(洗车头,D142=2)
#   2) **没有**走完 S4/S5/S6…(即卡在 S3 里出不来)
#   3) 末态出现过 D2 == 25(T99 原地停留超时),并最终进 S15
#
# 用法:
#   python3 analysis/sweep_params.py --list                 列出候选参数与取值
#   python3 analysis/sweep_params.py --one D30              一维扫描(固定其它)
#   python3 analysis/sweep_params.py --two D30 X001         二维组合扫描
#   python3 analysis/sweep_params.py --all                  一维全扫 + 关键参数二维
#   常用开关:--limit 扫描上限 --jobs 并行数 --budget 总秒数
#
# 退出码:0 = 找到可复现组合;2 = 没找到(并列出已排除的组合);1 = 用法/环境错误。
import argparse
import contextlib
import io
import multiprocessing
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))          # tools/plc-mock
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tests'))

import wash_cycle                                      # noqa: E402
from wash_cycle import build, new_state, Trace, run    # noqa: E402

CSV = os.path.join(ROOT, 'docs', '海天', '海天洗车机.csv')

# ---------------------------------------------------------------------------
# 候选参数:梯形图里**只读不写**的外部量(上位机/HMI 下发或现场输入)。
# 取值范围是"给现场核对的区间",不是猜出来的定值。
# 每一项都注明它在梯形图里的用途,便于现场拿着梯形图对号入座。
# ---------------------------------------------------------------------------
CANDIDATES = {
    # ---- 任务书点名的四个 ----
    'D276': {
        'values': [0, 50, 100, 200, 500, 1100, 1500, 3000],
        'why': 'P7#30 `SUB D276 D801 D804`:C0 的预设定来源(剩余行程)。'
               '全工程无写入点 ⇒ 只能上位机给。',
    },
    'M230': {
        'values': [0, 1],
        'why': 'P7#5 `AND M230`:D800 行程计数的使能位(外部 HMI 位)。'
               '全工程无写入点。',
    },
    'D30': {
        'values': [2, 30, 100, 500, 1100],
        'why': '主程序 pc=1946 `LDD= C220 D30` → `SET M16`:M16 一置位就掐断前进输出'
               '(pc=1964 `LDI M16`);同时 D32=D30+70(P6#1)是 S3→S4 的行程门槛'
               '(pc=2034 `ORD>= C220 D32`)。',
    },
    'X001': {
        'values': [0, 1],
        'why': '前位开关。pc=4703 `LD X001 / ANDD>= C220 K850 / MOV K34 D2` 与'
               'pc=2363 一组互斥约束,现场位置待确认。',
    },
    # ---- 本次从梯形图里新挖出来的 ----
    'D32': {
        'values': [0, 70, 72, 100, 200, 600, 1170],
        'why': 'S3→S4 的行程门槛(pc=2034 `ORD>= C220 D32`)。'
               '真机上是 P6#1 `DADD D30 K70 D32` = D30+70(默认 D30=2 ⇒ 72);'
               '模拟器的 32 位算术是空操作(见 ladder.py),D32 恒为 0,'
               '所以这一项要按真机的值手动喂进去。',
    },
    'D121': {
        'values': [0, 1, 2, 3],
        'why': 'pc=2006/2010 `AND= D121 K0` / `AND= D121 K1` → Y032/Y033 两个'
               '水阀输出。全工程无写入点。',
    },
    'D256': {
        'values': [0, 50, 100, 500, 1100],
        'why': 'pc=2001 `ANDD< C220 D256` → Y032 的门槛。全工程无写入点。',
    },
    'D250': {
        'values': [0, 30, 100, 500, 1100],
        'why': 'P6#9 `DADDP D6 D250 D30`:D30 的真实外部来源(洗车头位置)。',
    },
    'D514': {
        'values': [0, 1],
        'why': '屏蔽轮刷。P7#7 `ANI M514` 会掐断 D800 计数。',
    },
    'M528': {
        'values': [0, 1],
        'why': 'pc=3239-3245 S9→S27 的支路条件之一(现场语义待确认)。',
    },
    'M551': {
        'values': [0, 1],
        'why': 'S9→S27 的支路条件之一(现场语义待确认)。',
    },
    'M552': {
        'values': [0, 1],
        'why': 'S9→S27 的支路条件之一(现场语义待确认)。',
    },
    'M512': {
        'values': [0, 1],
        'why': 'S3 步内 pc=1918/1930/1937 `AND M512` → M146 → Y027 喷淋阀。'
               '全工程无写入点。',
    },
    'D512': {
        'values': [0, 100, 500],
        'why': 'D512~D548 整段是外部参数区(设备标定值),逐个核实读点。',
    },
    'D516': {
        'values': [0, 100, 500],
        'why': 'D512~D548 整段是外部参数区(设备标定值)。',
    },
    'D520': {
        'values': [0, 100, 500],
        'why': 'D512~D548 整段是外部参数区(设备标定值)。',
    },
    'D524': {
        'values': [0, 100, 500],
        'why': 'D512~D548 整段是外部参数区(设备标定值)。',
    },
    'D528': {
        'values': [0, 100, 500],
        'why': 'D512~D548 整段是外部参数区(设备标定值)。',
    },
    'D542': {
        'values': [0, 100, 500],
        'why': 'D512~D548 整段是外部参数区(设备标定值)。',
    },
    'D544': {
        'values': [0, 100, 500],
        'why': 'D512~D548 整段是外部参数区(设备标定值)。',
    },
    'D548': {
        'values': [0, 100, 500],
        'why': 'D512~D548 整段是外部参数区(设备标定值)。',
    },
    'D368': {
        'values': [0, 500, 1100, 2000],
        'why': 'pc=3230 `LDD< C220 D368` → M186,蜡水段行程窗口。',
    },
    'D370': {
        'values': [0, 500, 1100, 2000],
        'why': 'pc=3239-3245 S9→S27 的行程门槛。',
    },
    'X003': {
        'values': [0, 1],
        'why': '光电A(车头检测)。pc=4645 `LDD> C220 K0 / ANDD< C220 K150 / AND X003 '
               '→ D2=26`。',
    },
    'X024': {
        'values': [0, 1],
        'why': '光电B(车身检测)。pc=1950 `ANI X024` 是 M16 的使能条件。',
    },
    'X075': {
        'values': [0, 1],
        'why': '后限位。pc=4335 `LDP X075 / ANI Y030 / ANI S2 / ANI S3 / ANI S20 '
               '/ RST C220`。',
    },
    'X004': {
        'values': [0, 1],
        'why': 'IO 表里 X4 名称为空(现场语义未知)。pc=1757 `ANI X004 → SET M245`,'
               'M245 会掐断 P7 的行程计数;P7#27 `SET M31` 也靠它。',
    },
    'X006': {
        'values': [0, 1],
        'why': '顶刷下限位。S3→S4 出口条件之一(pc=2027 `LD X006`);'
               '顶刷下行被 pc=1078 `ANDD< C222 K40` 限在 40 个脉冲。',
    },
    'X010': {
        'values': [0, 1],
        'why': '左刷开位。S3→S4 出口条件之一(pc=2032 `LD X010 / AND X014`);'
               '也是 M1 启动前置条件之一,置 0 会直接挡住启动。',
    },
    'X014': {
        'values': [0, 1],
        'why': '右刷开位。同 X010,出口条件与 M1 前置条件都用它。',
    },
}

# 一维扫描的顺序:先跑任务书点名的四个,再跑新挖出来的。
ONE_D_ORDER = ['D276', 'M230', 'D30', 'X001', 'D32', 'X006', 'X010', 'X014',
               'D121', 'D256', 'D250', 'M512', 'X024', 'X003', 'X075', 'X004',
               'M514', 'M528', 'M551', 'M552', 'D512', 'D516', 'D520', 'D524',
               'D528', 'D542', 'D544', 'D548', 'D368', 'D370']

# 参数名 → 覆盖字典的键
FAMILY = {}
for _name in CANDIDATES:
    FAMILY[_name] = ('D' if _name.startswith('D') else
                     'M' if _name.startswith('M') else 'X')


def make_preset(overrides):
    """把 {参数名: 值} 变成 wash_cycle 认的覆盖字典。"""
    preset = {'D': {}, 'M': {}, 'X': {}}
    for name, value in overrides.items():
        family = FAMILY[name]
        if family == 'D':
            preset['D'][int(name[1:])] = value
        elif family == 'M':
            preset['M'][int(name[1:])] = value
        else:
            preset['X'][int(name[1:])] = value
    return preset


def simulate(overrides, limit=4000):
    """跑一遍完整洗车流程,返回现场症状判据的观测结果。"""
    ladder = build(CSV, preset=make_preset(overrides))
    state = new_state()
    trace = Trace(ladder)
    seen = {'d2': set(), 'scans': 0}
    # 停顿观测:在 S3 里"台车不动"(Y030=Y031=0)的连续扫描数。
    # T99 是 40 秒 = 800 扫描的原地停留超时,所以这个数 >= 800 才会报 D2=25。
    stall = {'run': 0, 'max': 0, 'at_c220': None, 'max_c220': None}

    def on_scan(lad, _state, index):
        seen['scans'] = index
        fault = lad.D.get(2)
        if fault is not None:
            seen['d2'].add(fault)
        if lad.S.get(3):
            moving = lad.Y.get(30, 0) or lad.Y.get(31, 0)
            if moving:
                stall['run'] = 0
            else:
                stall['run'] += 1
                if stall['run'] > stall['max']:
                    stall['max'] = stall['run']
                    stall['max_c220'] = lad.C.get(220)
                if stall['at_c220'] is None and stall['run'] == 1:
                    stall['at_c220'] = lad.C.get(220)
        # 一旦走出 S3(S4 及以后),就不是"卡在洗车头"了 —— 提前收工。
        if 4 in trace.step_trail:
            return True
        if 15 in trace.step_trail and fault is not None:
            return True
        return False

    # run() 在"没进就绪态"时会调 diag.diagnose 直接往 stdout 打印,
    # 扫描脚本要的是表格,把那堆诊断输出收进垃圾桶。
    with contextlib.redirect_stdout(io.StringIO()):
        run(ladder, state, trace, limit=limit, on_scan=on_scan)
    steps = trace.step_trail
    reached_s3 = 3 in steps
    passed_s3 = 4 in steps
    d2_25 = 25 in seen['d2']
    ended_s15 = 15 in steps
    return {
        'overrides': dict(overrides),
        'reached_s3': reached_s3,
        'passed_s3': passed_s3,
        'd2_25': d2_25,
        'ended_s15': ended_s15,
        'd2_seen': sorted(seen['d2']),
        'd142': trace.d142_values,
        'steps': steps,
        'scans': seen['scans'],
        'stall_max': stall['max'],
        'stall_at_c220': stall['at_c220'],
        'stall_max_c220': stall['max_c220'],
        'reproduced': reached_s3 and not passed_s3 and d2_25 and ended_s15,
        'timeout': not passed_s3 and seen['scans'] >= limit - 1,
    }


def _worker(task):
    overrides, limit = task
    try:
        return simulate(overrides, limit=limit)
    except Exception as exc:                      # 单个组合跑挂不能拖垮整轮扫描
        return {'overrides': dict(overrides), 'error': '%s: %s' % (type(exc).__name__, exc),
                'reproduced': False, 'd2_seen': [], 'd142': [], 'steps': [], 'scans': 0}


def run_batch(tasks, jobs=4, budget=None):
    """并行跑一批组合,受总时间预算约束。返回 (结果列表, 是否因预算提前收工)。"""
    results = []
    truncated = False
    start = time.time()
    if jobs <= 1:
        for item in tasks:
            if budget and time.time() - start > budget:
                truncated = True
                break
            results.append(_worker(item))
    else:
        pool = multiprocessing.Pool(jobs)
        try:
            for result in pool.imap_unordered(_worker, tasks):
                results.append(result)
                if budget and time.time() - start > budget:
                    truncated = True
                    pool.terminate()
                    break
        finally:
            pool.close()
            pool.join()
    return results, truncated


def describe(result):
    if 'error' in result:
        return 'ERROR %s' % result['error']
    bits = []
    bits.append('S3进=%s' % ('Y' if result['reached_s3'] else 'N'))
    bits.append('出S3=%s' % ('Y' if result['passed_s3'] else 'N'))
    bits.append('D2=%s' % (result['d2_seen'] or '[]'))
    bits.append('S15=%s' % ('Y' if result['ended_s15'] else 'N'))
    bits.append('停顿=%d扫描@C220=%s' % (result.get('stall_max', 0),
                                         result.get('stall_max_c220')))
    bits.append('扫描=%d' % result['scans'])
    bits.append('判定=%s' % ('★复现' if result['reproduced'] else
                              ('预算耗尽' if result['timeout'] else '不复现')))
    return ' '.join(bits)


def print_results(title, results):
    print('\n==== %s ====' % title)
    for result in results:
        overrides = ', '.join('%s=%s' % (k, v) for k, v in sorted(result['overrides'].items()))
        print('  %-34s %s' % (overrides or '(基线)', describe(result)))


def cmd_list(_args):
    print('候选参数(全部是梯形图里只读不写、只能外部给的量):')
    for name in ONE_D_ORDER:
        if name not in CANDIDATES:
            continue
        item = CANDIDATES[name]
        print('\n  %-6s 取值 %s' % (name, item['values']))
        for line in item['why'].split('\n'):
            print('         %s' % line)
    return 0


def cmd_one(args):
    name = args.one
    if name not in CANDIDATES:
        print('未知候选参数 %r;用 --list 看清单' % name)
        return 1
    values = args.values or CANDIDATES[name]['values']
    tasks = [({name: value}, args.limit) for value in values]
    results, truncated = run_batch(tasks, jobs=args.jobs, budget=args.budget)
    print_results('一维扫描 %s(其余固定为理想机基线)' % name, results)
    hit = [r for r in results if r.get('reproduced')]
    print('\n结论: %s' % ('★ 找到可复现组合 %s' %
                           ', '.join('%s=%s' % kv for kv in hit[0]['overrides'].items())
                           if hit else '没有组合复现现场症状'))
    if truncated:
        print('  (总时间预算到,本轮有组合没跑完)')
    return 0 if hit else 2


def cmd_two(args):
    first, second = args.two
    for name in (first, second):
        if name not in CANDIDATES:
            print('未知候选参数 %r;用 --list 看清单' % name)
            return 1
    values1 = args.values1 or CANDIDATES[first]['values']
    values2 = args.values2 or CANDIDATES[second]['values']
    tasks = [({first: v1, second: v2}, args.limit)
             for v1 in values1 for v2 in values2]
    results, truncated = run_batch(tasks, jobs=args.jobs, budget=args.budget)
    print_results('二维扫描 %s × %s' % (first, second), results)
    hit = [r for r in results if r.get('reproduced')]
    print('\n结论: %s' % ('★ 找到可复现组合 %s' %
                           ', '.join('%s=%s' % kv for kv in hit[0]['overrides'].items())
                           if hit else '没有组合复现现场症状'))
    if truncated:
        print('  (总时间预算到,本轮有组合没跑完)')
    return 0 if hit else 2


def cmd_all(args):
    """一维全扫(按 ONE_D_ORDER),再对有戏的参数做二维。"""
    tasks = []
    for name in ONE_D_ORDER:
        if name not in CANDIDATES:
            continue
        for value in CANDIDATES[name]['values']:
            tasks.append(({name: value}, args.limit))
    # 基线也跑一遍做对照
    tasks.append(({}, args.limit))
    start = time.time()
    results, truncated = run_batch(tasks, jobs=args.jobs, budget=args.budget)
    print_results('一维全扫', results)
    hit = [r for r in results if r.get('reproduced')]
    print('\n一维结论: %s' % ('★ 找到可复现组合' if hit else '没有组合复现现场症状'))
    if truncated:
        print('  (总时间预算到,一维没扫完)')
    print('  一维用时 %.1fs' % (time.time() - start))
    return 0 if hit else 2


def cmd_preset(args):
    """跑 wash_cycle.PRESETS 里的现场预设,看它是不是真的复现现场症状。"""
    import wash_cycle
    name = args.preset or 'stall-s3-t99'
    preset = wash_cycle.PRESETS[name]
    print('现场预设 %s: %s' % (name, preset.get('desc', '')))
    print('  D 覆盖: %s' % preset.get('D'))
    print('  M 覆盖: %s' % preset.get('M'))
    print('  X 覆盖: %s' % preset.get('X'))
    ladder = build(CSV, preset=preset)
    state = new_state()
    trace = Trace(ladder)
    seen = {'d2': set()}
    stall = {'run': 0, 'max': 0, 'at': None}

    def on_scan(lad, _st, index):
        if lad.D.get(2) is not None:
            seen['d2'].add(lad.D.get(2))
        if lad.S.get(3):
            if lad.Y.get(30) or lad.Y.get(31):
                stall['run'] = 0
            else:
                stall['run'] += 1
                if stall['run'] > stall['max']:
                    stall['max'] = stall['run']
                    stall['at'] = lad.C.get(220)
        if 4 in trace.step_trail:
            return True
        return False

    with contextlib.redirect_stdout(io.StringIO()):
        run(ladder, state, trace, limit=args.limit, on_scan=on_scan)
    entered = 3 in trace.step_trail
    left = 4 in trace.step_trail
    d2_25 = 25 in seen['d2']
    ended = 15 in trace.step_trail
    ok = entered and not left and d2_25 and ended
    print('  D142 轨迹: %s' % ' → '.join(str(v) if v else 'None' for v in trace.d142_values))
    print('  S 步轨迹: %s' % ' → '.join('S%d' % s for s in trace.step_trail))
    print('  进过 S3=%s 出 S3=%s D2=%s 收在 S15=%s 停顿=%d 扫描@C220=%s'
          % (entered, left, sorted(seen['d2']), ended, stall['max'], stall['at']))
    print('判定: %s' % ('★ 复现现场症状' if ok else '没复现'))
    return 0 if ok else 2


def main():
    parser = argparse.ArgumentParser(description='现场故障反推:外部参数扫描')
    parser.add_argument('--list', action='store_true', help='列出候选参数与取值')
    parser.add_argument('--one', help='一维扫描某个参数')
    parser.add_argument('--two', nargs=2, help='二维扫描两个参数')
    parser.add_argument('--all', action='store_true', help='一维全扫(所有候选参数)')
    parser.add_argument('--preset', nargs='?', const='stall-s3-t99',
                        help='跑 wash_cycle.PRESETS 里的现场预设并校验是否复现症状')
    parser.add_argument('--values', nargs='*', type=int, help='覆盖 --one 的取值')
    parser.add_argument('--values1', nargs='*', type=int, help='覆盖 --two 第一个参数的取值')
    parser.add_argument('--values2', nargs='*', type=int, help='覆盖 --two 第二个参数的取值')
    parser.add_argument('--limit', type=int, default=4000, help='单组合扫描上限')
    parser.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument('--budget', type=float, default=None, help='总时间预算(秒)')
    args = parser.parse_args()

    if args.list:
        return cmd_list(args)
    if args.preset:
        return cmd_preset(args)
    if args.one:
        return cmd_one(args)
    if args.two:
        return cmd_two(args)
    if args.all:
        return cmd_all(args)
    parser.print_help()
    return 1


if __name__ == '__main__':
    sys.exit(main())
