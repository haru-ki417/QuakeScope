"""C++ の計測震度の計算を、numpy で別に書いた計算と比べる（独立した確認）。
    python3 check_exact.py ./qs_cli
"""
import subprocess, sys, io
import numpy as np

def jma_gain(f):
    f = np.asarray(f, float); x = f / 10
    with np.errstate(divide='ignore'):
        period = np.where(f > 0, 1 / np.sqrt(np.where(f > 0, f, 1)), 0)
    hc = (1 + 0.694 * x**2 + 0.241 * x**4 + 0.0557 * x**6 + 0.009664 * x**8 + 0.00134 * x**10 + 0.000155 * x**12) ** -0.5
    lc = np.sqrt(1 - np.exp(-(f / 0.5) ** 3))
    return period * hc * lc

def intensity(a, fs=100.0):
    n = len(a); nfft = 1 << int(np.ceil(np.log2(2 * n)))
    acc = np.zeros(n)
    for c in range(3):
        s = a[:, c] - a[:, c].mean()
        F = np.fft.rfft(s, nfft) * jma_gain(np.fft.rfftfreq(nfft, 1 / fs))
        acc += np.fft.irfft(F, nfft)[:n] ** 2
    v = np.sort(np.sqrt(acc))[::-1]
    a03 = v[int(round(0.3 * fs)) - 1]
    return 2 * np.log10(a03) + 0.94

cli = sys.argv[1]
worst = 0
for kind in ('near', 'far', 'truck', 'door'):
    for target in (1.0, 3.3, 5.6):
        for seed in (1, 2):
            csv = subprocess.run([cli, 'synth', kind, str(target), str(seed), '90'], capture_output=True, text=True).stdout
            a = np.loadtxt(io.StringIO(csv), delimiter=',', skiprows=1)
            ref = intensity(a)
            out = subprocess.run([cli, 'intensity'], input=csv, capture_output=True, text=True).stdout.split()
            cpp = float(out[0])
            worst = max(worst, abs(ref - cpp))
            print(f'{kind:5s} I={target:3.1f} seed={seed}  numpy={ref:7.4f}  C++={cpp:7.4f}  差={abs(ref-cpp):.5f}')
print('最大の差', worst)
sys.exit(0 if worst < 1e-3 else 1)
