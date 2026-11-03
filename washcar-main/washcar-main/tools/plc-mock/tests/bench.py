#!/usr/bin/env python3
# 扫描吞吐基准:同一条梯形图、同一串现场输入,分别用快速路径和逐条解释器跑,
# 报 ms/scan、scans/s 和加速比。
#
# 用法:
#   python bench.py                         # 默认 1000 扫描 × 3 轮
#   python bench.py --scans 300 --rounds 2
#   python bench.py ../../docs/海天/海天洗车机.csv
#
# 注意:两种模式必须用各自独立的 Ladder 实例,且每轮都从同一个初始状态起跑,
# 否则洗车阶段不同、每扫描的指令条数不同,数字会漂。
import argparse
import gc
import os
import platform
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import wash_cycle


def measure(csv_path, use_fast, scans, warmup):
    ladder = wash_cycle.build(csv_path)
    ladder.use_fast = use_fast
    state = wash_cycle.new_state()
    for _ in range(warmup):
        wash_cycle.plant(ladder, state)
        ladder.scan(0.05)
    started = time.perf_counter()
    for _ in range(scans):
        wash_cycle.plant(ladder, state)
        ladder.scan(0.05)
    elapsed = time.perf_counter() - started
    return elapsed, ladder


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description='PLC 模拟器扫描吞吐基准')
    parser.add_argument('csv', nargs='?', default=wash_cycle.DEFAULT_CSV)
    parser.add_argument('--scans', type=int, default=1000, help='每轮计时扫描数')
    parser.add_argument('--warmup', type=int, default=100, help='预热扫描数(不计时)')
    parser.add_argument('--rounds', type=int, default=3, help='重复轮数,取每档最好成绩')
    args = parser.parse_args()

    if not os.path.exists(args.csv):
        print('找不到梯形图: %s' % args.csv)
        return 2

    print('梯形图: %s' % os.path.basename(args.csv))
    print('Python: %s (%s)' % (platform.python_version(), platform.platform()))
    print('每轮 %d 扫描,预热 %d,重复 %d 轮' % (args.scans, args.warmup, args.rounds))
    gc_was_enabled = gc.isenabled()
    gc.disable()
    try:
        fast_times = []
        slow_times = []
        final = None
        for index in range(args.rounds):
            fast_elapsed, ladder = measure(args.csv, True, args.scans, args.warmup)
            fast_times.append(fast_elapsed)
            slow_elapsed, _ = measure(args.csv, False, args.scans, args.warmup)
            slow_times.append(slow_elapsed)
            final = ladder
            print('  第 %d 轮: 快速 %.1f ms / 逐条 %.1f ms'
                  % (index + 1, fast_elapsed * 1000.0 / args.scans, slow_elapsed * 1000.0 / args.scans))
    finally:
        if gc_was_enabled:
            gc.enable()

    fast = min(fast_times) * 1000.0 / args.scans
    slow = min(slow_times) * 1000.0 / args.scans
    print()
    print('快速路径: %.3f ms/scan  %.0f scans/s' % (fast, 1000.0 / fast))
    print('逐条解释: %.3f ms/scan  %.0f scans/s' % (slow, 1000.0 / slow))
    print('加速: %.2fx' % (slow / fast))
    if final is not None:
        print('末态(基准只跑上电段,没注入启动,停在 S0 属正常): D142=%s D2=%s S=%s'
              % (final.D.get(142), final.D.get(2),
                 sorted(number for number, value in final.S.items() if value)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
