"""揺れの判定エンジン（core/src）を、ファームウェアのスケッチの中（firmware/QuakeScope/src/qs）に写す。
Arduino IDE はスケッチのフォルダーの外のファイルを読まないため。
    python3 tools/sync_core.py          # 写す
    python3 tools/sync_core.py --check  # 同じかどうかだけ確かめる（CI）
"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / 'core' / 'src'
DST = ROOT / 'firmware' / 'QuakeScope' / 'src' / 'qs'
SKIP = {'synth.h', 'synth.cpp', 'wasm_api.cpp'}  # 本体では使わない
HEADER = '// このファイルは core/src から写したものです。編集は core/src で行い、tools/sync_core.py を実行してください。\n'

def main():
    check = '--check' in sys.argv
    DST.mkdir(parents=True, exist_ok=True)
    want = {}
    for p in sorted(SRC.iterdir()):
        if p.suffix in ('.h', '.cpp') and p.name not in SKIP:
            want[p.name] = HEADER + p.read_text(encoding='utf-8')
    bad = []
    for name, text in want.items():
        d = DST / name
        if not d.exists() or d.read_text(encoding='utf-8') != text:
            bad.append(name)
            if not check:
                d.write_text(text, encoding='utf-8')
    extra = [p.name for p in DST.iterdir() if p.name not in want]
    if check:
        if bad or extra:
            print('core と firmware の写しが違います:', bad + extra)
            sys.exit(1)
        print('core と firmware の写しは同じです')
    else:
        for e in extra:
            (DST / e).unlink()
        print('写しました:', ', '.join(sorted(want)))

main()
