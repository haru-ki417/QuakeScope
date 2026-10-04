// QuakeScope ブラウザー版。判定は本体と同じ C++ のエンジン（WebAssembly）で行う。
import { Buzzer } from './audio.js';
import { loadEngine, LEVELS } from './engine.js';
import { Oled } from './oled.js';

const $ = (id) => document.getElementById(id);
const FS = 100;                       // 本体と同じ 100 Hz
const SHINDO = ['0', '1', '2', '3', '4', '5弱', '5強', '6弱', '6強', '7'];
const BOUNDS = [0.5, 1.5, 2.5, 3.5, 4.5, 5.0, 5.5, 6.0, 6.5];
// 気象庁と同じ丸め方（小数第 3 位を四捨五入し、小数第 2 位を切り捨て）。本体の画面と同じ値になる
const jr = (i) => Math.floor(Math.round(i * 100) / 10 + 1e-9) / 10;
const shindoOf = (i) => { let k = 0; while (k < BOUNDS.length && i >= BOUNDS[k]) k++; return SHINDO[k]; };
const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();

const SCENES = [
  { id: 'near4', kind: 'near', target: 4.0, seed: 2, sec: 60, title: '近くの地震', sub: '震度 4 相当。P 波のすぐあとに強い揺れ', tag: '地震' },
  { id: 'near5', kind: 'near', target: 5.3, seed: 4, sec: 70, title: '強い地震', sub: '震度 5 強相当。警報まで上がる', tag: '地震' },
  { id: 'far3', kind: 'far', target: 3.0, seed: 1, sec: 80, title: '遠くの地震', sub: '震度 3 相当。P 波が長く、ゆっくり大きく', tag: '地震' },
  { id: 'small', kind: 'near', target: 1.8, seed: 5, sec: 45, title: '小さな地震', sub: '震度 2 相当。検知するが通知しない', tag: '地震' },
  { id: 'door', kind: 'door', target: 0, seed: 3, sec: 25, title: 'ドアを閉めた衝撃', sub: '一瞬だけ大きい。揺れとみなさない', tag: '振動' },
  { id: 'steps', kind: 'steps', target: 0, seed: 2, sec: 25, title: '足音', sub: '小さな衝撃のくり返し', tag: '振動' },
  { id: 'truck', kind: 'truck', target: 0, seed: 1, sec: 35, title: 'トラックの振動', sub: '十数秒の細かい揺れ。通知はしない', tag: '振動' },
];

let E;                    // エンジン
let oled, buzzer;
let src = null;           // いまの入力（見本の揺れ・スマホ）
let shownShindo = null;
let wave = { x: new Float32Array(3000), y: new Float32Array(3000), z: new Float32Array(3000), head: 0, marks: [] };
let simClock = 0;         // 入力の時刻（秒）
let lastUi = 0;
let ledColor = '';

// ------------------------------------------------------------ 入力

function resetEngine() {
  E.init(+$('ic').value, +$('iw').value, 3);
  wave = { x: new Float32Array(3000), y: new Float32Array(3000), z: new Float32Array(3000), head: 0, marks: [] };
  simClock = 0;
  buzzer.stop();
  $('result').hidden = true;
}

function pushSample(x, y, z) {
  E.push(x, y, z);
  const d = E.dyn();
  const i = wave.head % 3000;
  wave.x[i] = d[0]; wave.y[i] = d[1]; wave.z[i] = d[2];
  wave.head++;
  simClock += 1 / FS;
  let ev;
  while ((ev = E.poll())) onEvent(ev);
}

class SimSource {
  constructor(scene) {
    this.scene = scene;
    this.onset = 15;
    const s = E.synth(scene.kind, scene.target, scene.seed, scene.sec + this.onset, this.onset);
    this.s = s;
    this.i = 0;
    this.result = { scene, p: s.p, s: s.s, events: [], exact: E.exact(s.x, s.y, s.z) };
    // 揺れの 2 秒前までは、すぐに流す（向きの測定と、平常時の揺れの学習）
    const pre = (this.onset - 2) * FS;
    for (; this.i < pre; this.i++) pushSample(s.x[this.i], s.y[this.i], s.z[this.i]);
    this.t0 = performance.now();
    this.done = false;
  }
  step(now) {
    const speed = $('fast').checked ? 3 : 1;
    const due = Math.min(this.s.x.length, (this.onset - 2) * FS + Math.floor((now - this.t0) / 1000 * FS * speed));
    for (; this.i < due; this.i++) pushSample(this.s.x[this.i], this.s.y[this.i], this.s.z[this.i]);
    if (this.i >= this.s.x.length && !this.done) { this.done = true; finishSim(this); }
  }
  stop() { this.done = true; }
}

class PhoneSource {
  constructor() { this.last = null; this.n = 0; this.t0 = performance.now(); this.next = null; this.handler = (e) => this.onMotion(e); }
  async start() {
    if (typeof DeviceMotionEvent === 'undefined') throw new Error('この端末には加速度センサーがありません（スマホで開いてください）');
    if (typeof DeviceMotionEvent.requestPermission === 'function') {
      const r = await DeviceMotionEvent.requestPermission();
      if (r !== 'granted') throw new Error('センサーの使用が許可されませんでした');
    }
    window.addEventListener('devicemotion', this.handler);
    setTimeout(() => { if (this.n === 0) $('phonehint').textContent = 'センサーの値が届きません。スマホで開いているか確かめてください。'; }, 2500);
  }
  onMotion(e) {
    const a = e.accelerationIncludingGravity;
    if (!a || a.x === null) return;
    const t = e.timeStamp / 1000;
    const v = [a.x * 100, a.y * 100, a.z * 100];   // m/s² → gal
    this.n++;
    // 100 Hz の等間隔に直す（線形補間）
    if (this.last && t - this.last.t > 0.5) this.next = null; // 画面を閉じていたなどで間が空いたら、つなぎ直す
    if (this.last) {
      if (this.next === null) this.next = this.last.t;
      while (this.next <= t) {
        const k = (this.next - this.last.t) / Math.max(1e-6, t - this.last.t);
        pushSample(...[0, 1, 2].map((j) => this.last.v[j] + (v[j] - this.last.v[j]) * k));
        this.next += 1 / FS;
      }
    }
    this.last = { t, v };
  }
  step() {
    const secs = (performance.now() - this.t0) / 1000;
    if (secs > 1) {
      const st = E.status();
      const floor = st.noiseI > -5 ? `・雑音は震度 ${st.noiseI.toFixed(1)} 相当（これより 0.6 以上大きい揺れを検知）` : '';
      $('phonehint').textContent = `毎秒 ${Math.round(this.n / secs)} 回（100 Hz に直して判定）${floor}`;
    }
  }
  stop() { window.removeEventListener('devicemotion', this.handler); }
}

// ------------------------------------------------------------ 出来事

const SAY = {
  Calibrated: (e) => `向きを測りました（重力 ${e.value.toFixed(0)} gal・傾き ${e.tilt.toFixed(0)}°）。揺れを見張っています。`,
  ShakeStart: (e) => `揺れ始めをとらえました（${e.time.toFixed(1)} 秒）。短い音で知らせます。`,
  LevelUp: (e) => e.level === 3 ? `強い揺れ（震度 ${e.shindoLabel} 相当）。強い警報音と通知。` : e.level === 2 ? `震度 ${e.shindoLabel} 相当の揺れ。警報音と通知。` : '',
  ShakeEnd: (e) => `揺れが収まりました。最大 震度 ${e.shindoLabel} 相当・${Math.round(e.duration)} 秒。`,
  SensorFault: () => 'センサーの値がおかしいようです。',
  SensorOk: () => 'センサーが戻りました。',
};

function onEvent(ev) {
  if (src && src.result) src.result.events.push(ev);
  const said = SAY[ev.type] && SAY[ev.type](ev);
  if (said) $('said').textContent = said;
  if (ev.type === 'ShakeStart') { wave.marks.push({ at: wave.head, label: '検知', c: css('--l1') }); buzzer.play('detect'); }
  if (ev.type === 'LevelUp') {
    wave.marks.push({ at: wave.head, label: LEVELS[ev.level], c: ev.level === 3 ? css('--l3') : css('--l2') });
    buzzer.play(ev.level === 3 ? 'warning' : 'caution');
  }
  if (ev.type === 'ShakeEnd') addLog(ev);
  // 通知（本体と同じ判断・同じ文面）
  if (ev.type === 'LevelUp' || ev.type === 'ShakeEnd') {
    const notified = ev.type === 'LevelUp' ? ev.level >= 2 : ev.level >= 2;
    if (!notified) return;
    const m = E.message($('place').value || '自宅', new Date(), ev.time - ev.startTime);
    if (m) addMessage(m, ev);
  }
}

function addMessage(m, ev) {
  const box = $('inbox');
  box.querySelector('.empty')?.remove();
  const d = document.createElement('div');
  d.className = 'msg' + (ev.type === 'ShakeEnd' ? ' e' : ev.level === 3 ? ' w' : '');
  const lines = m.body.split('\n').slice(1).join('\n');
  d.innerHTML = '<time></time><b></b><p></p>';
  d.querySelector('time').textContent = new Date().toLocaleTimeString('ja-JP');
  d.querySelector('b').textContent = m.title;
  d.querySelector('p').textContent = lines;
  box.prepend(d);
  $('json').textContent = JSON.stringify(JSON.parse(E.lineJson('U（送り先のユーザー ID）', m.title, m.body)), null, 2);
}

function addLog(ev) {
  const ol = $('log');
  ol.querySelector('.empty')?.remove();
  const li = document.createElement('li');
  li.innerHTML = `<b>震度${ev.shindoLabel}</b><span>計測震度 ${jr(ev.intensity).toFixed(1)}・${Math.round(ev.duration)} 秒・${ev.pga.toFixed(1)} gal</span><span class="t">${new Date().toLocaleTimeString('ja-JP', { hour: '2-digit', minute: '2-digit' })}</span>`;
  ol.prepend(li);
}

function finishSim(s) {
  $('stop').disabled = true;
  document.querySelectorAll('.scene').forEach((b) => b.setAttribute('aria-pressed', 'false'));
  const r = s.result, evs = r.events;
  const start = evs.find((e) => e.type === 'ShakeStart');
  const end = evs.find((e) => e.type === 'ShakeEnd');
  const ups = evs.filter((e) => e.type === 'LevelUp' && e.level >= 2);
  const li = [];
  const quake = r.scene.kind === 'near' || r.scene.kind === 'far';
  if (!start) li.push('揺れとはみなしませんでした（音も通知もなし）。');
  if (start && quake) {
    const lead = r.s - start.time;
    li.push(`P 波から ${(start.time - r.p).toFixed(1)} 秒で揺れ始めをとらえました${lead > 0 ? `（強い揺れの S 波の ${lead.toFixed(1)} 秒前）` : ''}。`);
  } else if (start) li.push(`震度 1 相当の細かい揺れとして検知しました（${start.time.toFixed(1)} 秒）。`);
  li.push(ups.length ? `通知: ${ups.map((u) => LEVELS[u.level] + '（震度' + u.shindoLabel + '相当）').join(' → ')}${end ? ' と、収まったときのまとめ' : ''}` : '通知: なし（震度 3 相当に届かない）');
  if (end) li.push(`本体の推定 ${jr(end.intensity).toFixed(1)}（震度${end.shindoLabel}相当）／ 計測震度 ${jr(r.exact.intensity).toFixed(1)}（震度${r.exact.shindoLabel}）`);
  else li.push(`この揺れの計測震度: ${jr(r.exact.intensity).toFixed(1)}（震度${r.exact.shindoLabel}）`);
  $('result').innerHTML = `<h4>${r.scene.title}の結果</h4><ul>${li.map((x) => `<li>${x}</li>`).join('')}</ul>`;
  $('result').hidden = false;
}

// ------------------------------------------------------------ 描画

function drawWave() {
  const c = $('wave'), dpr = Math.min(2, devicePixelRatio || 1);
  const W = c.clientWidth, H = c.clientHeight || 220;
  if (c.width !== Math.round(W * dpr) || c.height !== Math.round(H * dpr)) { c.width = Math.round(W * dpr); c.height = Math.round(H * dpr); }
  const g = c.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, W, H);
  // 方眼（地震計の記録紙）
  g.strokeStyle = css('--grid'); g.lineWidth = 1;
  for (let x = 0; x <= W; x += W / 30) { g.beginPath(); g.moveTo(x, 0); g.lineTo(x, H); g.stroke(); }
  g.strokeStyle = css('--grid2');
  for (let x = 0; x <= W; x += W / 6) { g.beginPath(); g.moveTo(x, 0); g.lineTo(x, H); g.stroke(); }
  const n = Math.min(wave.head, 3000);
  // 縦軸: 揺れの大きさに合わせて自動で（最低 ±2 gal）
  let mx = 2;
  for (let i = 0; i < n; i++) { const k = (wave.head - n + i) % 3000; mx = Math.max(mx, Math.abs(wave.x[k]), Math.abs(wave.y[k]), Math.abs(wave.z[k])); }
  const rows = [['x', '--x'], ['y', '--y'], ['z', '--z']];
  rows.forEach(([ax, col], r) => {
    const y0 = H * (r + 0.5) / 3, amp = H / 6.4 / mx;
    g.strokeStyle = css('--line'); g.beginPath(); g.moveTo(0, y0); g.lineTo(W, y0); g.stroke();
    g.strokeStyle = css(col); g.lineWidth = 1.3; g.beginPath();
    const step = Math.max(1, Math.floor(3000 / W / 2));
    for (let i = 0; i < 3000; i += step) {
      const idx = wave.head - 3000 + i;
      if (idx < 0) continue;
      const v = wave[ax][idx % 3000];
      const x = i / 3000 * W, y = y0 - v * amp;
      i === 0 || idx === 0 ? g.moveTo(x, y) : g.lineTo(x, y);
    }
    g.stroke();
  });
  // 目盛りと、出来事の印
  g.fillStyle = css('--muted'); g.font = '11px system-ui';
  g.fillText(`±${mx < 10 ? mx.toFixed(1) : Math.round(mx)} gal`, 6, 13);
  let lastX = -1e9, lift = 0;
  for (const m of wave.marks) {
    const age = wave.head - m.at;
    if (age > 3000) continue;
    const x = (1 - age / 3000) * W;
    lift = x - lastX < 44 ? lift + 15 : 0; lastX = x; // 近い印は、文字を少し上にずらして重ねない
    g.strokeStyle = m.c; g.lineWidth = 2; g.setLineDash([4, 3]);
    g.beginPath(); g.moveTo(x, 0); g.lineTo(x, H); g.stroke(); g.setLineDash([]);
    g.fillStyle = m.c; g.font = 'bold 12px system-ui'; g.fillText(m.label, Math.min(x + 4, W - 40), H - 6 - lift);
  }
}

function buildScale() {
  const cols = ['--muted', '--muted', '--z', '--y', '--l1', '--l2', '--l2', '--l3', '--l3', '--l3'];
  $('scale').innerHTML = SHINDO.map((s, i) => `<span style="--c:var(${cols[i]})" data-i="${i}">${s}</span>`).join('');
}

function updateUi(now) {
  const st = E.status();
  const lvl = st.level;
  $('state').dataset.level = lvl;
  $('levelchip').textContent = !src && !wave.head ? '待機中' : !st.calibrated ? '向きを測定中' : LEVELS[lvl];
  $('clock').textContent = simClock.toFixed(1) + ' 秒';
  const iNow = Math.max(0, st.iNow), iEv = Math.max(0, st.iEvent);
  const sh = st.inEvent ? shindoOf(jr(iEv)) : iNow >= 0.5 ? shindoOf(jr(iNow)) : '';
  if (sh !== shownShindo) {
    shownShindo = sh;
    $('shindo').classList.toggle('idle', !sh);
    $('shindo').innerHTML = sh ? sh.replace(/(弱|強)/, '<span>$1</span>') : '—';
  }
  $('inow').textContent = st.calibrated ? jr(iNow).toFixed(1) : '—';
  $('imax').textContent = st.inEvent ? jr(iEv).toFixed(1) : '—';
  $('pga').innerHTML = st.inEvent ? st.pga.toFixed(1) + '<small> gal</small>' : '—';
  // 震度の目盛り
  const shown = st.inEvent ? iEv : iNow;
  const k = SHINDO.indexOf(shindoOf(jr(shown)));
  document.querySelectorAll('#scale span').forEach((s, i) => s.classList.toggle('on', st.calibrated && i === k && (st.inEvent || shown >= 0.5)));
  const pos = Math.max(0, Math.min(1, shown / 7));
  $('needle').style.left = `calc(${(pos * 100).toFixed(2)}% - 1px)`;
  // 本体の画面と LED
  const d = new Date();
  const m = E.screen(d.getHours(), d.getMinutes(), d.getSeconds(), true);
  m.muted = !$('sound').checked;
  if (src || wave.head) oled.draw(m); // 何も流していないあいだは、案内の画面のまま
  const color = !src && !wave.head ? '#1f7a48' : !st.calibrated ? '#3aa0ff' : lvl === 3 ? (Math.floor(now / 150) % 2 ? '#ff2a2a' : '#300') : lvl === 2 ? (Math.floor(now / 500) % 2 ? '#ff8a1e' : '#4a2000') : lvl === 1 ? '#f2c200' : '#2bd46e';
  if (color !== ledColor) { ledColor = color; $('led').style.background = color; $('led').style.boxShadow = `0 0 10px ${color}`; }
  drawWave();
}

function loop(now) {
  if (src) src.step(now);
  if (now - lastUi > 50) { lastUi = now; updateUi(now); }
  requestAnimationFrame(loop);
}

// ------------------------------------------------------------ しくみ

function drawGain() {
  const c = $('gain'), dpr = Math.min(2, devicePixelRatio || 1), W = c.clientWidth, H = c.clientHeight || 200;
  if (!W) return;
  c.width = W * dpr; c.height = H * dpr;
  const g = c.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0);
  const fx = (f) => 36 + (Math.log10(f) + 1.3) / (Math.log10(30) + 1.3) * (W - 50);
  const fy = (v) => 10 + (1 - (20 * Math.log10(Math.max(v, 1e-3)) + 40) / 50) * (H - 34);
  g.strokeStyle = css('--line'); g.fillStyle = css('--muted'); g.font = '11px system-ui';
  for (const f of [0.1, 0.3, 1, 3, 10, 30]) { g.beginPath(); g.moveTo(fx(f), 10); g.lineTo(fx(f), H - 24); g.stroke(); g.textAlign = f === 30 ? 'right' : 'center'; g.fillText(f + ' Hz', f === 30 ? fx(f) : fx(f), H - 8); g.textAlign = 'left'; }
  for (const db of [-40, -20, 0, 10]) { const y = fy(10 ** (db / 20)); g.beginPath(); g.moveTo(36, y); g.lineTo(W - 10, y); g.stroke(); g.fillText(db + ' dB', 0, y + 4); }
  const line = (fn, col, dash) => {
    g.strokeStyle = col; g.lineWidth = 2; g.setLineDash(dash); g.beginPath();
    for (let i = 0; i <= 300; i++) { const f = 10 ** (-1.3 + i / 300 * (Math.log10(30) + 1.3)); const x = fx(f), y = fy(fn(f)); i ? g.lineTo(x, y) : g.moveTo(x, y); }
    g.stroke(); g.setLineDash([]);
  };
  line(E.gainJma, css('--ink'), []);
  line(E.gainIir, css('--accent'), [5, 4]);
}

async function verify() {
  const out = $('verifyout');
  out.innerHTML = '<p class="note">計算しています…</p>';
  await new Promise((r) => setTimeout(r, 30));
  const rows = [];
  let maxErr = 0, sum = 0, n = 0;
  for (const kind of ['near', 'far']) for (const t of [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]) for (const seed of [1, 2, 3, 4]) {
    const s = E.synth(kind, t, seed, 100, 15);
    const ex = E.exact(s.x, s.y, s.z).raw, rt = E.realtimeOf(s.x, s.y, s.z);
    const err = Math.abs(rt - ex);
    maxErr = Math.max(maxErr, err); sum += err; n++;
    if (seed === 1) rows.push([kind === 'near' ? '近い' : '遠い', t, ex, rt, err]);
  }
  // 誤報の確かめ
  const nuis = [];
  for (const kind of ['door', 'steps', 'truck']) {
    let starts = 0, alarms = 0;
    for (const seed of [1, 2, 3, 4, 5, 6]) {
      const s = E.synth(kind, 0, seed, 40, 15);
      E.init(2.5, 4.5, 3);
      for (let i = 0; i < s.x.length; i++) { E.push(s.x[i], s.y[i], s.z[i]); let ev; while ((ev = E.poll())) { if (ev.type === 'ShakeStart') starts++; if (ev.type === 'LevelUp' && ev.level >= 2) alarms++; } }
    }
    nuis.push([{ door: 'ドアを閉めた衝撃', steps: '足音', truck: 'トラックの振動' }[kind], starts, alarms]);
  }
  resetEngine();
  out.innerHTML = `<p><b>48 本の平均の差 ${(sum / n).toFixed(3)}、最大 ${maxErr.toFixed(3)}</b>（震度の階級は 1.0 刻みなので、0.1 未満の差は表示に影響しにくい）</p>
  <table class="v"><thead><tr><th>地震</th><th>目標</th><th>計測震度（周波数領域）</th><th>本体の推定（IIR）</th><th>差</th></tr></thead><tbody>
  ${rows.map((r) => `<tr><td>${r[0]}</td><td>${r[1].toFixed(1)}</td><td>${r[2].toFixed(2)}</td><td>${r[3].toFixed(2)}</td><td class="${r[4] < 0.1 ? 'ok' : 'ng'}">${r[4].toFixed(3)}</td></tr>`).join('')}</tbody></table>
  <table class="v"><thead><tr><th>地震ではない振動（6 回ずつ）</th><th>揺れとみなした回数</th><th>警報・通知</th></tr></thead><tbody>
  ${nuis.map((r) => `<tr><td>${r[0]}</td><td>${r[1]}</td><td class="${r[2] === 0 ? 'ok' : 'ng'}">${r[2]}</td></tr>`).join('')}</tbody></table>`;
}

// ------------------------------------------------------------ 画面の操作

function setTab(t) {
  document.querySelectorAll('.tabs button').forEach((b) => b.setAttribute('aria-selected', b.dataset.tab === t ? 'true' : 'false'));
  for (const k of ['sim', 'phone', 'how']) $('tab-' + k).hidden = k !== t;
  if (t === 'how') requestAnimationFrame(drawGain);
}

function stopSource() {
  if (src) src.stop();
  src = null;
  $('stop').disabled = true;
  $('phone').textContent = '測り始める';
  document.querySelectorAll('.scene').forEach((b) => b.setAttribute('aria-pressed', 'false'));
}

async function main() {
  E = await loadEngine();
  oled = new Oled($('oled'));
  buzzer = new Buzzer((on) => $('spk').classList.toggle('on', on));
  buildScale();
  E.init(2.5, 4.5, 3);
  oled.message(['     QuakeScope', '', ' choose a scene', ' or use the phone']);

  $('scenes').innerHTML = SCENES.map((s) => `<button class="scene" type="button" data-id="${s.id}" aria-pressed="false"><em class="${s.tag === '地震' ? 'q' : ''}">${s.tag}</em><b>${s.title}</b><small>${s.sub}</small></button>`).join('');
  document.querySelectorAll('.scene').forEach((b) => b.onclick = () => {
    buzzer.unlock();
    stopSource();
    resetEngine();
    b.setAttribute('aria-pressed', 'true');
    $('said').textContent = '流しています…';
    src = new SimSource(SCENES.find((s) => s.id === b.dataset.id));
    $('stop').disabled = false;
  });
  $('stop').onclick = () => { stopSource(); buzzer.stop(); };
  $('sound').onchange = () => { buzzer.enabled = $('sound').checked; if (!buzzer.enabled) buzzer.stop(); };
  $('phone').onclick = async () => {
    buzzer.unlock();
    if (src instanceof PhoneSource) { stopSource(); return; }
    stopSource(); resetEngine();
    const p = new PhoneSource();
    try { await p.start(); src = p; $('phone').textContent = '止める'; $('said').textContent = 'スマホを動かさずに置いてください。向きを測っています…'; }
    catch (e) { $('phonehint').textContent = e.message; }
  };
  document.querySelectorAll('.tabs button').forEach((b) => b.onclick = () => setTab(b.dataset.tab));
  $('verify').onclick = verify;
  $('ic').onchange = $('iw').onchange = () => E.setLevels(+$('ic').value, +$('iw').value);
  $('theme').onclick = () => {
    const cur = document.documentElement.getAttribute('data-theme') || (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
    const next = cur === 'dark' ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', next);
    try { localStorage.setItem('qs-theme', next); } catch (e) { /* 保存できなくても動く */ }
    drawGain();
  };
  addEventListener('resize', () => { if (!$('tab-how').hidden) drawGain(); });
  // URL の #near4 などで、見本をすぐ流す（紹介用）
  const h = location.hash.slice(1);
  if (SCENES.some((s) => s.id === h)) document.querySelector(`.scene[data-id="${h}"]`).click();
  requestAnimationFrame(loop);
}

main().catch((e) => { $('said').textContent = '読み込めませんでした: ' + e.message; });
