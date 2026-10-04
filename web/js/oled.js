// 本体の小さな画面（SSD1306、128×64）を、本体の display.cpp と同じ配置・同じ文字（5×7 の点）で描く。
import { FONT } from './font5x7.js';

const W = 128, H = 64, S = 4; // 1 点を 4×4 に拡大して描く

export class Oled {
  constructor(canvas) {
    this.c = canvas;
    this.c.width = W * S; this.c.height = H * S;
    this.g = canvas.getContext('2d');
    this.px = new Uint8Array(W * H);
    this.color = 1;
  }
  clear() { this.px.fill(0); }
  set(x, y, v) { if (x >= 0 && x < W && y >= 0 && y < H) this.px[y * W + x] = v; }
  fillRect(x, y, w, h, v = 1) { for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) this.set(x + i, y + j, v); }
  // Adafruit GFX の drawChar と同じ: 5 列 × 8 行、size 倍。背景は塗らない。
  char(ch, x, y, size) {
    let k = ch.charCodeAt(0) - 32;
    if (k < 0 || k > 94) k = '?'.charCodeAt(0) - 32;
    for (let col = 0; col < 5; col++) {
      const bits = parseInt(FONT.substr((k * 5 + col) * 2, 2), 16);
      for (let row = 0; row < 8; row++) if (bits & (1 << row)) this.fillRect(x + col * size, y + row * size, size, size, this.color);
    }
  }
  text(s, x, y, size = 1) { for (const ch of String(s)) { this.char(ch, x, y, size); x += 6 * size; } }
  graph(g, y, h) {
    for (let x = 0; x < 128; x++) {
      const v = Math.floor(((g[x] || 0) * (h - 1) + 127) / 255);
      if (v > 0) this.fillRect(x, y + h - v, 1, v, 1);
    }
  }
  draw(m) {
    this.clear(); this.color = 1;
    const shaking = !!m.shindo;
    this.text(m.clock, 0, 0);
    this.text((m.muted ? 'M ' : '  ') + (m.wifi === false ? '-' : 'W'), 98, 0);
    if (m.alert) this.fillRect(0, 10, 128, 20, 1);
    this.color = m.alert ? 0 : 1;
    this.text(shaking ? 'SHINDO ' + m.shindo : m.status, 2, 13, 2);
    this.color = 1;
    this.text(m.now, 0, 33);
    if (shaking) this.text(m.max, 74, 33);
    this.graph(m.graph, 43, 21);
    this.flush();
  }
  message(lines) {
    this.clear(); this.color = 1;
    lines.forEach((l, i) => this.text(l, 0, 4 + i * 14));
    this.flush();
  }
  flush() {
    const g = this.g, cs = getComputedStyle(document.documentElement);
    const on = cs.getPropertyValue('--oled-px').trim() || '#cfe9ff';
    g.fillStyle = cs.getPropertyValue('--oled-bg').trim() || '#05080b';
    g.fillRect(0, 0, W * S, H * S);
    g.fillStyle = on;
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (this.px[y * W + x]) g.fillRect(x * S, y * S, S - 0.5, S - 0.5);
  }
}
