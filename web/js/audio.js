// 本体のブザーと同じ音のパターン（alarm.cpp と同じ周波数・長さ）を、Web Audio で鳴らす。
const PATTERNS = {
  detect: { steps: [[1200, 80], [0, 80], [1200, 80]], total: 240, rank: 2 },
  caution: { steps: [[2000, 200], [0, 300]], total: 8000, rank: 3 },     // ブラウザー版は短め（本体は 30 秒）
  warning: { steps: [[2400, 120], [1800, 120]], total: 10000, rank: 4 }, // 本体は 60 秒
};

export class Buzzer {
  constructor(onChange) { this.ctx = null; this.osc = null; this.gain = null; this.cur = null; this.timer = 0; this.enabled = true; this.onChange = onChange || (() => {}); }
  unlock() {
    if (this.ctx) { if (this.ctx.state === 'suspended') this.ctx.resume(); return; }
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    this.ctx = new AC();
    this.gain = this.ctx.createGain();
    this.gain.gain.value = 0;
    this.gain.connect(this.ctx.destination);
    this.osc = this.ctx.createOscillator();
    this.osc.type = 'square';
    this.osc.connect(this.gain);
    this.osc.start();
  }
  play(name) {
    const p = PATTERNS[name];
    if (!p || !this.enabled) return;
    if (this.cur && PATTERNS[this.cur].rank > p.rank) return;
    this.stop();
    this.cur = name;
    this.onChange(true);
    const start = performance.now();
    let i = 0, at = start;
    const tick = () => {
      const now = performance.now();
      if (now - start >= p.total) return this.stop();
      if (now >= at) {
        const [hz, ms] = p.steps[i % p.steps.length];
        this.tone(hz);
        at = now + ms; i++;
      }
      this.timer = requestAnimationFrame(tick);
    };
    tick();
  }
  tone(hz) {
    if (!this.ctx) return;
    const t = this.ctx.currentTime;
    if (hz) { this.osc.frequency.setValueAtTime(hz, t); this.gain.gain.setTargetAtTime(0.05, t, 0.005); }
    else this.gain.gain.setTargetAtTime(0, t, 0.005);
  }
  stop() {
    cancelAnimationFrame(this.timer);
    this.tone(0);
    if (this.cur) { this.cur = null; this.onChange(false); }
  }
}
