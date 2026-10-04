"""QuakeScope の回路図（hardware/schematic.svg）を描く。

部品の記号は JIS C 0617（IEC 60617）に合わせる（抵抗は長方形）。
配線は「ネット名の旗」でつなぎ、線が交差しないようにしている。
実行: python3 tools/hw/schematic.py
"""
from pathlib import Path

W, H = 1280, 900
INK, MUTED, LINE = '#1b1e24', '#6b7079', '#c9c4b8'
NET = {'5V': '#c0392b', '3V3': '#d9822b', 'GND': '#1b1e24', 'SDA': '#2d6cc4', 'SCL': '#1d8a5c', 'SIG': '#6a4fb3'}
out = []


def add(s):
    out.append(s)


def text(x, y, s, size=13, anchor='start', fill=INK, weight=400, family='sans'):
    fam = '"IBM Plex Mono", Menlo, Consolas, monospace' if family == 'mono' else '"IBM Plex Sans JP", "Noto Sans JP", "Hiragino Sans", sans-serif'
    add(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" fill="{fill}" font-weight="{weight}" font-family=\'{fam}\'>{s}</text>')


def line(pts, color=INK, w=1.8, dash=None):
    d = ' '.join(f'{"M" if i == 0 else "L"}{x},{y}' for i, (x, y) in enumerate(pts))
    extra = f' stroke-dasharray="{dash}"' if dash else ''
    add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"{extra}/>')


def dot(x, y, color=INK):
    add(f'<circle cx="{x}" cy="{y}" r="3.6" fill="{color}"/>')


def flag(x, y, net, side='right'):
    """ネット名の旗（同じ名前どうしがつながっている）。"""
    c = NET.get(net.split('_')[0] if net not in NET else net, NET['SIG'])
    label = {'5V': '+5V', '3V3': '+3.3V'}.get(net, net)
    w = 9 + 7.6 * len(label)
    if side == 'right':
        add(f'<path d="M{x},{y} l8,-9 h{w} v18 h-{w} z" fill="#fff" stroke="{c}" stroke-width="1.6"/>')
        text(x + 12, y + 4.5, label, 12, fill=c, weight=600, family='mono')
    else:
        add(f'<path d="M{x},{y} l-8,-9 h-{w} v18 h{w} z" fill="#fff" stroke="{c}" stroke-width="1.6"/>')
        text(x - 12, y + 4.5, label, 12, 'end', fill=c, weight=600, family='mono')


def gnd(x, y):
    line([(x, y), (x, y + 10)])
    line([(x - 11, y + 10), (x + 11, y + 10)], w=2.2)
    line([(x - 7, y + 15), (x + 7, y + 15)], w=2)
    line([(x - 3, y + 20), (x + 3, y + 20)], w=1.8)


def rail(x, y, net):
    c = NET[net]
    line([(x, y), (x, y - 12)], c)
    line([(x - 10, y - 12), (x + 10, y - 12)], c, 2.4)
    text(x, y - 18, {'5V': '+5V', '3V3': '+3.3V'}[net], 12, 'middle', c, 600, 'mono')


def resistor_h(x, y, ref, val):
    """横向きの抵抗（x から右へ 60）。"""
    line([(x, y), (x + 14, y)])
    add(f'<rect x="{x + 14}" y="{y - 7}" width="32" height="14" fill="#fff" stroke="{INK}" stroke-width="1.8"/>')
    line([(x + 46, y), (x + 60, y)])
    text(x + 30, y - 12, ref, 11.5, 'middle', MUTED, family='mono')
    text(x + 30, y + 22, val, 12, 'middle', INK, 600, 'mono')


def resistor_v(x, y, ref, val):
    """縦向きの抵抗（y から下へ 60）。"""
    line([(x, y), (x, y + 14)])
    add(f'<rect x="{x - 7}" y="{y + 14}" width="14" height="32" fill="#fff" stroke="{INK}" stroke-width="1.8"/>')
    line([(x, y + 46), (x, y + 60)])
    text(x + 12, y + 28, ref, 11.5, fill=MUTED, family='mono')
    text(x + 12, y + 43, val, 12, fill=INK, weight=600, family='mono')


def box(x, y, w, h, title, sub, pins_l=(), pins_r=(), pitch=30, top=46, fill='#fffdf8'):
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="{fill}" stroke="{INK}" stroke-width="2"/>')
    text(x + w / 2, y + 22, title, 14.5, 'middle', INK, 700)
    if sub:
        text(x + w / 2, y + 38, sub, 11.5, 'middle', MUTED)
    pts = {}
    for i, (name, label) in enumerate(pins_l):
        py = y + top + 12 + i * pitch
        line([(x - 26, py), (x, py)])
        text(x + 8, py + 4.5, label, 12, family='mono')
        pts[name] = (x - 26, py)
    for i, (name, label) in enumerate(pins_r):
        py = y + top + 12 + i * pitch
        line([(x + w, py), (x + w + 26, py)])
        text(x + w - 8, py + 4.5, label, 12, 'end', family='mono')
        pts[name] = (x + w + 26, py)
    return pts


def draw():
    add(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
    # 方眼（薄く）
    add('<defs><pattern id="g" width="20" height="20" patternUnits="userSpaceOnUse"><path d="M20 0H0V20" fill="none" stroke="#eef1f5" stroke-width="1"/></pattern></defs>')
    add(f'<rect x="16" y="16" width="{W - 32}" height="{H - 32}" fill="url(#g)" stroke="{LINE}" stroke-width="1.2"/>')

    # 表題
    text(36, 52, 'QuakeScope 回路図', 22, weight=700)
    text(36, 76, 'ESP32 版 ハードウェア v1.0 ・ 部品番号は部品表（hardware/BOM.md）と同じ', 12.5, fill=MUTED)

    # ---------------------------------------------------- U1 ESP32
    u1 = box(540, 140, 230, 400, 'U1  ESP32-DevKitC-32E', 'ESP32-WROOM-32E（技適あり）',
             pins_l=[('5V', '5V'), ('3V3', '3V3'), ('GND', 'GND')],
             pins_r=[('21', 'GPIO21'), ('22', 'GPIO22'), ('25', 'GPIO25'), ('26', 'GPIO26'), ('27', 'GPIO27'), ('14', 'GPIO14'), ('33', 'GPIO33')],
             pitch=44, top=50)
    # USB 給電
    add(f'<rect x="560" y="490" width="74" height="30" rx="4" fill="#fff" stroke="{MUTED}" stroke-dasharray="4 3"/>')
    text(597, 510, 'micro USB', 11.5, 'middle', MUTED, family='mono')
    text(645, 502, '← USB-AC アダプター', 11.5, fill=MUTED)
    text(658, 518, '5V・1A 以上', 11.5, fill=MUTED)
    flag(*u1['5V'], '5V', 'left')
    flag(*u1['3V3'], '3V3', 'left')
    x, y = u1['GND']; line([(x, y), (x - 20, y), (x - 20, y + 14)]); gnd(x - 20, y + 14)
    labels = {'21': 'SDA', '22': 'SCL', '25': 'BUZ', '26': 'LED_R', '27': 'LED_G', '14': 'LED_B', '33': 'BTN'}
    for k, n in labels.items():
        flag(*u1[k], n, 'right')
    # 予備の電源のコンデンサー
    cx, cy = 330, 380
    rail(cx, cy, '5V')
    line([(cx, cy), (cx, cy + 22)], NET['5V'])
    line([(cx - 14, cy + 22), (cx + 14, cy + 22)], w=2.4)
    add(f'<path d="M{cx - 14},{cy + 32} q14,-8 28,0" fill="none" stroke="{INK}" stroke-width="2.4"/>')
    text(cx - 18, cy + 21, '+', 12, 'end')
    line([(cx, cy + 30), (cx, cy + 50)]); gnd(cx, cy + 50)
    text(cx + 22, cy + 22, 'C1', 11.5, fill=MUTED, family='mono')
    text(cx + 22, cy + 37, '100µF 16V', 12, weight=600, family='mono')
    text(cx + 22, cy + 56, 'ブザーが鳴ったときの', 11.5, fill=MUTED)
    text(cx + 22, cy + 72, '電圧の落ち込みを防ぐ', 11.5, fill=MUTED)

    # ---------------------------------------------------- U2 GY-521
    u2 = box(990, 110, 250, 180, 'U2  GY-521', 'MPU6050 加速度センサー（I²C 0x68）',
             pins_l=[('VCC', 'VCC'), ('GND', 'GND'), ('SCL', 'SCL'), ('SDA', 'SDA'), ('AD0', 'AD0')], pitch=26, top=50)
    flag(*u2['VCC'], '5V', 'left')
    x, y = u2['GND']; line([(x, y), (x - 30, y)]); gnd(x - 30, y)
    flag(*u2['SCL'], 'SCL', 'left'); flag(*u2['SDA'], 'SDA', 'left')
    x, y = u2['AD0']; line([(x, y), (x - 14, y), (x - 14, y + 12)]); gnd(x - 14, y + 12)
    text(1115, 308, 'VCC は 5V（基板の 3.3V レギュレーターを通す）', 11, 'middle', MUTED)

    # ---------------------------------------------------- U3 OLED
    u3 = box(990, 350, 250, 150, 'U3  OLED 0.96″', 'SSD1306 128×64（I²C 0x3C）',
             pins_l=[('VCC', 'VCC'), ('GND', 'GND'), ('SCL', 'SCL'), ('SDA', 'SDA')], pitch=24, top=46)
    flag(*u3['VCC'], '3V3', 'left')
    x, y = u3['GND']; line([(x, y), (x - 30, y)]); gnd(x - 30, y)
    flag(*u3['SCL'], 'SCL', 'left'); flag(*u3['SDA'], 'SDA', 'left')
    text(1115, 518, 'ピンの並びは製品で違う。基板の印刷を見る', 11, 'middle', MUTED)

    # ---------------------------------------------------- ブザーの駆動（Q1）
    bx, by = 120, 620
    text(bx - 70, by - 20, 'ブザー（警報音）', 13.5, weight=700)
    rail(bx + 150, by + 10, '5V')
    # BZ1
    line([(bx + 150, by + 10), (bx + 150, by + 30)], NET['5V'])
    add(f'<rect x="{bx + 136}" y="{by + 30}" width="28" height="34" rx="3" fill="#fff" stroke="{INK}" stroke-width="1.8"/>')
    add(f'<path d="M{bx + 164},{by + 38} q10,9 0,18 M{bx + 170},{by + 33} q15,14 0,28" fill="none" stroke="{INK}" stroke-width="1.6"/>')
    text(bx + 192, by + 44, 'BZ1', 11.5, fill=MUTED, family='mono')
    text(bx + 192, by + 59, '電磁ブザー（自励なし）', 12, weight=600)
    # D1（ブザーと並列、カソードを 5V へ）
    line([(bx + 150, by + 20), (bx + 100, by + 20), (bx + 100, by + 34)])
    add(f'<path d="M{bx + 90},{by + 60} h20 l-10,-16 z" fill="{INK}"/>')
    line([(bx + 90, by + 44), (bx + 110, by + 44)], w=2.4)
    line([(bx + 100, by + 34), (bx + 100, by + 44)])
    line([(bx + 100, by + 60), (bx + 100, by + 74), (bx + 150, by + 74)])
    dot(bx + 150, by + 20); dot(bx + 150, by + 74)
    text(bx + 56, by + 46, 'D1', 11.5, fill=MUTED, family='mono')
    text(bx + 30, by + 61, '1N4148', 12, weight=600, family='mono')
    line([(bx + 150, by + 64), (bx + 150, by + 92)])
    # Q1（NPN）
    qx, qy = bx + 150, by + 120
    add(f'<circle cx="{qx - 6}" cy="{qy}" r="22" fill="#fff" stroke="{INK}" stroke-width="1.6"/>')
    line([(qx - 16, qy - 13), (qx - 16, qy + 13)], w=2.6)
    line([(qx - 16, qy - 6), (qx, qy - 22), (qx, qy - 28)])
    line([(qx, qy - 28), (qx, by + 92)])
    line([(qx - 16, qy + 6), (qx, qy + 22), (qx, qy + 40)])
    add(f'<path d="M{qx},{qy + 22} l-9,-2 l5,-7 z" fill="{INK}"/>')
    text(qx + 22, qy - 2, 'Q1', 11.5, fill=MUTED, family='mono')
    text(qx + 22, qy + 13, '2SC1815（NPN）', 12, weight=600, family='mono')
    gnd(qx, qy + 40)
    # ベース: R1 1k、R2 10k（起動中に鳴らないよう GND へ）
    line([(qx - 16, qy), (qx - 46, qy)])
    resistor_h(qx - 106, qy, 'R1', '1kΩ')
    flag(qx - 106, qy, 'BUZ', 'left')
    dot(qx - 46, qy)
    line([(qx - 46, qy), (qx - 46, qy + 4)])
    resistor_v(qx - 46, qy + 4, 'R2', '10kΩ')
    gnd(qx - 46, qy + 64)

    # ---------------------------------------------------- RGB LED
    lx, ly = 560, 620
    text(lx - 30, ly - 20, '状態の LED（RGB・カソード共通）', 13.5, weight=700)
    for i, (net, ref, val, col) in enumerate([('LED_R', 'R3', '330Ω', '#d0202d'), ('LED_G', 'R4', '100Ω', '#1d8a5c'), ('LED_B', 'R5', '100Ω', '#2d6cc4')]):
        yy = ly + 18 + i * 60
        flag(lx, yy, net, 'left')
        resistor_h(lx, yy, ref, val)
        # LED（右向き）
        x0 = lx + 60
        line([(x0, yy), (x0 + 20, yy)])
        add(f'<path d="M{x0 + 20},{yy - 10} v20 l16,-10 z" fill="#fff" stroke="{INK}" stroke-width="1.8"/>')
        line([(x0 + 36, yy - 10), (x0 + 36, yy + 10)], w=2.4)
        add(f'<path d="M{x0 + 24},{yy - 14} l7,-9 M{x0 + 31},{yy - 14} l7,-9" stroke="{col}" stroke-width="1.6"/>')
        line([(x0 + 36, yy), (x0 + 70, yy)])
        text(x0 + 28, yy + 24, ['赤', '緑', '青'][i], 11, 'middle', col, 700)
        if i:
            dot(x0 + 70, yy)
    line([(lx + 130, ly + 18), (lx + 130, ly + 18 + 120 + 12)])
    gnd(lx + 130, ly + 150)
    text(lx + 140, ly + 28, 'LED1', 11.5, fill=MUTED, family='mono')
    text(lx + 140, ly + 43, '5mm 拡散', 12, weight=600)

    # ---------------------------------------------------- ボタン
    sx, sy = 930, 630
    text(sx - 10, sy - 20, 'ボタン', 13.5, weight=700)
    flag(sx, sy + 18, 'BTN', 'left')
    line([(sx, sy + 18), (sx + 30, sy + 18)])
    dot(sx + 30, sy + 18); dot(sx + 76, sy + 18)
    line([(sx + 33, sy + 10), (sx + 72, sy - 2)], w=2)
    line([(sx + 52, sy + 4), (sx + 52, sy - 8)], w=1.4)
    line([(sx + 44, sy - 8), (sx + 60, sy - 8)], w=1.8)
    line([(sx + 76, sy + 18), (sx + 106, sy + 18), (sx + 106, sy + 30)])
    gnd(sx + 106, sy + 30)
    text(sx + 30, sy + 46, 'SW1', 11.5, fill=MUTED, family='mono')
    text(sx + 30, sy + 61, '押しボタン（モーメンタリ）', 12, weight=600)
    text(sx + 30, sy + 78, 'GPIO33 は内部でプルアップ', 11, fill=MUTED)

    # ---------------------------------------------------- 注記
    nx, ny = 36, 120
    notes = ['注意', '・I²C（SDA・SCL）は 3.3V。5V に引き上げない（ESP32 が壊れる）',
             '・GY-521 と OLED には I²C の引き上げ抵抗が載っている。足さない',
             '・GPIO25 はブザー専用。起動中は R2 で止める',
             '・MPU6050 は基板にねじで固定し、ケースの底板と一体にする']
    for i, s in enumerate(notes):
        text(nx, ny + i * 20, s, 12.5 if i else 13.5, fill=INK if i == 0 else '#3d424b', weight=700 if i == 0 else 400)

    # 凡例
    lx2, ly2 = 930, 800
    text(lx2, ly2, 'ネットの色', 12, fill=MUTED, weight=700)
    for i, (n, lab) in enumerate([('5V', '+5V'), ('3V3', '+3.3V'), ('SDA', 'SDA'), ('SCL', 'SCL'), ('SIG', '信号')]):
        xx = lx2 + (i % 3) * 100
        yy = ly2 + 18 + (i // 3) * 20
        line([(xx, yy - 4), (xx + 18, yy - 4)], NET[n], 3)
        text(xx + 24, yy, lab, 11.5, family='mono')
    text(W - 36, H - 30, 'QuakeScope ・ MIT License', 11, 'end', MUTED)


draw()
svg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">' + ''.join(out) + '</svg>\n'
p = Path(__file__).resolve().parents[2] / 'hardware' / 'schematic.svg'
p.write_text(svg, encoding='utf-8')
print('作りました', p.relative_to(p.parents[1]), len(svg), 'bytes')
