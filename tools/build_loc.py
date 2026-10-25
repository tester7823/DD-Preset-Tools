#!/usr/bin/env python3
# strings.py → mod 의 localization 폴더 (string_table.xml + 언어별 .loc2) 생성
import os, sys
from collections import OrderedDict
sys.path.insert(0, os.path.dirname(__file__))
import strings, loc2
out = sys.argv[1]                       # <모드 폴더>
GAME = ["english", "koreana", "schinese", "japanese", "russian"]   # 게임이 지원하는 언어
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
# 번체 중국어: 게임에 번체 언어 슬롯이 없어 간체(schinese) 자리에 덮어쓰는 선택 파일
tc = os.path.join(out, 'localization_tchinese'); os.makedirs(tc, exist_ok=True)
open(os.path.join(tc, 'dd_preset_tools_tchinese.string_table.xml'), 'w', encoding='utf-8').write(xml(['tchinese'], {'tchinese': 'schinese'}))
ents = OrderedDict(('ddpt_' + r[0], [r[idx['tchinese']]]) for r in strings.S)
open(os.path.join(tc, 'dd_preset_tools_schinese.loc2'), 'wb').write(loc2.encode(ents))
open(os.path.join(tc, 'README.txt'), 'w', encoding='utf-8').write(
"""Traditional Chinese (繁體中文)
Darkest Dungeon has no Traditional Chinese language slot. If you play with a Traditional Chinese
patch that replaces Simplified Chinese, copy dd_preset_tools_schinese.loc2 from this folder into
..\\localization\\ (overwrite the file there).

暗黑地牢沒有繁體中文語言選項。若您使用以繁體取代簡體中文的漢化補丁，請將本資料夾中的
dd_preset_tools_schinese.loc2 複製到 ..\\localization\\ 並覆蓋原檔。
""")
print('ok', len(strings.S), 'strings')
