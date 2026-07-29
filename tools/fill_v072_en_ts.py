# -*- coding: utf-8 -*-
# v0.7.2: fill EN translations for newly added UI strings only.
import io, os, re, sys

TS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                  'resources', 'i18n', 'xtranslate_en.ts')
PAIRS = {
    '云端（Edge，推荐）': 'Cloud (Edge, recommended)',
    '系统嗓音': 'System voice',
    '朗读引擎': 'TTS engine',
    '该语言暂无内置 Edge 嗓音映射，将使用多语言默认嗓音':
        'No built-in Edge voice mapping for this language; a multilingual default voice will be used',
    '云端语音为非官方免费接口，需联网，仅供个人学习；失败时自动回退系统嗓音。':
        'Cloud voice uses an unofficial free endpoint, requires network access and is for personal study only; falls back to the system voice on failure.',
    '本机没有可用的系统语音引擎；云端（Edge）引擎仍可用，但失败时无法回退本机嗓音。':
        'No system speech engine is available on this machine; the cloud (Edge) engine still works, but cannot fall back to a local voice on failure.',
    '热键冲突提示（默认关，仅写日志）':
        'Hotkey conflict notice (off by default, log only)',
}

with io.open(TS, 'r', encoding='utf-8') as f:
    text = f.read()

count = 0
for src, dst in PAIRS.items():
    pat = ('<source>' + re.escape(src) + '</source>\n        '
           '<translation type="unfinished"></translation>')
    rep = ('<source>' + src + '</source>\n        '
           '<translation>' + dst + '</translation>')
    text, n = re.subn(pat, rep, text)
    count += n
    if n == 0:
        print('MISS:', src)

with io.open(TS, 'w', encoding='utf-8') as f:
    f.write(text)
print('filled', count)
