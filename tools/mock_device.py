"""本体の Web 画面を、本体なしで動かして確かめるための仮のサーバー（開発用）。
合成した揺れ（core の qs_cli で作る）をくり返し流し、本体と同じ形の API を返す。
    python3 tools/mock_device.py <qs_cli のパス> [--setup] [--port 8080]
設定のパスワードは test-pass-123
"""
import base64, json, math, struct, subprocess, sys, time, io
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs
import numpy as np

CLI = sys.argv[1]
SETUP = '--setup' in sys.argv
PORT = int(sys.argv[sys.argv.index('--port') + 1]) if '--port' in sys.argv else 8080
PAGE = open('firmware/web/index.html', 'rb').read()

csv = subprocess.run([CLI, 'synth', 'near', '4.3', '3', '70'], capture_output=True, text=True).stdout
w = np.loadtxt(io.StringIO(csv), delimiter=',', skiprows=1)
w[:, 2] -= 980.665
mag = np.sqrt((w ** 2).sum(1))
env = np.array([mag[i:i + 10].max() for i in range(0, len(mag), 10)])  # 0.1 秒ごと
t0 = time.time()
settings = {'place': 'リビング', 'oled': 64, 'beep': True, 'volume': 80, 'ic': '2.5', 'iw': '4.5', 'nmin': '2', 'nend': True,
            'lon': True, 'luser': 'U0123456789abcdef', 'ltokSet': True, 'ton': False, 'tchat': '', 'ttokSet': False,
            'don': True, 'dhookSet': True, 'mon': False, 'mhost': 'smtp.gmail.com', 'mport': '465', 'muser': '', 'mto': '', 'mpassSet': False,
            'ssid': 'home-wifi'}
now = int(time.time())
events = [
    {'id': 3, 'start': now - 3600 * 5, 'duration': 41.0, 'intensity': 4.3, 'shindo': '4', 'pga': 96.4, 'level': 2, 'notified': True, 'samples': 2950, 'fs': 50, 'unit': 0.1},
    {'id': 2, 'start': now - 86400 * 2, 'duration': 12.0, 'intensity': 1.8, 'shindo': '2', 'pga': 6.1, 'level': 1, 'notified': False, 'samples': 1200, 'fs': 50, 'unit': 0.1},
    {'id': 1, 'start': now - 86400 * 9, 'duration': 8.0, 'intensity': 0.7, 'shindo': '1', 'pga': 2.3, 'level': 1, 'notified': False, 'samples': 900, 'fs': 50, 'unit': 0.1},
]
logs = [{'t': now - 3600 * 5 + 4, 'ch': 'LINE', 'title': '【揺れを検知】リビングで震度4相当の揺れ', 'code': 200, 'ok': True},
        {'t': now - 3600 * 5 + 5, 'ch': 'Discord', 'title': '【揺れを検知】リビングで震度4相当の揺れ', 'code': 204, 'ok': True},
        {'t': now - 3600 * 5 + 50, 'ch': 'LINE', 'title': '【揺れがおさまりました】リビング 最大震度4相当', 'code': 200, 'ok': True}]

def status():
    k = int((time.time() - t0) * 10) % len(env)
    hist = [int(env[(k - 127 + i) % len(env)] * 10) for i in range(128)]
    tt = k / 10
    lvl = 0 if tt < 15.6 else 1 if tt < 17.5 else 2 if tt < 55 else 0
    inow = max(0.0, min(4.2, 2 * math.log10(max(env[k] * 0.35, 1e-3)) + 0.94))
    return {'setup': SETUP, 'name': 'QuakeScope-1A2B', 'host': 'QuakeScope-1A2B.local', 'version': '1.0.0', 'place': settings['place'],
            'time': time.strftime('%Y/%-m/%-d %H:%M:%S'), 'uptime': int(time.time() - t0) + 7200, 'ic': 2.5, 'iw': 4.5,
            'wifi': {'ssid': 'home-wifi', 'rssi': -54, 'ip': '192.168.1.42'}, 'pending': 0, 'fsUsed': 61440, 'fsTotal': 196608,
            'engine': {'calibrated': True, 'sensorOk': True, 'inEvent': lvl > 0, 'level': lvl, 'iNow': round(inow, 2),
                       'iEvent': round(min(4.3, max(inow, 0.6 if lvl else 0) if tt < 22 else 4.3), 2), 'pga': round(float(mag[:k * 10 + 1].max()), 1) if lvl else 0,
                       'staLta': 1.0, 'noise': 0.25, 'noiseI': -0.6}, 'hist': hist}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ctype='application/json'):
        if isinstance(body, (dict, list)): body = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def authed(self):
        a = self.headers.get('Authorization', '')
        ok = a == 'Basic ' + base64.b64encode('admin:test-pass-123'.encode()).decode()
        if not ok: self.send(401, {'error': 'auth'})
        return ok
    def do_GET(self):
        u = urlparse(self.path)
        if u.path == '/': return self.send(200, PAGE, 'text/html; charset=utf-8')
        if u.path == '/api/status': return self.send(200, status())
        if u.path == '/api/scan': return self.send(200, [{'ssid': 'home-wifi', 'rssi': -50}, {'ssid': 'home-wifi-5G', 'rssi': -61}, {'ssid': 'neighbor', 'rssi': -80}])
        if u.path == '/api/events': return self.send(200, events)
        if u.path == '/api/logs': return self.send(200, logs)
        if u.path == '/api/settings': return self.authed() and self.send(200, settings)
        if u.path == '/api/event':
            d = w[1000:1000 + 5900:2]
            return self.send(200, (np.clip(d * 10, -32768, 32767)).astype('<i2').tobytes(), 'application/octet-stream')
        self.send(404, b'not found', 'text/plain')
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0)); self.rfile.read(n)
        if self.path == '/api/setup': return self.send(200, {'ok': True})
        if self.authed(): self.send(200, {'ok': True})

print(f'http://localhost:{PORT}/', '(setup)' if SETUP else '')
ThreadingHTTPServer(('127.0.0.1', PORT), H).serve_forever()
