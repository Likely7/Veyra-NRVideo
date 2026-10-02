"""Wrap every user-visible Chinese string literal in qml/Veyra/*.qml with qsTr().

usage: python -B scripts/i18n/wrap_qml.py [--report] [--apply]

Chinese is the source language: the literal stays as it is and becomes the translation key
(the runtime translator in src/ui/UiLanguage.cpp looks keys up by source text, any context).
Comments, imports and literals already inside qsTr() are left alone. --report lists the
places where a wrapped literal is compared or switched on, so each can be checked: both
sides of such a comparison must be translated the same way (or neither).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HAN = re.compile('[一-鿿぀-ヿ]')


def literals(text):
    """Yield (start, end, quote) for every string literal outside comments."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in '"\'':
            j = i + 1
            # QML string literals may span lines; a literal that does is reported, not
            # wrapped, because its line break is part of the key.
            while j < n and text[j] != c:
                if text[j] == '\\':
                    j += 1
                j += 1
            yield i, j + 1, c
            i = j + 1
            continue
        if c == '`':
            j = text.find('`', i + 1)
            i = n if j < 0 else j + 1
            continue
        i += 1


def process(path, apply, report):
    text = path.read_text(encoding='utf-8')
    out, last, wrapped = [], 0, 0
    for s, e, q in literals(text):
        body = text[s + 1:e - 1]
        if not HAN.search(body):
            continue
        if '\n' in body:
            print(f'{path.name}:{text.count(chr(10), 0, s) + 1}: multi-line literal left as is; split it with "\\n"')
            continue
        before = text[:s].rstrip()
        line_start = text.rfind('\n', 0, s) + 1
        line = text[line_start:text.find('\n', e) if text.find('\n', e) >= 0 else len(text)]
        if line.lstrip().startswith('import '):
            continue
        if before.endswith('qsTr(') or before.endswith('qsTranslate("ui",') or before.endswith('QT_TR_NOOP('):
            continue
        if report:
            after = text[e:e + 6]
            prev = before[-4:]
            if re.search(r'(===|!==|==|!=)\s*$', before) or re.match(r'\s*(===|!==|==|!=)', after) or before.endswith('case') \
                    or re.search(r'(indexOf|startsWith|endsWith|includes|split|replace)\($', before):
                no = text.count('\n', 0, s) + 1
                print(f'{path.name}:{no}: {line.strip()[:160]}')
        out.append(text[last:s])
        out.append('qsTr(' + text[s:e] + ')')
        last = e
        wrapped += 1
    out.append(text[last:])
    if apply and wrapped:
        path.write_text(''.join(out), encoding='utf-8', newline='')
    return wrapped


def main():
    apply = '--apply' in sys.argv
    report = '--report' in sys.argv
    total = 0
    for path in sorted((ROOT / 'qml/Veyra').glob('*.qml')):
        n = process(path, apply, report)
        total += n
        if n and not report:
            print(f'{path.name}: {n}')
    print('wrapped' if apply else 'would wrap', total)


if __name__ == '__main__':
    main()
