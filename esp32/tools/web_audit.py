#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""web_audit.py — K8 网页端设计系统+功能零回退断言（375px，H7 口径续档）

构建 web_files 的本地断言副本（注入审计 JS，结果写 <pre id="audit-out">），
headless Chrome dump-dom 解析 + 375px 截图（K7 视觉包用）。
需 web_mock 在 8123 运行（python esp32/tools/web_mock.py）。
用法: python esp32/tools/web_audit.py
"""
import json
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

CHROME = r"C:\Program Files\Google\Chrome\Application\chrome.exe"
ROOT = Path(__file__).resolve().parent.parent
EV = ROOT.parent / "docs/evidence"
TMP = ROOT / "tools/_web_audit_page.html"

INJECT = """
window.__audit = (async () => {
  const sleep = (ms) => new Promise(r => setTimeout(r, ms));
  const r = {};
  for (let i = 0; i < 30; i++) { await sleep(200);
    if (document.getElementById("conn") && document.getElementById("conn").textContent === "已连接") break; }
  await sleep(1200);
  const cs = getComputedStyle(document.body);
  r.bg = cs.backgroundColor;
  const estop = document.getElementById("btn-estop");
  r.estop_bg = getComputedStyle(estop).backgroundColor;
  r.estop_h = Math.round(estop.getBoundingClientRect().height);
  const small = [];
  document.querySelectorAll("button, input[type=range], summary").forEach(el => {
    const b = el.getBoundingClientRect();
    if (b.height > 0 && b.height < 43.5 && !el.closest(".hidden") && el.offsetParent)
      small.push((el.id || el.className) + ":" + Math.round(b.height) + "px");
  });
  r.small_targets = small;
  r.conn = document.getElementById("conn").textContent;
  r.t1 = document.getElementById("t1").textContent;
  r.t2 = document.getElementById("t2").textContent;
  r.tb = document.getElementById("tb").textContent;
  r.nparams = document.querySelectorAll("#params .prow").length;
  r.nadv = document.querySelectorAll("#adv-params .prow").length;
  r.sliders = document.querySelectorAll("#params input[type=range]").length;
  const plus = document.querySelector("#params .stepper button");
  const inp = plus && plus.closest(".prow").querySelector("input");
  if (inp) { const v0 = +inp.value; plus.click(); await sleep(500);
    r.step_delta = +(+inp.value - v0).toFixed(3); }
  let confirmTxt = null;
  window.confirm = (m) => { confirmTxt = m; return false; };
  estop.click(); await sleep(150);
  r.estop_confirm = confirmTxt;
  try {
    const orig = HTMLAnchorElement.prototype.click; let csvName = null;
    HTMLAnchorElement.prototype.click = function(){ csvName = this.download; };
    document.getElementById("btn-csv").click();
    HTMLAnchorElement.prototype.click = orig;
    r.csv = csvName;
  } catch(e) { r.csv_err = String(e); }
  r.overlay = !!document.getElementById("overlay");
  return r;
})();
window.__audit.then(r => {
  const pre = document.createElement("pre");
  pre.id = "audit-out";
  pre.textContent = "AUDIT_JSON:" + JSON.stringify(r);
  document.body.appendChild(pre);
  document.title = "AUDIT_DONE";
});
"""


def fetch(url):
    return urllib.request.urlopen(url, timeout=10).read().decode("utf-8")


def main():
    import json as _json
    import time as _time
    import urllib.request
    import websocket

    base = "http://127.0.0.1:8123"
    dbg = subprocess.Popen(
        [CHROME, "--headless=new", "--disable-gpu", "--no-sandbox",
         "--remote-debugging-port=9333", "--remote-allow-origins=*", "--window-size=375,760",
         "--user-data-dir=" + str(ROOT / "tools/_chrome_prof")],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        ws_url = None
        for _ in range(30):
            _time.sleep(0.5)
            try:
                tabs = _json.loads(urllib.request.urlopen(
                    "http://127.0.0.1:9333/json", timeout=5).read())
                pages = [t for t in tabs if t.get("type") == "page"]
                if pages:
                    ws_url = pages[0]["webSocketDebuggerUrl"]
                    break
            except Exception:
                continue
        if not ws_url:
            print("[web_audit] FAIL: CDP 连接失败")
            return 1
        ws = websocket.create_connection(ws_url, timeout=30)
        _id = [0]

        def call(method, **params):
            _id[0] += 1
            ws.send(_json.dumps({"id": _id[0], "method": method, "params": params}))
            while True:
                msg = _json.loads(ws.recv())
                if msg.get("id") == _id[0]:
                    return msg

        call("Page.enable")
        call("Emulation.setDeviceMetricsOverride", width=375, height=760,
             deviceScaleFactor=1, mobile=True)
        call("Page.navigate", url=base + "/__audit")
        _time.sleep(4.5)                      # WS 首帧 + ticker 帧真实到达
        expr = "window.__audit.then(r => r)"   # 页面自带 promise（/__audit 注入）
        res = call("Runtime.evaluate", expression=expr, awaitPromise=True,
                   returnByValue=True)
        r = res["result"]["result"].get("value")
        if not r:
            print("[web_audit] FAIL: 断言未产出", res)
            return 1
        shot = call("Page.captureScreenshot", format="png")
        import base64
        (EV / "K8_web_375.png").write_bytes(base64.b64decode(shot["result"]["data"]))
        ws.close()
    finally:
        dbg.terminate()

    ok = True

    def chk(name, cond, detail):
        nonlocal ok
        ok &= bool(cond)
        print(f"  {'PASS' if cond else 'FAIL'} {name}: {detail}")

    chk("底色=灰阶", "238, 241, 245" in r.get("bg", ""), r.get("bg"))
    chk("急停实红", "220, 38, 38" in r.get("estop_bg", ""), r.get("estop_bg"))
    chk("急停≥64px", r.get("estop_h", 0) >= 63.5, r.get("estop_h"))
    chk("触控目标≥44", not r.get("small_targets"), r.get("small_targets"))
    chk("WS 已连接", r.get("conn") == "已连接", r.get("conn"))
    chk("状态帧渲染", r.get("t1") not in ("", "--.-") and r.get("tb") not in ("", "--.-"),
        f"t1={r.get('t1')} tb={r.get('tb')}")
    chk("参数行(PF_WEB)", r.get("nparams", 0) >= 7, f"{r.get('nparams')} 行/滑条 {r.get('sliders')}")
    chk("步进生效", r.get("step_delta", 0) != 0, r.get("step_delta"))
    chk("急停二次确认", "确认急停" in (r.get("estop_confirm") or ""), r.get("estop_confirm"))
    chk("CSV 导出", r.get("csv") == "tcs_body_log.csv", r.get("csv") or r.get("csv_err"))
    chk("断连灰罩结构", r.get("overlay") is True, r.get("overlay"))
    chk("页面无异常", not r.get("errors"), r.get("errors"))

    dest = EV / "K8_web_audit.json"
    with open(dest, "w", encoding="utf-8") as f:
        json.dump(r, f, ensure_ascii=False, indent=1)
    print(f"[web_audit] {'ALL PASS' if ok else 'HAS FAIL'} → {dest} + K8_web_375.png")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
