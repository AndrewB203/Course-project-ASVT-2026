#!/usr/bin/env python3
# OVS dev server: serves frontend/ and proxies /api/* to the backend.
import argparse, json, sys, time, urllib.request, urllib.error
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

MIME_EXTRA = {'.mjs': 'application/javascript', '.wasm': 'application/wasm',
              '.webp': 'image/webp', '.woff2': 'font/woff2', '.map': 'application/json'}

def probe_backend(ports):
    for port in ports:
        url = 'http://127.0.0.1:%d/' % port
        try:
            with urllib.request.urlopen(url, timeout=0.5):
                return 'http://127.0.0.1:%d' % port
        except urllib.error.HTTPError:
            return 'http://127.0.0.1:%d' % port
        except Exception:
            continue
    return None

class Handler(SimpleHTTPRequestHandler):
    backend = 'http://127.0.0.1:8000'
    root = '.'

    def __init__(self, *a, **kw):
        super().__init__(*a, directory=self.root, **kw)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stdout.write('[%s] %s\n' % (time.strftime('%H:%M:%S'), fmt % args))

    def _is_api(self):
        return self.path.split('?')[0].startswith('/api')

    def _proxy(self):
        length = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(length) if length else None
        req = urllib.request.Request(self.backend + self.path, data=body, method=self.command)
        for k, v in self.headers.items():
            if k.lower() in ('host', 'connection', 'content-length', 'accept-encoding'):
                continue
            req.add_header(k, v)
        if body is not None:
            req.add_header('Content-Length', str(len(body)))
        try:
            with urllib.request.urlopen(req, timeout=15) as r:
                data = r.read()
                self.send_response(r.status)
                for k, v in r.headers.items():
                    if k.lower() in ('transfer-encoding', 'connection', 'content-length', 'content-encoding'):
                        continue
                    self.send_header(k, v)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)
        except urllib.error.HTTPError as e:
            data = e.read()
            self.send_response(e.code)
            self.send_header('Content-Type', e.headers.get('Content-Type', 'application/json'))
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except Exception as e:
            payload = json.dumps({'error': 'backend unavailable',
                                  'backend': self.backend, 'detail': str(e)}).encode('utf-8')
            self.send_response(502)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    def do_GET(self):
        if self._is_api():
            return self._proxy()
        p = Path(self.translate_path(self.path))
        if not p.exists() and not p.suffix:
            self.path = '/index.html'
        return super().do_GET()

    def do_HEAD(self):
        if self._is_api():
            return self._proxy()
        return super().do_HEAD()

    def do_POST(self):
        if self._is_api():
            return self._proxy()
        self.send_error(405, 'Static server: POST only for /api')

    do_PUT = do_POST
    do_DELETE = do_POST
    do_PATCH = do_POST

    def do_OPTIONS(self):
        if self._is_api():
            return self._proxy()
        self.send_response(204)
        self.send_header('Allow', 'GET, HEAD, OPTIONS')
        self.end_headers()

Handler.extensions_map = {**SimpleHTTPRequestHandler.extensions_map, **MIME_EXTRA}

def main():
    ap = argparse.ArgumentParser(description='OVS dev server: static + /api proxy')
    ap.add_argument('--root', default=str(Path(__file__).resolve().parent.parent / 'frontend'))
    ap.add_argument('--port', type=int, default=8080)
    ap.add_argument('--backend', default=None, help='e.g. http://127.0.0.1:8000')
    args = ap.parse_args()

    root = Path(args.root).resolve()
    if not (root / 'index.html').exists():
        print('ERROR: no index.html in %s' % root)
        sys.exit(1)

    backend = args.backend
    if not backend:
        backend = probe_backend([8000, 8001, 5000, 3000, 8081, 5001])
        if backend:
            print('Auto-detected backend: %s' % backend)
        else:
            backend = 'http://127.0.0.1:8000'
            print('WARNING: no backend detected, defaulting to %s (start it or pass --backend)' % backend)

    Handler.backend = backend.rstrip('/')
    Handler.root = str(root)
    srv = ThreadingHTTPServer(('0.0.0.0', args.port), Handler)
    srv.daemon_threads = True
    print('Serving %s' % root)
    print('Local:   http://127.0.0.1:%d/' % args.port)
    print('API ->   %s/api/*' % Handler.backend)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print('\nStopped.')

if __name__ == '__main__':
    main()