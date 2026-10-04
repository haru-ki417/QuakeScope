"""計測震度フィルターを、標本化 100 Hz のデジタル IIR（2 次の区間 × 5）で近似する。
アナログの原型の各極・零点を双一次変換（周波数のゆがみを補正）でデジタルにし、デジタルの周波数特性で直接合わせる。
出力: ../core/src/jma_filter_coeffs.h
"""
import numpy as np
from scipy.optimize import least_squares
from scipy import signal
from fit_jma_filter import jma_mag

FS = 100.0

def warp(fc):
    # デジタル周波数 fc に対応するアナログ角周波数
    return 2 * FS * np.tan(np.pi * fc / FS)

def sections(p):
    fa, fb, hb, fc, hc_, fd, hd, fe, ff, fg, fh, lk = p
    sos = []
    def bil(b, a):
        bz, az = signal.bilinear(b, a, fs=FS)
        return np.concatenate([np.pad(bz, (0, 3 - len(bz))), np.pad(az, (0, 3 - len(az)))])
    wa, wb, wc, wd, we, wf, wg, wh = map(warp, (fa, fb, fc, fd, fe, ff, fg, fh))
    sos.append(bil([1, 0], [1, wa]))                       # 低域を切る（1 次）
    sos.append(bil([1, 0, 0], [1, 2 * hb * wb, wb**2]))     # 低域を切る（2 次）
    sos.append(bil([wc**2], [1, 2 * hc_ * wc, wc**2]))      # 高域を切る
    sos.append(bil([wd**2], [1, 2 * hd * wd, wd**2]))       # 高域を切る
    sos.append(bil(np.polymul([1, we], [1, wg]), np.polymul([1, wf], [1, wh])))  # 周期の効果（2 つの棚）
    sos = np.array(sos)
    sos[:, :3] /= sos[:, 3:4]; sos[:, 3:] /= sos[:, 3:4]
    sos[0, :3] *= np.exp(lk)
    return sos

f = np.logspace(np.log10(0.05), np.log10(45), 700)
target = jma_mag(f)
wt = np.where((f > 0.3) & (f < 12), 3.0, 1.0)
def resp(p):
    _, h = signal.sosfreqz(sections(p), worN=f, fs=FS)
    return np.abs(h)
def resid(p):
    return wt * (20 * np.log10(resp(p) + 1e-12) - 20 * np.log10(target + 1e-12)) * (target > 1e-3)

lo = [0.05, 0.05, 0.3, 2, 0.2, 4, 0.2, 0.1, 0.05, 0.3, 0.05, -10]
hi = [5, 5, 2.0, 40, 2.0, 45, 2.0, 40, 40, 45, 45, 10]
p0 = np.load('jma_fit.npy')
p0 = np.clip(p0, np.array(lo) + 1e-6, np.array(hi) - 1e-6)
best = least_squares(resid, p0, bounds=(lo, hi), max_nfev=6000)
rng = np.random.default_rng(7)
for _ in range(30):
    st = np.clip(best.x * rng.uniform(0.7, 1.3, len(p0)), np.array(lo) + 1e-6, np.array(hi) - 1e-6)
    r = least_squares(resid, st, bounds=(lo, hi), max_nfev=6000)
    if r.cost < best.cost: best = r
p = best.x
err = 20 * np.log10(resp(p) / target)
b1 = (f > 0.3) & (f < 12); b2 = (f > 0.1) & (f < 20)
print('params', np.round(p, 4).tolist())
print('max |err| dB  0.3-12Hz %.3f   0.1-20Hz %.3f' % (np.abs(err[b1]).max(), np.abs(err[b2]).max()))
sos = sections(p)
# 安定性の確認
for row in sos:
    assert np.all(np.abs(np.roots(row[3:])) < 1), 'unstable'
np.save('jma_sos.npy', sos)
lines = ['// 自動生成: tools/fit_jma_digital.py（気象庁の計測震度フィルターを 100 Hz の IIR で近似）',
         '// 近似の誤差: 0.3〜12 Hz で最大 %.2f dB、0.1〜20 Hz で最大 %.2f dB' % (np.abs(err[b1]).max(), np.abs(err[b2]).max()),
         '#pragma once', '', 'namespace qs {', '', 'constexpr int kJmaSections = %d;' % len(sos),
         '// 各行: b0, b1, b2, a1, a2（a0 = 1）', 'constexpr double kJmaSos[kJmaSections][5] = {']
for r in sos:
    lines.append('    {%s},' % ', '.join('%.17g' % v for v in (r[0], r[1], r[2], r[4], r[5])))
lines += ['};', '', 'constexpr double kJmaSampleRate = 100.0;', '', '}  // namespace qs', '']
import os
os.makedirs('../core/src', exist_ok=True)
open('../core/src/jma_filter_coeffs.h', 'w').write('\n'.join(lines))
print(open('../core/src/jma_filter_coeffs.h').read())
