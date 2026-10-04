"""ユニバーサル基板の部品配置と配線（hardware/layout.json・perfboard.svg・wiring.md）を作る。

座標は「穴の番号」。左上の穴が (1, 1)、右へ x、下へ y。2.54 mm ピッチ。
部品面（上から見た図）と、はんだ面（裏から見た図・左右が反転）の 2 枚を描く。
実行: python3 tools/hw/layout.py
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PITCH = 2.54
BOARD = {'w': 95.0, 'h': 72.0, 't': 1.6, 'origin': [8.0, 6.0], 'cols': 34, 'rows': 25,
         'mount': [[4.0, 4.0], [91.0, 4.0], [4.0, 68.0], [91.0, 68.0]], 'mountD': 3.2,
         'example': '95×72 mm・2.54 mm ピッチ（例: サンハヤト ICB-293GV）'}

# ESP32-DevKitC のピン（アンテナ側が 1 番、USB 側が 19 番）。公式の Getting Started Guide の表と同じ並び
ESP_J2 = ['3V3', 'EN', 'VP', 'VN', 'IO34', 'IO35', 'IO32', 'IO33', 'IO25', 'IO26', 'IO27', 'IO14', 'IO12', 'GND', 'IO13', 'D2', 'D3', 'CMD', '5V']
ESP_J3 = ['GND', 'IO23', 'IO22', 'TX', 'RX', 'IO21', 'GND', 'IO19', 'IO18', 'IO5', 'IO17', 'IO16', 'IO4', 'IO0', 'IO2', 'IO15', 'D1', 'D0', 'CLK']

parts = []


def part(ref, name, pins, outline, height, kind='box', note='', color='#d8d2c4'):
    parts.append({'ref': ref, 'name': name, 'pins': pins, 'outline': outline, 'height': height, 'kind': kind, 'note': note, 'color': color})


# U1: ESP32 はピンソケット（1×19 を 2 本）に挿す。USB を左、アンテナを右へ
part('U1', 'ESP32-DevKitC-32E', {f'J2.{k + 1}': [20 - (k + 1), 3] for k in range(19)} | {f'J3.{k + 1}': [20 - (k + 1), 13] for k in range(19)},
     [-1.0, 1.6, 21.6, 14.4], 16.0, 'esp', 'ピンソケットに挿す（高さ約 8.5 mm）', '#2b2f36')
pinname = {f'J2.{k + 1}': n for k, n in enumerate(ESP_J2)} | {f'J3.{k + 1}': n for k, n in enumerate(ESP_J3)}
part('U2', 'GY-521（MPU6050）', {'VCC': [2, 17], 'GND': [3, 17], 'SCL': [4, 17], 'SDA': [5, 17], 'XDA': [6, 17], 'XCL': [7, 17], 'AD0': [8, 17], 'INT': [9, 17]},
     [1.2, 16.4, 9.8, 22.9], 5.0, 'box', 'ピンヘッダーの黒い台を抜いて低く付け、基板とのすき間に薄い両面テープ（クッションのないもの）を挟んで密着させる', '#1f4fa8')
part('BZ1', '電磁ブザー 12 mm', {'+': [28, 4], '-': [31, 4]}, [29.5, 4, 2.36], 9.5, 'circle', '足の間隔 7.6 mm', '#26292e')
part('D1', '1N4148', {'K': [28, 7], 'A': [31, 7]}, [28, 7, 31, 7], 2.0, 'diode', 'カソード（帯）を左（5V 側）')
part('Q1', '2SC1815', {'E': [28, 10], 'C': [29, 10], 'B': [30, 10]}, [27.5, 9.4, 30.5, 10.7], 5.0, 'to92', '平らな面を手前（下）に、左から E C B')
part('R1', '1kΩ', {'a': [30, 13], 'b': [33, 13]}, [30, 13, 33, 13], 2.0, 'res')
part('R2', '10kΩ', {'a': [30, 16], 'b': [33, 16]}, [30, 16, 33, 16], 2.0, 'res')
part('C1', '100µF 16V', {'+': [24, 4], '-': [24, 5]}, [24, 4.5, 1.24], 11.0, 'cap', '足の長い方（+）を上')
part('R3', '330Ω', {'a': [13, 17], 'b': [16, 17]}, [13, 17, 16, 17], 2.0, 'res')
part('R4', '100Ω', {'a': [13, 19], 'b': [16, 19]}, [13, 19, 16, 19], 2.0, 'res')
part('R5', '100Ω', {'a': [13, 21], 'b': [16, 21]}, [13, 21, 16, 21], 2.0, 'res')
part('J3', 'OLED へ（L 型ピンヘッダー 1×4）', {'GND': [22, 20], 'VCC': [23, 20], 'SCL': [24, 20], 'SDA': [25, 20]}, [21.5, 19.5, 25.5, 20.5], 5.0, 'header')
part('J4', 'LED へ（L 型ピンヘッダー 1×4）', {'R': [22, 23], 'G': [23, 23], 'B': [24, 23], 'K': [25, 23]}, [21.5, 22.5, 25.5, 23.5], 5.0, 'header')
part('J5', 'ボタンへ（L 型ピンヘッダー 1×2）', {'BTN': [27, 23], 'GND': [28, 23]}, [26.5, 22.5, 28.5, 23.5], 5.0, 'header')

# 配線（ネットごとに、順につなぐ）
NETS = [
    ('5V', '#c0392b', ['U1.J2.19', 'U2.VCC', 'C1.+', 'BZ1.+', 'D1.K']),
    ('3V3', '#d9822b', ['U1.J2.1', 'J3.VCC']),
    ('GND', '#2b2f36', ['U1.J3.1', 'J3.GND', 'J4.K', 'J5.GND']),
    ('GND', '#2b2f36', ['U1.J3.7', 'U2.GND', 'U2.AD0']),
    ('GND', '#2b2f36', ['U1.J3.1', 'Q1.E', 'R2.b']),
    ('GND', '#2b2f36', ['Q1.E', 'C1.-']),
    ('SDA', '#2d6cc4', ['U1.J3.6', 'U2.SDA', 'J3.SDA']),
    ('SCL', '#1d8a5c', ['U1.J3.3', 'U2.SCL', 'J3.SCL']),
    ('BUZ', '#6a4fb3', ['U1.J2.9', 'R1.b']),
    ('Q1B', '#6a4fb3', ['R1.a', 'Q1.B', 'R2.a']),
    ('BZ-', '#8d6e63', ['BZ1.-', 'D1.A', 'Q1.C']),
    ('LED_R', '#d0202d', ['U1.J2.10', 'R3.a']), ('LED_R', '#d0202d', ['R3.b', 'J4.R']),
    ('LED_G', '#1d8a5c', ['U1.J2.11', 'R4.a']), ('LED_G', '#1d8a5c', ['R4.b', 'J4.G']),
    ('LED_B', '#2d6cc4', ['U1.J2.12', 'R5.a']), ('LED_B', '#2d6cc4', ['R5.b', 'J4.B']),
    ('BTN', '#6a4fb3', ['U1.J2.8', 'J5.BTN']),
]
NETNAME = {'5V': '+5V', '3V3': '+3.3V', 'Q1B': 'Q1 ベース', 'BZ-': 'ブザー −'}


def pin(ref_pin):
    ref, p = ref_pin.split('.', 1)
    for pt in parts:
        if pt['ref'] == ref:
            return pt['pins'][p]
    raise KeyError(ref_pin)


def label(ref_pin):
    ref, p = ref_pin.split('.', 1)
    if ref == 'U1':
        return f'U1 {pinname[p]}'
    return f'{ref} {p}' if p not in ('a', 'b') else f'{ref}'


def mm(x, y):
    return BOARD['origin'][0] + (x - 1) * PITCH, BOARD['origin'][1] + (y - 1) * PITCH


# 部品の穴が重なっていないか
used = {}
for pt in parts:
    for p, xy in pt['pins'].items():
        k = tuple(xy)
        assert k not in used, f'穴が重なっています: {pt["ref"]}.{p} と {used[k]}'
        assert 1 <= xy[0] <= BOARD['cols'] and 1 <= xy[1] <= BOARD['rows'], f'基板の外: {pt["ref"]}.{p}'
        used[k] = f'{pt["ref"]}.{p}'

wires = []
for net, color, chain in NETS:
    for a, b in zip(chain, chain[1:]):
        wires.append({'net': net, 'color': color, 'from': a, 'to': b, 'a': pin(a), 'b': pin(b)})


def route(a, b, i):
    """L 字に曲げる（どちらを先に曲げるかは、番号で入れ替えて重なりを減らす）。"""
    (x1, y1), (x2, y2) = a, b
    if x1 == x2 or y1 == y2:
        return [a, b]
    return [a, [x2, y1], b] if i % 2 == 0 else [a, [x1, y2], b]


# ------------------------------------------------------------ SVG
S = 9.0  # 1 mm = 9 px


def svg_board(mirror, ox, oy):
    o = []
    W = BOARD['w'] * S

    def X(xmm):
        return ox + ((BOARD['w'] - xmm) if mirror else xmm) * S

    def Y(ymm):
        return oy + ymm * S

    def H(x, y):
        a, b = mm(x, y)
        return X(a), Y(b)

    o.append(f'<rect x="{ox}" y="{oy}" width="{W}" height="{BOARD["h"] * S}" rx="10" fill="#e9dcb8" stroke="#b9a678" stroke-width="2"/>')
    for (mx, my) in BOARD['mount']:
        o.append(f'<circle cx="{X(mx)}" cy="{Y(my)}" r="{BOARD["mountD"] / 2 * S}" fill="#fff" stroke="#b9a678" stroke-width="2"/>')
    for x in range(1, BOARD['cols'] + 1):
        for y in range(1, BOARD['rows'] + 1):
            hx, hy = H(x, y)
            on = (x, y) in used
            st = ' stroke="#8a5a1c" stroke-width="1"' if on else ''
            o.append(f'<circle cx="{hx:.1f}" cy="{hy:.1f}" r="{3.3 if on else 2.2}" fill="{"#c58b3a" if on else "#cdbb8e"}"{st}/>')
    # 列と行の番号
    for x in range(1, BOARD['cols'] + 1, 1):
        if x in (1, 5, 10, 15, 20, 25, 30, 34):
            hx, _ = H(x, 1)
            o.append(f'<text x="{hx:.1f}" y="{oy - 8}" font-size="12" text-anchor="middle" fill="#6b7079" font-family="monospace">{x}</text>')
    for y in (1, 5, 10, 15, 20, 25):
        _, hy = H(1, y)
        xx = ox - 10 if not mirror else ox + W + 10
        o.append(f'<text x="{xx}" y="{hy + 4:.1f}" font-size="12" text-anchor="{"end" if not mirror else "start"}" fill="#6b7079" font-family="monospace">{y}</text>')

    # 部品（部品面のみ。はんだ面は薄く）
    alpha = 1 if not mirror else 0.22
    for pt in parts:
        k, ol = pt['kind'], pt['outline']
        if k in ('esp', 'box', 'to92', 'header'):
            x1, y1 = H(ol[0], ol[1]); x2, y2 = H(ol[2], ol[3])
            xa, xb = sorted([x1, x2])
            fill = pt['color'] if k in ('esp', 'box') else '#3a3d44' if k == 'header' else '#2b2f36'
            o.append(f'<rect x="{xa:.1f}" y="{y1:.1f}" width="{xb - xa:.1f}" height="{y2 - y1:.1f}" rx="4" fill="{fill}" fill-opacity="{0.86 * alpha}" stroke="#111" stroke-opacity="{alpha}"/>')
            if k == 'esp':
                # ソケット 2 本
                for row in (3, 13):
                    a1, b1 = H(1, row); a2, _ = H(19, row)
                    xa2, xb2 = sorted([a1, a2])
                    o.append(f'<rect x="{xa2 - 11:.1f}" y="{b1 - 11:.1f}" width="{xb2 - xa2 + 22:.1f}" height="22" rx="3" fill="#111" fill-opacity="{0.9 * alpha}"/>')
                # USB とアンテナ
                ux, uy = H(-0.9, 8)
                o.append(f'<rect x="{ux - 32 if not mirror else ux - 40:.1f}" y="{uy - 34}" width="72" height="68" rx="4" fill="#b8bec7" fill-opacity="{alpha}" stroke="#555" stroke-opacity="{alpha}"/>')
                ax, ay = H(19.6, 3)
                o.append(f'<text x="{ax:.1f}" y="{ay + 56}" font-size="13" text-anchor="middle" fill="#fff" fill-opacity="{alpha}" font-family="sans-serif" transform="rotate(-90 {ax:.1f} {ay + 56})">アンテナ</text>')
                cx, cy = H(10, 8)
                if not mirror:
                    o.append(f'<text x="{cx:.1f}" y="{cy - 4:.1f}" font-size="17" text-anchor="middle" fill="#fff" font-weight="700" font-family="sans-serif">U1 ESP32-DevKitC-32E</text>')
                    o.append(f'<text x="{cx:.1f}" y="{cy + 18:.1f}" font-size="12.5" text-anchor="middle" fill="#cfd5dc" font-family="sans-serif">ピンソケットに挿す ・ USB を左へ</text>')
                    uxx, uyy = H(-0.6, 8)
                    o.append(f'<text x="{uxx:.1f}" y="{uyy + 4:.1f}" font-size="12" text-anchor="middle" fill="#333" font-family="monospace">USB</text>')
            elif k == 'box' and not mirror:
                cx, cy = H((ol[0] + ol[2]) / 2, (ol[1] + ol[3]) / 2 + 1.6)
                o.append(f'<text x="{cx:.1f}" y="{cy:.1f}" font-size="14" text-anchor="middle" fill="#fff" font-weight="700" font-family="sans-serif">{pt["ref"]} GY-521</text>')
                for hx_, hy_ in ((1.9, 22.2), (9.1, 22.2)):
                    a, b = H(hx_, hy_)
                    o.append(f'<circle cx="{a:.1f}" cy="{b:.1f}" r="13" fill="#e9dcb8" stroke="#fff" stroke-width="1.5"/>')
        elif k in ('circle', 'cap'):
            cx, cy = H(ol[0], ol[1])
            r = ol[2] * PITCH * S
            o.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="{"#26292e" if k == "circle" else "#2a4f8a"}" fill-opacity="{0.85 * alpha}" stroke="#111" stroke-opacity="{alpha}"/>')
        elif k in ('res', 'diode'):
            x1, y1 = H(ol[0], ol[1]); x2, y2 = H(ol[2], ol[3])
            xa, xb = sorted([x1, x2])
            o.append(f'<line x1="{xa:.1f}" y1="{y1:.1f}" x2="{xb:.1f}" y2="{y2:.1f}" stroke="#888" stroke-width="2" stroke-opacity="{alpha}"/>')
            col = '#d9b77a' if k == 'res' else '#d2462f'
            o.append(f'<rect x="{xa + 16:.1f}" y="{y1 - 8:.1f}" width="{xb - xa - 32:.1f}" height="16" rx="7" fill="{col}" fill-opacity="{alpha}" stroke="#7a5b2a" stroke-opacity="{alpha}"/>')
            if k == 'diode':
                kx = H(ol[0], ol[1])[0]
                bx = kx + (20 if not mirror else -26)
                o.append(f'<rect x="{bx:.1f}" y="{y1 - 8:.1f}" width="6" height="16" fill="#111" fill-opacity="{alpha}"/>')
    # 配線（はんだ面だけ）。少し弓なりに描いて、穴の列と重ならないようにする
    if mirror:
        for i, w in enumerate(wires):
            (x1, y1), (x2, y2) = H(*w['a']), H(*w['b'])
            dx, dy = x2 - x1, y2 - y1
            L = max(1.0, (dx * dx + dy * dy) ** 0.5)
            bow = min(40.0, 0.12 * L) * (1 if i % 2 == 0 else -1)
            cx, cy = (x1 + x2) / 2 - dy / L * bow, (y1 + y2) / 2 + dx / L * bow
            o.append(f'<path d="M{x1:.1f},{y1:.1f} Q{cx:.1f},{cy:.1f} {x2:.1f},{y2:.1f}" fill="none" stroke="#fff" stroke-width="6.5" stroke-linecap="round" opacity="0.9"/>')
            o.append(f'<path d="M{x1:.1f},{y1:.1f} Q{cx:.1f},{cy:.1f} {x2:.1f},{y2:.1f}" fill="none" stroke="{w["color"]}" stroke-width="3.6" stroke-linecap="round"/>')
            for (px, py) in ((x1, y1), (x2, y2)):
                o.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="4.6" fill="{w["color"]}" stroke="#fff" stroke-width="1.2"/>')
            # 番号（配線表と同じ）
            o.append(f'<text x="{cx * 0.5 + (x1 + x2) / 4:.1f}" y="{cy * 0.5 + (y1 + y2) / 4 + 4:.1f}" font-size="11" text-anchor="middle" font-weight="700" fill="{w["color"]}" font-family="monospace" paint-order="stroke" stroke="#fff" stroke-width="3.5">{i + 1}</text>')
    # 部品の名前
    if not mirror:
        for pt in parts:
            if pt['kind'] in ('esp', 'box'):
                continue
            xs = [p[0] for p in pt['pins'].values()]; ys = [p[1] for p in pt['pins'].values()]
            cx, cy = H(sum(xs) / len(xs), min(ys))
            dy = -16 if pt['kind'] not in ('circle',) else -26
            if pt['ref'] == 'C1':
                cx, cy = H(22.2, 4.5); dy = 5
            o.append(f'<text x="{cx:.1f}" y="{cy + dy:.1f}" font-size="13" text-anchor="middle" fill="#1b1e24" font-weight="700" font-family="sans-serif" paint-order="stroke" stroke="#e9dcb8" stroke-width="4">{pt["ref"]}</text>')
        # ピンの名前（小さく）
        for ref in ('Q1', 'J3', 'J4', 'J5', 'BZ1'):
            pt = next(p for p in parts if p['ref'] == ref)
            for nm, (x, y) in pt['pins'].items():
                hx, hy = H(x, y)
                o.append(f'<text x="{hx:.1f}" y="{hy + 20:.1f}" font-size="10.5" text-anchor="middle" fill="#3d424b" font-family="monospace" paint-order="stroke" stroke="#e9dcb8" stroke-width="3">{nm}</text>')
        for nm, (x, y) in next(p for p in parts if p['ref'] == 'U2')['pins'].items():
            hx, hy = H(x, y)
            o.append(f'<text x="{hx:.1f}" y="{hy + 10:.1f}" font-size="10" text-anchor="start" fill="#fff" font-family="monospace" transform="rotate(90 {hx:.1f} {hy + 10:.1f})" dy="3.5">{nm}</text>')
        # ESP32 の使うピンの名前
        for key in ('J2.19', 'J2.1', 'J2.8', 'J2.9', 'J2.10', 'J2.11', 'J2.12', 'J3.1', 'J3.3', 'J3.6', 'J3.7'):
            x, y = next(p for p in parts if p['ref'] == 'U1')['pins'][key]
            hx, hy = H(x, y)
            ty = hy + 26 if y == 3 else hy - 18
            o.append(f'<text x="{hx:.1f}" y="{ty:.1f}" font-size="10.5" text-anchor="middle" fill="#ffd27a" font-family="monospace">{pinname[key].replace("IO", "")}</text>')
    return o


def build_svg():
    pad = 60
    bw, bh = BOARD['w'] * S, BOARD['h'] * S
    Wd = pad * 2 + bw
    Ht = 90 + bh + 110 + bh + 260
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {Wd:.0f} {Ht:.0f}" width="{Wd:.0f}" height="{Ht:.0f}" font-family="sans-serif">',
         f'<rect width="100%" height="100%" fill="#fff"/>',
         f'<text x="{pad}" y="40" font-size="24" font-weight="700" fill="#1b1e24">QuakeScope 基板の配置と配線</text>',
         f'<text x="{pad}" y="64" font-size="13.5" fill="#6b7079">{BOARD["example"]}。穴の番号は左上が (1, 1)。線の色はネット（回路図と同じ）</text>']
    o.append(f'<text x="{pad}" y="{90 + 6}" font-size="15" font-weight="700" fill="#1b1e24">部品面（上から見た図）</text>')
    o += svg_board(False, pad, 118)
    y2 = 118 + bh + 70
    o.append(f'<text x="{pad}" y="{y2 - 22}" font-size="15" font-weight="700" fill="#1b1e24">はんだ面（裏返した図・左右が逆になる）</text>')
    o += svg_board(True, pad, y2)
    # 凡例
    ly = y2 + bh + 50
    seen = []
    for net, color, _ in NETS:
        if net not in seen:
            seen.append(net)
    for i, net in enumerate(seen):
        color = next(c for n, c, _ in NETS if n == net)
        x = pad + (i % 6) * 140; y = ly + (i // 6) * 26
        o.append(f'<line x1="{x}" y1="{y - 5}" x2="{x + 26}" y2="{y - 5}" stroke="{color}" stroke-width="4" stroke-linecap="round"/>')
        o.append(f'<text x="{x + 34}" y="{y}" font-size="13" fill="#1b1e24" font-family="monospace">{NETNAME.get(net, net)}</text>')
    notes = ['・線は被覆のある線（ジャンパー線・UEW）で。交差してよい。むき出しの線どうしは触れさせない',
             '・U2（加速度センサー）は基板にしっかり固定する。浮いていると、本当の揺れより大きく測ってしまう',
             '・ESP32 のアンテナ（右端）の上下には、金属や太い配線を置かない']
    for i, n in enumerate(notes):
        o.append(f'<text x="{pad}" y="{ly + 74 + i * 22}" font-size="13.5" fill="#3d424b">{n}</text>')
    o.append('</svg>\n')
    return '\n'.join(o)


def wiring_md():
    rows = ['| # | ネット | から | へ | から（穴） | へ（穴） |', '|---|---|---|---|---|---|']
    for i, w in enumerate(wires, 1):
        rows.append(f'| {i} | {NETNAME.get(w["net"], w["net"])} | {label(w["from"])} | {label(w["to"])} | ({w["a"][0]}, {w["a"][1]}) | ({w["b"][0]}, {w["b"][1]}) |')
    place = ['| 部品 | 品名 | 穴（ピン） | メモ |', '|---|---|---|---|']
    for pt in parts:
        if pt['ref'] == 'U1':
            pins = 'ソケット 2 本: (1〜19, 3) と (1〜19, 13)。(19, 3) が 3V3、(1, 3) が 5V'
        else:
            pins = '、'.join(f'{k} ({v[0]}, {v[1]})' for k, v in pt['pins'].items())
        place.append(f'| {pt["ref"]} | {pt["name"]} | {pins} | {pt["note"]} |')
    return ('<!-- tools/hw/layout.py が作ったファイルです。手で直さないでください -->\n'
            '# 基板の配置と配線表\n\n'
            f'基板: {BOARD["example"]}。穴の番号は、部品面を上にして左上の穴が (1, 1)、右へ x、下へ y です。\n\n'
            '![基板の配置と配線](perfboard.svg)\n\n'
            '## 部品の位置\n\n' + '\n'.join(place) + '\n\n'
            '## 配線表\n\n上から順につなぐと、抜けがありません。1 本つないだら、テスターの導通チェックで確かめてください。\n\n' + '\n'.join(rows) + '\n')


(ROOT / 'hardware').mkdir(exist_ok=True)
(ROOT / 'hardware' / 'perfboard.svg').write_text(build_svg(), encoding='utf-8')
(ROOT / 'hardware' / 'wiring.md').write_text(wiring_md(), encoding='utf-8')
(ROOT / 'hardware' / 'layout.json').write_text(json.dumps({'board': BOARD, 'pitch': PITCH, 'parts': parts}, ensure_ascii=False, indent=1), encoding='utf-8')
print('作りました hardware/perfboard.svg・wiring.md・layout.json', len(wires), '本の配線')
