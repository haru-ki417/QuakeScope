// 本体と同じ判定エンジン（C++）を WebAssembly で動かすための薄い包み。
const EV = ['Calibrated', 'ShakeStart', 'LevelUp', 'ShakeEnd', 'SensorFault', 'SensorOk'];
export const LEVELS = ['平常', '揺れ検知', '注意', '警報'];
export const SYNTH = { quiet: 0, near: 1, far: 2, door: 3, steps: 4, truck: 5 };

export async function loadEngine(url = new URL('../qs.wasm', import.meta.url)) {
  let mem;
  // ファイルを使わないので、WASI の呼び出しは何もしない
  const wasi = {
    fd_write: (fd, iov, n, out) => { const v = new DataView(mem.buffer); let t = 0; for (let i = 0; i < n; i++) t += v.getUint32(iov + 8 * i + 4, true); v.setUint32(out, t, true); return 0; },
    fd_close: () => 0, fd_seek: () => 70, proc_exit: () => { throw new Error('exit'); },
  };
  const bytes = typeof fetch === 'function' && !(url instanceof URL && url.protocol === 'file:')
    ? await (await fetch(url)).arrayBuffer()
    : (await import('node:fs')).readFileSync(url);
  const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: wasi });
  const x = instance.exports;
  mem = x.memory;
  if (x._initialize) x._initialize();
  const dec = new TextDecoder();
  const enc = new TextEncoder();
  const str = (p) => { const u = new Uint8Array(mem.buffer, p); let n = 0; while (u[n]) n++; return dec.decode(u.subarray(0, n)); };
  const withStrings = (strs, fn) => {
    const ptrs = strs.map((s) => { const b = enc.encode(s + '\0'); const p = x.qs_alloc(b.length); new Uint8Array(mem.buffer, p, b.length).set(b); return p; });
    try { return fn(...ptrs); } finally { ptrs.forEach((p) => x.qs_free(p)); }
  };
  const copyIn = (arr) => { const p = x.qs_alloc(arr.length * 4); new Float32Array(mem.buffer, p, arr.length).set(arr); return p; };

  return {
    init(caution = 2.5, warning = 4.5, calibrateSec = 3) { x.qs_init(caution, warning, calibrateSec); },
    setLevels(c, w) { x.qs_set_levels(c, w); },
    push(ax, ay, az) { x.qs_push(ax, ay, az); },
    dyn() { return [x.qs_dyn(0), x.qs_dyn(1), x.qs_dyn(2)]; },
    poll() {
      if (!x.qs_poll()) return null;
      const f = (i) => x.qs_ev(i);
      return { type: EV[f(0)], level: f(1), time: f(2), intensity: f(3), shindo: f(4), shindoLabel: str(x.qs_shindo_label(f(4))), pga: f(5), duration: f(6), startTime: f(7), value: f(8), tilt: f(9) };
    },
    status() {
      const f = (i) => x.qs_st(i);
      return { calibrated: !!f(0), sensorOk: !!f(1), inEvent: !!f(2), level: f(3), time: f(4), iNow: f(5), iEvent: f(6), pga: f(7), staLta: f(8), noise: f(9), eventStart: f(10), noiseI: f(11) };
    },
    history() { return Array.from(new Float32Array(mem.buffer, x.qs_history(), 128)); },
    screen(h = -1, m = -1, s = -1, wifi = true) {
      const [clock, status, shindo, now, max, alert, g] = str(x.qs_screen(h, m, s, wifi ? 1 : 0)).split('\t');
      const graph = []; for (let i = 0; i < g.length; i += 2) graph.push(parseInt(g.substr(i, 2), 16));
      return { clock, status, shindo, now, max, alert: alert === '1', graph };
    },
    // 直前に poll した出来事の通知の文面（通知しない出来事なら null）
    message(place, date = new Date(), startAgo = 0) {
      const t = withStrings([place], (p) => str(x.qs_message(p, date.getFullYear(), date.getMonth() + 1, date.getDate(), date.getHours(), date.getMinutes(), date.getSeconds(), startAgo)));
      if (!t) return null;
      const [title, body] = t.split('\x1f');
      return { title, body };
    },
    lineJson(user, title, body) { return withStrings([user, title, body], (a, b, c) => str(x.qs_line_json(a, b, c))); },
    synth(kind, target = 4, seed = 1, seconds = 60, onset = 8) {
      const n = x.qs_synth(SYNTH[kind] ?? kind, target, seed, seconds, onset);
      const ax = (a) => Float32Array.from(new Float32Array(mem.buffer, x.qs_synth_axis(a), n));
      return { x: ax(0), y: ax(1), z: ax(2), fs: 100, p: x.qs_synth_p(), s: x.qs_synth_s() };
    },
    exact(xs, ys, zs, fs = 100) {
      const px = copyIn(xs), py = copyIn(ys), pz = copyIn(zs);
      try {
        const raw = x.qs_exact(px, py, pz, xs.length, fs);
        return { raw, intensity: x.qs_exact_field(0), a03: x.qs_exact_field(1), pga: x.qs_exact_field(2), shindoLabel: str(x.qs_shindo_label(x.qs_exact_field(3))) };
      } finally { x.qs_free(px); x.qs_free(py); x.qs_free(pz); }
    },
    realtimeOf(xs, ys, zs) {
      const px = copyIn(xs), py = copyIn(ys), pz = copyIn(zs);
      try { return x.qs_realtime_of(px, py, pz, xs.length); } finally { x.qs_free(px); x.qs_free(py); x.qs_free(pz); }
    },
    gainJma: (f) => x.qs_gain_jma(f),
    gainIir: (f) => x.qs_gain_iir(f),
  };
}
