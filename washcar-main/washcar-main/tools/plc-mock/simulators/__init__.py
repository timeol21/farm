"""机型注册表。

`simulators/` 下每个**包**只要在 `__init__.py` 里暴露 `SIMULATOR`（类或实例），
就会被自动发现，命令行 `--plc <包名>` 和看板的机型下拉都能看到它。
以 `_` 开头的包（`_template`）不参与注册 —— 模板留着复制用。
"""

import importlib
import pkgutil

_CACHE = None


def discover():
    """扫描一遍机型目录，返回 {key: 实例}。"""
    global _CACHE
    if _CACHE is not None:
        return _CACHE
    found = {}
    for info in pkgutil.iter_modules(__path__):
        if not info.ispkg or info.name.startswith('_') or info.name == 'base':
            continue
        try:
            module = importlib.import_module('%s.%s' % (__name__, info.name))
        except Exception as exc:                      # 一个机型坏了不该拖垮其它机型
            print('[simulators] 跳过 %s: %s' % (info.name, exc))
            continue
        factory = getattr(module, 'SIMULATOR', None)
        if factory is None:
            continue
        instance = factory() if isinstance(factory, type) else factory
        found[instance.key] = instance
    _CACHE = dict(sorted(found.items()))
    return _CACHE


def load(key):
    """按名字取机型。名字错了就把可选机型列出来。"""
    available = discover()
    try:
        return available[key]
    except KeyError:
        raise KeyError('没有机型 %r，可选: %s' % (key, ', '.join(available) or '(空)'))


def keys():
    return list(discover())
