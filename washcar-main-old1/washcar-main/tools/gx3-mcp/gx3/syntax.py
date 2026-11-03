"""Lossless V1 string-table and recursive structure reader. No instruction whitelist."""
import re


class FormatError(ValueError):
    pass


def decode(data):
    if not data.startswith('V1:'):
        raise FormatError('Unsupported serialization version')
    p = 3

    def number():
        nonlocal p
        end = data.index(':', p)
        n = int(data[p:end])
        if n < 0:
            raise FormatError('Negative length')
        p = end + 1
        return n

    count = number()
    lengths = [number() for _ in range(count)]
    strings = []
    for length in lengths:
        start, units = p, 0
        while units < length and p < len(data):
            units += 2 if ord(data[p]) > 0xffff else 1
            p += 1
        if units != length or p >= len(data) or data[p] != ':':
            raise FormatError(f'Invalid UTF-16 string length at {start}')
        strings.append(data[start:p])
        p += 1
    body_start = p
    refs = []

    def value():
        nonlocal p
        start = p
        if p >= len(data):
            raise FormatError('Unexpected end')
        if data[p] == '#':
            p += 1
            i = len(refs)
            if i >= len(strings):
                raise FormatError('More placeholders than strings')
            refs.append({'index': i, 'offset': start})
            return strings[i]
        if data[p] == '[':
            p += 1
            out = []
            while p < len(data) and data[p] != ']':
                out.append(value())
                if data[p] == ':':
                    p += 1
                elif data[p] != ']':
                    raise FormatError(f'Expected list separator at {p}')
            expect(']')
            return out
        while p < len(data) and data[p] not in '{}[]:':
            p += 1
        token = data[start:p]
        if not token:
            raise FormatError(f'Empty token at {p}')
        if p < len(data) and data[p] == '{':
            p += 1
            fields = {}
            while p < len(data) and data[p] != '}':
                key_start = p
                while p < len(data) and data[p] not in '=}:':
                    p += 1
                key = data[key_start:p]
                expect('=')
                if key in fields:
                    raise FormatError(f'Duplicate field {key}')
                fields[key] = value()
                if data[p] == ':':
                    p += 1
                elif data[p] != '}':
                    raise FormatError(f'Expected field separator at {p}')
            expect('}')
            return {'type': token, 'fields': fields}
        return token

    def expect(char):
        nonlocal p
        if p >= len(data) or data[p] != char:
            raise FormatError(f'Expected {char!r} at {p}')
        p += 1

    root = value()
    if p != len(data) or len(refs) != count:
        raise FormatError(f'Unconsumed data: offset={p}/{len(data)}, refs={len(refs)}/{count}')
    return {'strings': strings, 'lengths': lengths, 'body_offset': body_start,
            'references': refs, 'root': root}


def fields(node):
    return node.get('fields', {}) if isinstance(node, dict) else {}


def walk(node, path='$'):
    if isinstance(node, dict):
        yield path, node
        for key, val in fields(node).items():
            yield from walk(val, path + '.' + key)
    elif isinstance(node, list):
        for i, val in enumerate(node):
            yield from walk(val, f'{path}[{i}]')


def pair(text, separator=','):
    vals = text.split(separator)
    if len(vals) != 2:
        raise FormatError(f'Invalid coordinate: {text}')
    return [int(v) for v in vals]
