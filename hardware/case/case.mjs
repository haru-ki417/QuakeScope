// QuakeScope のケース（底と、ふた）を作り、3D プリンター用の STL に書き出す。
// 寸法は hardware/layout.json（基板の配置）と、下の PARAMS から決まる。部品の寸法が手元の物と違うときは PARAMS を直す。
//
// 実行: node hardware/case/case.mjs <manifold.js のパス>
//   manifold-3d（Apache-2.0）の manifold.js と manifold.wasm を使う。npm の manifold-3d@3.2.1 と同じもの。
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const libPath = process.argv[2] || path.join(here, 'node_modules/manifold-3d/manifold.js');
const { default: Module } = await import(pathToFileURL(path.resolve(libPath)).href);
const wasm = await Module();
wasm.setup();
const { Manifold, CrossSection } = wasm;
const layout = JSON.parse(fs.readFileSync(path.join(here, '..', 'layout.json'), 'utf8'));

// ------------------------------------------------------------ 寸法（mm）
export const PARAMS = {
  wall: 2.4,          // 壁の厚さ
  floor: 3.0,         // 底の厚さ
  gap: 1.5,           // 基板と壁のすき間
  innerH: 33,         // 底の内側の高さ（底板の上から、ふたの下まで）。ESP32 をソケットに挿すと上面は約 27 mm
  radius: 5,          // 角の丸み（外側）
  standoff: 6,        // 基板を浮かせる柱の高さ
  standoffD: 7,       // 柱の太さ
  pilotD: 2.5,        // 柱の下穴（M3 のタッピングねじ）
  lid: 2.6,           // ふたの厚さ
  lipH: 3.2,          // ふたの内側の縁の高さ
  lipW: 1.6,          // ふたの内側の縁の厚さ
  fit: 0.3,           // ふたと壁のすき間（きつければ大きく）
  oled: { w: 28.4, h: 28.4, depth: 1.3, winW: 25, winH: 14.5, winDy: 2.0 }, // 0.96″ OLED（製品で寸法が違う）
  oledAt: [75, 50],   // OLED の中心（基板の座標・mm）
  ledAt: [56, 50],    // LED（5 mm）の中心
  ledD: 5.15,
  buttonD: 12.2,      // 押しボタン（前の壁）
  buttonAt: [40, 20], // [基板の x, 底からの高さ]
  earW: 14, earHole: 4.5, // 床やたなにねじ止めする耳
};
const P = PARAMS;
const B = layout.board;

// 外形
const inW = B.w + 2 * P.gap, inD = B.h + 2 * P.gap;
const outW = inW + 2 * P.wall, outD = inD + 2 * P.wall;
const baseH = P.floor + P.innerH;

const rrect = (w, h, r) => CrossSection.square([Math.max(0.01, w - 2 * r), Math.max(0.01, h - 2 * r)]).translate([r, r]).offset(r, 'Round', 2, 48);
// 基板の座標（左上が原点、y が下向き）→ ケースの座標（左下が原点、y が上向き）
const bx = (x) => P.wall + P.gap + x;
const by = (y) => P.wall + P.gap + (B.h - y);
const holeMm = ([cx, cy]) => [B.origin[0] + (cx - 1) * layout.pitch, B.origin[1] + (cy - 1) * layout.pitch];
const cyl = (h, d, seg = 48) => Manifold.cylinder(h, d / 2, d / 2, seg);
const box = (x, y, z, w, d, h) => Manifold.cube([w, d, h]).translate([x, y, z]);

// ------------------------------------------------------------ 底
function base() {
  let m = rrect(outW, outD, P.radius).extrude(baseH);
  m = m.subtract(rrect(inW, inD, Math.max(0.5, P.radius - P.wall)).extrude(baseH).translate([P.wall, P.wall, P.floor]));
  // 耳（左右）: 床・たな・壁にねじ止めして、揺れをそのまま伝える
  for (const side of [-1, 1]) {
    const x0 = side < 0 ? -P.earW : outW;
    let ear = rrect(P.earW + P.radius, 22, 4).extrude(P.floor).translate([side < 0 ? x0 : outW - P.radius, outD / 2 - 11, 0]);
    ear = ear.subtract(cyl(P.floor + 2, P.earHole).translate([side < 0 ? x0 + P.earW / 2 : outW + P.earW / 2, outD / 2, -1]));
    // ねじの頭の座ぐり
    ear = ear.subtract(Manifold.cylinder(1.6, P.earHole / 2, P.earHole / 2 + 1.6, 40).translate([side < 0 ? x0 + P.earW / 2 : outW + P.earW / 2, outD / 2, P.floor - 1.6 + 0.01]));
    m = m.add(ear);
  }
  // 基板の柱
  for (const [mx, my] of B.mount) {
    const c = cyl(P.standoff, P.standoffD).subtract(cyl(P.standoff + 1, P.pilotD, 24).translate([0, 0, 1])).translate([bx(mx), by(my), P.floor]);
    m = m.add(c);
  }
  // USB の口（左の壁）。ESP32 の USB は基板の (x≈-0.9, y=8) の穴の位置、ソケットの上
  const [, usbY] = holeMm([0, 8]);
  const usbZ = P.floor + P.standoff + B.t + 8.5 + 2.5 + 1.6 + 1.5; // ソケット 8.5・ESP32 のピンの台 2.5・基板 1.6・USB の中心 1.5
  m = m.subtract(rrect(13, 8.5, 2).extrude(P.wall + 2).rotate([90, 0, 90]).translate([-1, by(usbY) - 6.5, usbZ - 4.25]));
  // 押しボタン（前の壁）
  m = m.subtract(cyl(P.wall + 2, P.buttonD).rotate([-90, 0, 0]).translate([bx(P.buttonAt[0]), -1, P.buttonAt[1]]));
  // 空気の通り道（後ろの壁）
  for (let i = 0; i < 6; i++) m = m.subtract(rrect(2.2, 12, 1.1).extrude(P.wall + 2).rotate([-90, 0, 0]).translate([bx(14 + i * 6), outD - P.wall - 1, baseH - 18]));
  // ふたを留めるねじ穴（前と後ろの壁、M2.6 のタッピングねじ）
  for (const y of [-1, outD - P.wall - 1]) m = m.subtract(cyl(P.wall + 2, 2.9, 24).rotate([-90, 0, 0]).translate([outW / 2, y, baseH - P.lipH / 2]));
  // 底の裏に部品名と向き（浅く彫る）: 矢印（USB の向き）
  const arrow = CrossSection.ofPolygons([[[0, 0], [10, 6], [4, 6], [4, 14], [-4, 14], [-4, 6], [-10, 6]]]).rotate(90);
  m = m.subtract(arrow.extrude(0.8).translate([outW / 2 - 18, outD / 2, -0.4]));
  return m;
}

// ------------------------------------------------------------ ふた（使うときの向きで作る。印刷は裏返す）
function lid() {
  let m = rrect(outW, outD, P.radius).extrude(P.lid).translate([0, 0, baseH]);
  // 内側の縁（壁の内側にはまる）
  const lw = inW - 2 * P.fit, ld = inD - 2 * P.fit;
  const ring = rrect(lw, ld, Math.max(0.5, P.radius - P.wall - P.fit)).subtract(rrect(lw - 2 * P.lipW, ld - 2 * P.lipW, Math.max(0.5, P.radius - P.wall - P.fit - P.lipW)).translate([P.lipW, P.lipW]));
  m = m.add(ring.extrude(P.lipH).translate([P.wall + P.fit, P.wall + P.fit, baseH - P.lipH]));
  // ねじを受ける厚み（前と後ろ）
  for (const y of [P.wall + P.fit, outD - P.wall - P.fit - 4.5]) {
    let b = box(outW / 2 - 4, y, baseH - P.lipH, 8, 4.5, P.lipH);
    m = m.add(b);
  }
  for (const y of [P.wall - 0.5, outD - P.wall - 5]) m = m.subtract(cyl(5.5, 2.2, 24).rotate([-90, 0, 0]).translate([outW / 2, y, baseH - P.lipH / 2]));
  // OLED: 裏にくぼみ、表に窓
  const [ox, oy] = P.oledAt;
  const o = P.oled;
  m = m.subtract(box(bx(ox) - o.w / 2, by(oy) - o.h / 2, baseH - 0.01, o.w, o.h, o.depth + 0.01));
  m = m.subtract(rrect(o.winW, o.winH, 1.2).extrude(P.lid + 2).translate([bx(ox) - o.winW / 2, by(oy) - o.winH / 2 + o.winDy, baseH - 1]));
  // 窓の縁を斜めに（見やすく）
  const win = rrect(o.winW, o.winH, 1.2).translate([-o.winW / 2, -o.winH / 2]);
  m = m.subtract(win.extrude(1.0, 0, 0, [(o.winW + 2.4) / o.winW, (o.winH + 2.4) / o.winH]).translate([bx(ox), by(oy) + o.winDy, baseH + P.lid - 1.0 + 0.001]));
  // LED
  m = m.subtract(cyl(P.lid + 2, P.ledD, 32).translate([bx(P.ledAt[0]), by(P.ledAt[1]), baseH - 1]));
  m = m.subtract(cyl(1.4, 7.6, 40).translate([bx(P.ledAt[0]), by(P.ledAt[1]), baseH - 0.01])); // LED のつば
  // ブザーの音の穴（BZ1 の真上）
  const bz = layout.parts.find((p) => p.ref === 'BZ1');
  const [zx, zy] = holeMm([bz.outline[0], bz.outline[1]]);
  const holes = [[0, 0]];
  for (let k = 0; k < 6; k++) holes.push([3.6 * Math.cos(k * Math.PI / 3), 3.6 * Math.sin(k * Math.PI / 3)]);
  for (let k = 0; k < 12; k++) holes.push([7.0 * Math.cos(k * Math.PI / 6 + Math.PI / 12), 7.0 * Math.sin(k * Math.PI / 6 + Math.PI / 12)]);
  for (const [dx, dy] of holes) m = m.subtract(cyl(P.lid + 2, 2.0, 20).translate([bx(zx) + dx, by(zy) + dy, baseH - 1]));
  // 地震計の波形の模様（浅く彫る）
  const pts = [[0, 0], [8, 0], [11, 6], [15, -9], [19, 11], [23, -6], [26, 2], [30, 0], [44, 0]];
  let wave = null;
  for (let i = 0; i < pts.length - 1; i++) {
    const [x1, y1] = pts[i], [x2, y2] = pts[i + 1];
    const seg = CrossSection.hull([CrossSection.circle(0.9, 16).translate([x1, y1]), CrossSection.circle(0.9, 16).translate([x2, y2])]);
    wave = wave ? wave.add(seg) : seg;
  }
  m = m.subtract(wave.extrude(0.7).translate([bx(8), by(50), baseH + P.lid - 0.7 + 0.001]));
  return m;
}

// ------------------------------------------------------------ STL
function toStl(man, name) {
  const mesh = man.getMesh();
  const v = mesh.vertProperties, t = mesh.triVerts, np = mesh.numProp;
  const n = t.length / 3;
  const buf = Buffer.alloc(84 + n * 50);
  buf.write(`QuakeScope ${name}`.padEnd(80, ' '), 0, 'ascii');
  buf.writeUInt32LE(n, 80);
  let o = 84;
  for (let i = 0; i < n; i++) {
    const a = t[i * 3] * np, b = t[i * 3 + 1] * np, c = t[i * 3 + 2] * np;
    const ux = v[b] - v[a], uy = v[b + 1] - v[a + 1], uz = v[b + 2] - v[a + 2];
    const wx = v[c] - v[a], wy = v[c + 1] - v[a + 1], wz = v[c + 2] - v[a + 2];
    let nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
    const l = Math.hypot(nx, ny, nz) || 1;
    for (const f of [nx / l, ny / l, nz / l]) { buf.writeFloatLE(f, o); o += 4; }
    for (const k of [a, b, c]) for (let j = 0; j < 3; j++) { buf.writeFloatLE(v[k + j], o); o += 4; }
    buf.writeUInt16LE(0, o); o += 2;
  }
  return buf;
}

const b = base(), l = lid();
// ふたは裏返して、平らな面を下に（印刷の向き）
const lidPrint = l.rotate([180, 0, 0]).translate([0, outD, baseH + P.lid]);
for (const [name, m] of [['case_base', b], ['case_lid', lidPrint]]) {
  if (m.status && m.status() !== 'NoError') throw new Error(`${name}: ${m.status()}`);
  fs.writeFileSync(path.join(here, `${name}.stl`), toStl(m, name));
  const bb = m.boundingBox();
  console.log(`${name}.stl  ${(bb.max[0] - bb.min[0]).toFixed(1)} × ${(bb.max[1] - bb.min[1]).toFixed(1)} × ${(bb.max[2] - bb.min[2]).toFixed(1)} mm  体積 ${(m.volume() / 1000).toFixed(1)} cm³  genus ${m.genus()}`);
}
console.log(`外形 ${outW.toFixed(1)} × ${outD.toFixed(1)} × ${(baseH + P.lid).toFixed(1)} mm（耳を除く）`);
