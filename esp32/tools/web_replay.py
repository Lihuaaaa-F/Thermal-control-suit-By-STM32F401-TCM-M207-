#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""web_replay.py — 真机网页帧回放：把 `webtest dump` 转储的原始字节，回放进真实浏览器里的真实前端

为什么需要（U 线，2026-11-24）：开发机连不上设备热点（一连就断网；aTrust 拦截），
"手机打开真固件的网页"在本机做不了。最接近的一步是：让固件把自己的 HTTP 响应体和 WebSocket 帧
**原文**打到串口（现场档/台架档 `webtest dump`），这里解析出来，原样喂给仓库里的同一份前端。

  parse  串口日志 → frames.json：逐帧按 CRC32 校验，丢过字节的帧会被点名
  serve  起一个仅本机可访问的回放服务（默认 127.0.0.1:18130）：
           /                               仓库里的 index.html，只在 app.js 之前多插一行 replay-ws.js
           /app.js /chart.js /style.css    仓库里的同一份源文件（不压缩；真机发的是它们的 .gz）
           /api/status /api/registry /api/log /api/linkstats   真机转储的响应体原文
           /ws（由 /replay-ws.js 冒充）     页面连 /ws 时拿到的是 ReplaySocket：
             连上后按真机顺序推 reg、log、st；之后每 500ms 推一条转储里的 st（循环）；
             页面发来什么，就回一条转储里**同类型**的真机帧——set → pv、cmd → ack 或 err、ping → pong。
             只把 id / ref / v / c 这几个"必须等于请求里的值"的数字换成请求里的，其余字节不动。
           /__tx                           页面发往 /ws 的每一帧都追加到 <frames.json>.tx.jsonl
           /__report?name=<文件名>          浏览器端把检查结果 POST 回来，落到 frames.json 旁边

这不等于真机实测（传输层、手机浏览器、WiFi 都没经过）；它证明的是
"真固件发的字节，页面能正确解析和显示"。

在真实 Chrome 里逐项检查并出截图：node esp32/tools/web_replay_test.js <frames.json> <输出目录>

用法:
  python esp32/tools/web_replay.py parse <串口日志文件> <frames.json>
  python esp32/tools/web_replay.py serve <frames.json> [--port 18130]
"""
import argparse
import base64
import json
import os
import re
import sys
import urllib.parse
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
WEB_FILES = os.path.normpath(os.path.join(HERE, '..', 'components', 'tcs_web', 'web_files'))
TYPES = {'.html': 'text/html; charset=utf-8', '.js': 'application/javascript; charset=utf-8',
         '.css': 'text/css; charset=utf-8', '.json': 'application/json'}

WSD = re.compile(r'^WSD (\S+) (\d+) (\d+) ([0-9a-f]{8}) ([A-Za-z0-9+/=]*)\s*$')
RUN_MARK = re.compile(r'^webtest base=\S+ \(dump\)')

REPLAY_JS = r"""/* replay-ws.js — 由 esp32/tools/web_replay.py 生成：用真机转储的帧冒充 WebSocket（仅本机回放用） */
(function () {
  'use strict';
  var FR = __FRAMES__;
  function of(t) { return FR.filter(function (f) { return f.t === t; }).map(function (f) { return f.text; }); }
  var REG = of('reg'), LOG = of('log'), ST = of('st'), PONG = of('pong'), ACK = of('ack'), ERR = of('err');
  var PV_OK = of('pv').filter(function (s) { return /"ok":1/.test(s); });
  var PV_REJ = of('pv').filter(function (s) { return /"ok":0/.test(s); });
  var ERR_BAD_CMD = ERR.filter(function (s) { return /bad_cmd/.test(s); });
  var REJECT_ID = 3;          /* 发给 id 3 的 set 回"被约束拒绝"那条真机帧（回环自检里被拒的正是 id 3）*/
  var CMD_WHITELIST = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

  /* 只替换 fields 里列出的数值字段，其余字节保持真机原样。v 按固件的写法保留三位小数 */
  function subst(text, fields) {
    Object.keys(fields).forEach(function (k) {
      var val = Number(fields[k]);
      if (fields[k] === undefined || fields[k] === null || !isFinite(val)) return;
      var lit = (k === 'v') ? val.toFixed(3) : String(val);
      var re = new RegExp('"' + k + '":-?[0-9]+(?:\\.[0-9]+)?');
      if (re.test(text)) text = text.replace(re, '"' + k + '":' + lit);
    });
    return text;
  }
  function post(raw) {
    try { fetch('/__tx', { method: 'POST', body: raw, keepalive: true }); } catch (e) { /* 回放工具不在就算了 */ }
  }

  function ReplaySocket(url) {
    var self = this;
    this.url = url;
    this.readyState = 0;
    this.protocol = '';
    this.onopen = this.onmessage = this.onclose = this.onerror = null;
    setTimeout(function () {
      self.readyState = 1;
      if (self.onopen) self.onopen({ type: 'open' });
      REG.concat(LOG, ST.slice(0, 1)).forEach(function (t) { self._emit(t); });   /* 真机的首推顺序 */
      var i = 0;
      if (ST.length > 1) setInterval(function () { i = (i + 1) % ST.length; self._emit(ST[i]); }, 500);
    }, 30);
  }
  ReplaySocket.prototype._emit = function (t) {
    if (this.onmessage) this.onmessage({ type: 'message', data: t });
  };
  ReplaySocket.prototype.send = function (raw) {
    post(raw);
    var m, self = this;
    try { m = JSON.parse(raw); } catch (e) { return; }
    /* 回执异步送回（真网络不可能在 send() 返回之前到达；页面也是在 send() 之后才登记这次请求的）*/
    var later = function (text) { setTimeout(function () { self._emit(text); }, 5); };
    if (m.t === 'ping') {
      if (PONG.length) later(subst(PONG[0], { ref: m.ref }));
    } else if (m.t === 'set') {
      var ok = !(m.id === REJECT_ID && PV_REJ.length), pool = ok ? PV_OK : PV_REJ;
      if (pool.length) {
        /* 被拒绝时真机回的 v 是"没有被改动的当前值"——保持真机原样；被接受时才是请求值（真机回生效值）*/
        later(subst(pool[0], ok ? { id: m.id, v: m.v, ref: m.ref } : { id: m.id, ref: m.ref }));
      }
    } else if (m.t === 'cmd') {
      if (CMD_WHITELIST.indexOf(m.c) < 0 && ERR_BAD_CMD.length) later(ERR_BAD_CMD[0]);
      else if (ACK.length) later(subst(ACK[0], { c: m.c, ref: m.ref }));
    }
  };
  ReplaySocket.prototype.close = function () {
    this.readyState = 3;
    if (this.onclose) this.onclose({ type: 'close', code: 1000 });
  };
  ReplaySocket.CONNECTING = 0; ReplaySocket.OPEN = 1; ReplaySocket.CLOSING = 2; ReplaySocket.CLOSED = 3;
  window.WebSocket = ReplaySocket;
})();
"""


def parse_log(path):
    """返回 {'http': {路径: 文本}, 'ws': [{tag,t,text}], 'meta': {...}}；取日志里最后一次 dump。"""
    with open(path, 'rb') as f:
        text = f.read().decode('utf-8', 'replace')
    runs, cur, nrun = [], None, 0
    for line in text.splitlines():
        if RUN_MARK.match(line):
            cur = {}
            runs.append(cur)
            nrun += 1
            continue
        m = WSD.match(line)
        if not m:
            continue
        if cur is None:                 # 日志是从半截开始的：也收，但记为第 0 组
            cur = {}
            runs.append(cur)
        tag, idx, nch, crc, b64 = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5)
        cur.setdefault(tag, {})[idx] = (nch, crc, b64)
    if not runs:
        raise SystemExit('no "webtest base=... (dump)" run found in %s' % path)
    run = runs[-1]
    http, ws, bad, total = {}, [], [], 0
    for tag in sorted(run, key=lambda t: (t[0], int(re.sub(r'\D', '', t) or 0))):
        parts = run[tag]
        nch = next(iter(parts.values()))[0]
        if sorted(parts) != list(range(nch)):
            bad.append({'tag': tag, 'why': 'chunks %d/%d' % (len(parts), nch)})
            continue
        crc = next(iter(parts.values()))[1]
        try:
            data = base64.b64decode(''.join(parts[i][2] for i in range(nch)), validate=True)
        except Exception as e:      # noqa: BLE001 — 串口丢字节时 base64 可能残缺
            bad.append({'tag': tag, 'why': 'base64: %s' % e})
            continue
        if (zlib.crc32(data) & 0xFFFFFFFF) != int(crc, 16):
            bad.append({'tag': tag, 'why': 'crc mismatch (got %08x want %s)' % (zlib.crc32(data) & 0xFFFFFFFF, crc)})
            continue
        total += len(data)
        body = data.decode('utf-8', 'replace')
        if tag.startswith('H'):
            http[tag[1:]] = body
        else:
            try:
                t = json.loads(body).get('t')
            except ValueError:
                t = None
            ws.append({'tag': tag, 't': t, 'text': body})
    meta = {'source': os.path.basename(path), 'runs_in_log': nrun, 'ws_frames': len(ws),
            'http_bodies': len(http), 'bytes_crc_ok': total, 'bad': bad}
    return {'meta': meta, 'http': http, 'ws': ws}


def make_server(frames_path, port):
    with open(frames_path, encoding='utf-8') as f:
        fr = json.load(f)
    tx_path = frames_path + '.tx.jsonl'
    out_dir = os.path.dirname(os.path.abspath(frames_path))
    with open(os.path.join(WEB_FILES, 'index.html'), encoding='utf-8') as f:
        index = f.read()
    inject = '<script src="/replay-ws.js"></script>\n'
    if '<script src="/app.js"></script>' not in index:
        raise SystemExit('index.html 里找不到 <script src="/app.js"></script>，无法插入回放脚本')
    index = index.replace('<script src="/app.js"></script>', inject + '<script src="/app.js"></script>', 1)
    replay_js = REPLAY_JS.replace('__FRAMES__', json.dumps(fr['ws'], ensure_ascii=False))
    have = {p[5:]: (p in fr['http']) for p in ('/api/status', '/api/registry', '/api/log', '/api/linkstats')}

    class Handler(BaseHTTPRequestHandler):
        def _send(self, code, body, ctype, extra=None):
            if isinstance(body, str):
                body = body.encode('utf-8')
            self.send_response(code)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urllib.parse.urlparse(self.path).path
            if path in ('/', '/index.html'):
                return self._send(200, index, TYPES['.html'])
            if path == '/replay-ws.js':
                return self._send(200, replay_js, TYPES['.js'])
            if path in ('/app.js', '/chart.js', '/style.css'):
                with open(os.path.join(WEB_FILES, path[1:]), 'rb') as f:
                    return self._send(200, f.read(), TYPES[os.path.splitext(path)[1]])
            if path.startswith('/api/'):
                if path in fr['http']:
                    return self._send(200, fr['http'][path], TYPES['.json'])
                return self._send(404, '{"error":"not in dump"}', TYPES['.json'])
            return self._send(404, 'not found (replay)', 'text/plain; charset=utf-8')

        def do_POST(self):
            u = urllib.parse.urlparse(self.path)
            n = int(self.headers.get('Content-Length') or 0)
            body = self.rfile.read(n) if n else b''
            if u.path == '/__tx':
                with open(tx_path, 'ab') as f:
                    f.write(body.rstrip(b'\r\n') + b'\n')
                return self._send(204, b'', 'text/plain')
            if u.path == '/__report':
                name = urllib.parse.parse_qs(u.query).get('name', ['report'])[0]
                if not re.fullmatch(r'[A-Za-z0-9_.-]{1,64}', name):
                    return self._send(400, 'bad name', 'text/plain')
                with open(os.path.join(out_dir, name), 'wb') as f:
                    f.write(body)
                return self._send(200, 'saved %s' % name, 'text/plain; charset=utf-8')
            return self._send(404, 'not found (replay)', 'text/plain')

        def log_message(self, fmt, *args):     # 默认每条请求打一行 stderr，回放时太吵
            pass

    srv = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    return srv, have


def main():
    ap = argparse.ArgumentParser(description='真机网页帧回放（见文件头说明）')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('parse')
    p.add_argument('log')
    p.add_argument('out')
    s = sub.add_parser('serve')
    s.add_argument('frames')
    s.add_argument('--port', type=int, default=18130)
    a = ap.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8', errors='replace')
        except Exception:      # noqa: BLE001
            pass
    if a.cmd == 'parse':
        fr = parse_log(a.log)
        with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(fr, f, ensure_ascii=False, indent=1)
        m = fr['meta']
        print('runs=%d  ws frames=%d  http bodies=%d  bytes(crc ok)=%d  bad=%d' %
              (m['runs_in_log'], m['ws_frames'], m['http_bodies'], m['bytes_crc_ok'], len(m['bad'])))
        for w in fr['ws']:
            print('  %s t=%-5s %5d B' % (w['tag'], w['t'], len(w['text'].encode('utf-8'))))
        for k in sorted(fr['http']):
            print('  H%s %5d B' % (k, len(fr['http'][k].encode('utf-8'))))
        for b in m['bad']:
            print('  BAD %s: %s' % (b['tag'], b['why']))
        return 1 if m['bad'] else 0
    srv, have = make_server(a.frames, a.port)
    print('replay: http://127.0.0.1:%d/   api from device: %s' %
          (a.port, ', '.join('%s=%s' % (k, 'yes' if v else 'NO') for k, v in have.items())))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
