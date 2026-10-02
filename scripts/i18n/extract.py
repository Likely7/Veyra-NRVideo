"""Collect every Chinese source string into i18n/catalog.json, keeping existing translations.

usage: python -B scripts/i18n/extract.py [--check]

Sources (Simplified Chinese is the source language and the lookup key):
  * QML: the argument of every qsTr("...") in qml/Veyra;
  * C++ UI: tr("...") / QCoreApplication::translate(..., "...") and the other Chinese literals in
    src/ui and apps/veyra-qml (fixed tables and wide-string messages shown through uiText());
  * engine messages: Chinese wide literals in src/** that reach the interface as status or error
    text. std::format slots ({}, {:.1f}) stay in the key; the runtime matches them as patterns.
Adjacent C++ literals ("a" "b") are joined, as the compiler does.

Entries no source uses any more are dropped. --check reports placeholder mismatches
({} count, %1..%9) and missing translations without writing anything.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / 'i18n/catalog.json'
LANGS = ('zh-TW', 'en', 'ja')
HAN = re.compile('[一-鿿]')
ESC = {'n': '\n', 't': '\t', '"': '"', "'": "'", '\\': '\\', '0': '\0'}


def unescape(body):
    out, i = [], 0
    while i < len(body):
        c = body[i]
        if c == '\\' and i + 1 < len(body):
            nxt = body[i + 1]
            if nxt == 'u' and i + 5 < len(body):
                out.append(chr(int(body[i + 2:i + 6], 16)))
                i += 6
                continue
            out.append(ESC.get(nxt, nxt))
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def qml_strings(text):
    """Yield (source, disambiguation) for every qsTr("...") / qsTr("...", "...")."""
    for m in re.finditer(r'qsTr\(\s*(["\'])', text):
        q = m.group(1)
        i = j = m.end()
        while j < len(text) and text[j] != q:
            j += 2 if text[j] == '\\' else 1
        ctx = re.match(r'\s*,\s*"([^"]*)"\s*\)', text[j + 1:])
        yield unescape(text[i:j]), ctx.group(1) if ctx else ''


# tr("关闭", "off"): a C++ string with a disambiguation.
CPP_DISAMBIGUATED = re.compile(r'\btr\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"([^"]*)"\s*\)')


def cpp_literal_runs(text):
    """Yield (prefix_before_run, joined_value) for each run of adjacent string literals."""
    i, n = 0, len(text)
    tokens = []  # (start, end, value)
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if text[i] == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            i = j + 1
            continue
        if text[i] == '"':
            start = i
            if i >= 1 and text[i - 1] == 'L':
                start = i - 1
            elif i >= 2 and text[i - 2:i] == 'u8':
                start = i - 2
            if text[start - 1:start] == 'R' or text[i - 1:i] == 'R':
                j = text.find(')"', i)
                i = n if j < 0 else j + 2
                continue
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            tokens.append((start, j + 1, unescape(text[i + 1:j])))
            i = j + 1
            continue
        i += 1
    run = []
    for tok in tokens:
        if run and re.fullmatch(r'\s*', text[run[-1][1]:tok[0]]):
            run.append(tok)
            continue
        if run:
            yield text[max(0, run[0][0] - 60):run[0][0]], ''.join(t[2] for t in run)
        run = [tok]
    if run:
        yield text[max(0, run[0][0] - 60):run[0][0]], ''.join(t[2] for t in run)


LOG_CALL = re.compile(r'(log::(info|warn|error|debug)|logUi|veyra::log::\w+)\(\s*"[^"]*",\s*(std::format\()?\s*$')


def collect():
    found = {}

    def add(s, where, ctx=''):
        if s and HAN.search(s):
            found.setdefault((s, ctx), where)

    for f in sorted((ROOT / 'qml/Veyra').glob('*.qml')):
        for s, ctx in qml_strings(f.read_text(encoding='utf-8')):
            add(s, 'qml/' + f.name, ctx)
    cpp = list((ROOT / 'src/ui').glob('*.cpp')) + list((ROOT / 'apps/veyra-qml').glob('*.cpp'))
    for d in ('engine', 'source', 'gfx', 'pipeline', 'moonlight', 'xbox', 'remoteplay', 'sink', 'media', 'ngx', 'base'):
        cpp += list((ROOT / 'src' / d).rglob('*.cpp'))
        # Default member values (a status set in a class body) live in the headers.
        cpp += list((ROOT / 'include/veyra' / d).rglob('*.h'))
    for f in sorted(set(cpp)):
        rel = f.relative_to(ROOT).as_posix()
        source_text = f.read_text(encoding='utf-8', errors='replace')
        for m in CPP_DISAMBIGUATED.finditer(source_text):
            add(unescape(m.group(1)), rel, m.group(2))
        for before, value in cpp_literal_runs(source_text):
            # Log lines stay in the source language and are not catalogued.
            if LOG_CALL.search(before):
                continue
            add(value, rel)
    return found


def placeholders(s):
    return (len(re.findall(r'\{[^{}]*\}', s)), sorted(set(re.findall(r'%\d', s))))


def main():
    old = {}
    if CATALOG.exists():
        for e in json.loads(CATALOG.read_text(encoding='utf-8')).get('entries', []):
            old[(e['zh'], e.get('ctx', ''))] = e
    found = collect()
    entries = []
    for key in sorted(found):
        zh, ctx = key
        e = {'zh': zh}
        if ctx:
            e['ctx'] = ctx
        e['src'] = found[key]
        for lang in LANGS:
            e[lang] = old.get(key, {}).get(lang, '')
        if old.get(key, {}).get('note'):
            e['note'] = old[key]['note']
        entries.append(e)
    problems = 0
    missing = {lang: 0 for lang in LANGS}
    for e in entries:
        want = placeholders(e['zh'])
        for lang in LANGS:
            t = e[lang]
            if not t:
                missing[lang] += 1
                continue
            if placeholders(t) != want:
                problems += 1
                print(f'placeholder mismatch [{lang}] {e["zh"]!r} -> {t!r}')
    print(f'entries={len(entries)} dropped={sorted(set(old) - set(found))[:8]} missing={missing} placeholder problems={problems}')
    if '--check' in sys.argv:
        sys.exit(1 if problems else 0)
    CATALOG.parent.mkdir(exist_ok=True)
    CATALOG.write_text(json.dumps({'entries': entries}, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
