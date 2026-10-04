"""気象庁の計測震度のフィルター（周波数領域）を、リアルタイムで使える IIR フィルター（アナログの原型 → 双一次変換）で近似する。
結果の係数は core/src/jma_filter_coeffs.h に書き出す。
参考: 気象庁「計測震度の算出方法」（フィルターの式）。
"""
import numpy as np
from scipy.optimize import least_squares

def jma_mag(f):
    f = np.asarray(f, float)
    x = f / 10.0
    period = np.where(f > 0, 1.0 / np.sqrt(np.maximum(f, 1e-12)), 0.0)
    hc = 1.0 / np.sqrt(1 + 0.694 * x**2 + 0.241 * x**4 + 0.0557 * x**6 + 0.009664 * x**8 + 0.00134 * x**10 + 0.000155 * x**12)
    lc = np.sqrt(np.maximum(0.0, 1 - np.exp(-(f / 0.5) ** 3)))
    return period * hc * lc

# 構成: [HP 1次 fa] [HP 2次 fb,hb] [LP 2次 fc,hc] [LP 2次 fd,hd] [shelf (s+we)/(s+wf)] [shelf (s+wg)/(s+wh)] × K
def analog_resp(p, f):
    fa, fb, hb, fc, hc_, fd, hd, fe, ff, fg, fh, lk = p
    s = 2j * np.pi * f
    w = lambda q: 2 * np.pi * q
    H = s / (s + w(fa))
    H *= s**2 / (s**2 + 2 * hb * w(fb) * s + w(fb) ** 2)
    H *= w(fc) ** 2 / (s**2 + 2 * hc_ * w(fc) * s + w(fc) ** 2)
    H *= w(fd) ** 2 / (s**2 + 2 * hd * w(fd) * s + w(fd) ** 2)
    H *= (s + w(fe)) / (s + w(ff))
    H *= (s + w(fg)) / (s + w(fh))
    return np.exp(lk) * H

f = np.logspace(np.log10(0.05), np.log10(45), 600)
target = jma_mag(f)
# 重み: 地震動のエネルギーが多い 0.3〜12 Hz を重く
wt = np.where((f > 0.3) & (f < 12), 3.0, 1.0)
def resid(p):
    h = np.abs(analog_resp(p, f))
    return wt * (20 * np.log10(h + 1e-9) - 20 * np.log10(target + 1e-9)) * (target > 1e-3)

p0 = [0.45, 0.5, 0.7, 7.0, 0.7, 12.0, 0.7, 2.0, 0.8, 10.0, 4.0, 0.0]
lo = [0.05, 0.05, 0.3, 2, 0.2, 4, 0.2, 0.1, 0.05, 0.5, 0.2, -10]
hi = [5, 5, 2.0, 30, 2.0, 40, 2.0, 30, 30, 40, 40, 10]
best = None
rng = np.random.default_rng(1)
for trial in range(40):
    start = p0 if trial == 0 else [rng.uniform(a, b) for a, b in zip(lo, hi)]
    try:
        r = least_squares(resid, start, bounds=(lo, hi), max_nfev=4000)
    except Exception:
        continue
    if best is None or r.cost < best.cost:
        best = r
p = best.x
h = np.abs(analog_resp(p, f))
err_db = 20 * np.log10(h / target)
band = (f > 0.3) & (f < 12)
print('params', np.round(p, 4).tolist())
print('max |err| dB 0.3-12Hz: %.3f  0.1-20Hz: %.3f' % (np.abs(err_db[band]).max(), np.abs(err_db[(f > 0.1) & (f < 20)]).max()))
np.save('jma_fit.npy', p)
