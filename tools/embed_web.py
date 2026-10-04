"""firmware/web/index.html を gzip で縮めて、ファームウェアに埋め込む C++ のヘッダーにする。
    python3 tools/embed_web.py          # 作る
    python3 tools/embed_web.py --check  # 最新かどうかだけ確かめる（CI）
"""
import gzip, hashlib, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / 'firmware' / 'web' / 'index.html'
DST = ROOT / 'firmware' / 'QuakeScope' / 'src' / 'app' / 'web_page.h'

def build():
    raw = SRC.read_bytes()
    gz = gzip.compress(raw, compresslevel=9, mtime=0)
    digest = hashlib.sha256(raw).hexdigest()[:16]
    rows = [', '.join('0x%02x' % b for b in gz[i:i + 20]) for i in range(0, len(gz), 20)]
    return ('// 自動生成: tools/embed_web.py（元: firmware/web/index.html、sha256 %s）\n'
            '// 本体の Web 画面（gzip で圧縮、%d → %d バイト）\n'
            '#pragma once\n#include <Arduino.h>\n\nnamespace app {\n\n'
            'constexpr size_t kWebPageLen = %d;\nconst uint8_t kWebPage[] PROGMEM = {\n    %s\n};\n\n}  // namespace app\n'
            % (digest, len(raw), len(gz), len(gz), ',\n    '.join(rows)))

text = build()
if '--check' in sys.argv:
    ok = DST.exists() and DST.read_text() == text
    print('web_page.h は最新です' if ok else 'web_page.h が古いです（tools/embed_web.py を実行してください）')
    sys.exit(0 if ok else 1)
DST.write_text(text)
print('作りました', DST.relative_to(ROOT), len(text), 'bytes')
