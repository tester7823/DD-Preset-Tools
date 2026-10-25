#!/usr/bin/env python3
# strings.py → mod 의 localization 폴더 (string_table.xml + 언어별 .loc2) 생성
import os, sys
from collections import OrderedDict
sys.path.insert(0, os.path.dirname(__file__))
import strings, loc2
out = sys.argv[1]                       # <모드 폴더>
GAME = strings.LANGS   # 게임이 지원하는 12개 언어 전부
idx = {l: i + 1 for i, l in enumerate(strings.LANGS)}

def xml(langs, rename=None):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<root>']
    for l in langs:
        lines.append(f'  <language id="{(rename or {}).get(l, l)}">')
        for row in strings.S:
            lines.append(f'    <entry id="ddpt_{row[0]}"><![CDATA[{row[idx[l]]}]]></entry>')
        lines.append('  </language>')
    lines.append('</root>')
    return '\n'.join(lines) + '\n'

loc = os.path.join(out, 'localization'); os.makedirs(loc, exist_ok=True)
open(os.path.join(loc, 'dd_preset_tools.string_table.xml'), 'w', encoding='utf-8').write(xml(GAME))
for l in GAME:
    ents = OrderedDict(('ddpt_' + r[0], [r[idx[l]]]) for r in strings.S)
    open(os.path.join(loc, f'dd_preset_tools_{l}.loc2'), 'wb').write(loc2.encode(ents))
print('ok', len(strings.S), 'strings')
