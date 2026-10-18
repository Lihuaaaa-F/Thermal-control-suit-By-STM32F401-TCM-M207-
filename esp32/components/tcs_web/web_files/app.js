/* app.js — 调温服网页逻辑（任务书 §4.3）
 * WS: 收 {"t":"st",...}(500ms) / {"t":"pv","id","v","src"}；发 {"t":"set",...}/{"t":"cmd","c"}
 * 注册表驱动参数区（PF_WEB），M207 参数自动进高级区（D6：屏不显，网页显示）。
 * 断连灰罩+重连倒计时；多次失败「请靠近设备」。急停=红色大按钮+二次确认。
 */
"use strict";
const $ = (id) => document.getElementById(id);
const ALM = { BODY: 1, M207: 2, ESTOP: 4, OVERTEMP: 8, EEPROM: 16 };
const ALM_TXT = {
  1: "体温传感器失联（TEC 已停，风扇全开）",
  2: "M207 失联（TEC 已停，风扇全开）",
  4: "急停已触发",
  8: "水温过高（>48°C）",
  16: "EEPROM 配置无效，已恢复默认",
};
const PROTO_VER_EXPECT = 1;

let ws = null, wsAlive = false, retry = 0, retryTimer = null;
let registry = [];                 /* [{id,type,flags,min,max,step,key,name,unit}] */
let lastStatus = null;
let built = false;
let webAckWait = null;             /* {id, us} 等自己的 set 回显 */
let ackToastShownFor = 0;
let curve = null, curvePts = [], curveLastT = 0;
let upTimer = null;

/* ====== WebSocket ====== */
function wsConnect() {
  clearTimeout(retryTimer);
  const proto = location.protocol === "https:" ? "wss" : "ws";
  ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen = () => { wsAlive = true; retry = 0; overlay(false); $("conn").textContent = "已连接"; $("dot").classList.add("on"); };
  ws.onclose = () => { wsAlive = false; $("dot").classList.remove("on"); scheduleRetry(); };
  ws.onerror = () => { try { ws.close(); } catch (e) {} };
  ws.onmessage = (ev) => {
    let m; try { m = JSON.parse(ev.data); } catch (e) { return; }
    if (m.t === "st") onStatus(m);
    else if (m.t === "pv") onPv(m);
    else if (m.t === "reg") { registry = m.list || []; buildParams(); }
  };
}
function scheduleRetry() {
  overlay(true, ++retry);
  const wait = Math.min(5, retry);
  $("conn").textContent = `重连(${wait}s)`;
  retryTimer = setTimeout(wsConnect, wait * 1000);
}
function send(obj) { if (wsAlive && ws.readyState === 1) ws.send(JSON.stringify(obj)); }

/* ====== 状态 ====== */
function onStatus(s) {
  lastStatus = s;
  $("app").classList.remove("hidden");
  $("t1").textContent = s.tc1_act.toFixed(1);
  $("t2").textContent = s.tc2_act.toFixed(1);
  $("tb").textContent = s.body_sensor_ok === false ? "--.-" : s.body.toFixed(1);
  drawGauge($("g1"), s.tc1_act, 5, 45);
  drawGauge($("g2"), s.tc2_act, 5, 45);
  drawGauge($("g3"), s.body_sensor_ok === false ? null : s.body, 35, 40);

  $("sw-tec1").classList.toggle("on", !!s.tec1);
  $("sw-tec2").classList.toggle("on", !!s.tec2);
  $("sw-fan").classList.toggle("on", !!s.fan);
  $("sw-mode").classList.toggle("on", !!s.mode);

  /* 报警横幅 */
  let msgs = [];
  for (const k in ALM_TXT) if (s.alarm & +k) msgs.push(ALM_TXT[k]);   /* +k：ALM_TXT 数字键 → 位值（ALM[k] 恒 undefined，横幅永不显示——WP-F.2 实测修复）*/
  const bar = $("alarmbar");
  if (msgs.length) { bar.textContent = "⚠ " + msgs.join("｜"); bar.classList.remove("hidden"); }
  else bar.classList.add("hidden");
  $("btn-ack").classList.toggle("hidden", !s.alarm);

  /* 协议版本黄条（不拒绝服务）*/
  if (s.ver !== PROTO_VER_EXPECT) {
    $("verbar").textContent = `固件协议 v${s.ver} ≠ 网页预期 v${PROTO_VER_EXPECT}，功能可能受限`;
    $("verbar").classList.remove("hidden");
  }
  pushCurve(s.body);
}

/* ====== 参数回显 ====== */
function onPv(m) {
  const src = m.src || "h743";
  const nowUs = Date.now();
  if (webAckWait && m.id === webAckWait.id) {
    /* clamp toast：0x90 ACK status=1 已由 ws 层转 pv src=web_ack 携带 clamped 值 */
    webAckWait = null;
    const el = sliderOf(m.id);
    if (el) el.value = m.v;
    const disp = valOf(m.id);
    if (disp) disp.textContent = fmtVal(m.id, m.v);
    return;
  }
  /* H743 侧（触摸/CLI）改的 → 同步控件 */
  const el = sliderOf(m.id);
  if (el && document.activeElement !== el) el.value = m.v;
  const disp = valOf(m.id);
  if (disp) disp.textContent = fmtVal(m.id, m.v);
  if (m.id === 6) $("sw-mode").classList.toggle("on", m.v !== 0);
}

/* ====== 注册表 → 控件 ====== */
function buildParams() {
  const norm = $("params"), adv = $("adv-params");
  norm.innerHTML = ""; adv.innerHTML = "";
  for (const p of registry) {
    if (!(p.flags & 0x04)) continue;              /* PF_WEB */
    const isAdv = !!(p.flags & 0x10);             /* PF_M207 → 高级区 */
    const row = document.createElement("div");
    row.className = "prow";
    const name = p.name || p.key;
    const isMode = p.id === 6;
    row.innerHTML =
      `<div class="pname"><span>${name}</span><span class="pval" data-v="${p.id}">${isMode ? "" : fmtVal(p.id, p.def)}</span></div>` +
      (isMode
        ? `<div class="stepper"><button data-cmd-off="${p.id}">手动</button><button data-cmd-on="${p.id}">自动</button></div>`
        : `<input type="range" data-id="${p.id}" min="${p.min}" max="${p.max}" step="${p.step}" value="${p.def ?? p.min}">` +
          `<div class="stepper"><button data-step="-">−</button><span class="step-num" data-v="${p.id}"></span><button data-step="+">＋</button></div>`);
    (isAdv ? adv : norm).appendChild(row);
  }
  built = true;
  bindParamEvents();
}
function sliderOf(id) { return document.querySelector(`input[data-id="${id}"]`); }
function valOf(id) { return document.querySelector(`.pval[data-v="${id}"]`); }
function fmtVal(id, v) {
  const p = registry.find((x) => x.id === id);
  if (id === 6) return v !== 0 ? "自动" : "手动";
  return p && p.unit ? `${(+v).toFixed(1)} ${p.unit}` : (+v).toFixed(1);
}
function curVal(id) {
  const el = sliderOf(id);
  return el ? +el.value : 0;
}
function doSet(id, v) {
  const p = registry.find((x) => x.id === id);
  /* F4 高温二次确认：设定值超 tc_set_confirm(0x07) → confirm 弹窗 */
  if ((id === 1 || id === 2) && registry.length) {
    const cf = registry.find((x) => x.id === 7);
    const thr = cf ? (cf.cur ?? cf.def ?? 40) : 40;
    if (v > thr && !confirm(`设定 ${v.toFixed(1)}°C 超过确认阈值 ${thr}°C，确认？`)) return;
  }
  webAckWait = { id, us: Date.now() };
  send({ t: "set", id, v });
}
function bindParamEvents() {
  document.querySelectorAll("input[data-id]").forEach((el) => {
    el.addEventListener("change", () => doSet(+el.dataset.id, +el.value));
  });
  document.querySelectorAll("button[data-step]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const id = +btn.parentElement.querySelector("input") ? +btn.parentElement.querySelector("input").dataset.id : 0;
      const p = registry.find((x) => x.id === id);
      if (!p) return;
      const dir = btn.dataset.step === "+" ? 1 : -1;
      let v = +(curVal(id) + dir * p.step).toFixed(3);
      v = Math.max(p.min, Math.min(p.max, v));
      const el = sliderOf(id); if (el) el.value = v;
      doSet(id, v);
    });
  });
  document.querySelectorAll("button[data-cmd-on]").forEach((b) =>
    b.addEventListener("click", () => doSet(6, 1)));
  document.querySelectorAll("button[data-cmd-off]").forEach((b) =>
    b.addEventListener("click", () => doSet(6, 0)));
}

/* ====== 开关/急停/确认恢复 ====== */
function bindButtons() {
  $("sw-tec1").onclick = () => send({ t: "cmd", c: $("sw-tec1").classList.contains("on") ? 2 : 1 });
  $("sw-tec2").onclick = () => send({ t: "cmd", c: $("sw-tec2").classList.contains("on") ? 4 : 3 });
  $("sw-fan").onclick = () => send({ t: "cmd", c: 5 });
  $("sw-mode").onclick = () => send({ t: "cmd", c: $("sw-mode").classList.contains("on") ? 7 : 6 });
  $("btn-ack").onclick = () => send({ t: "cmd", c: 9 });
  $("btn-estop").onclick = () => {
    /* 与屏同一 tcs_emergency_stop 语义：TEC 全停 + 风扇全开 + 手动 + ALM_ESTOP */
    if (confirm("确认急停？TEC 将全部停止，风扇全开")) send({ t: "cmd", c: 8 });
  };
}

/* ====== 仪表盘 + 曲线 ====== */
function drawGauge(cv, v, mn, mx) {
  const c = cv.getContext("2d"), w = cv.width, h = cv.height, cx = w / 2, cy = h / 2, r = w / 2 - 8;
  c.clearRect(0, 0, w, h);
  c.lineWidth = 10; c.lineCap = "round";
  c.strokeStyle = "#2a323d";
  c.beginPath(); c.arc(cx, cy, r, Math.PI * 0.75, Math.PI * 2.25); c.stroke();
  if (v != null && isFinite(v)) {
    const frac = Math.max(0, Math.min(1, (v - mn) / (mx - mn)));
    c.strokeStyle = "#4f8cff";
    c.beginPath(); c.arc(cx, cy, r, Math.PI * 0.75, Math.PI * (0.75 + 1.5 * frac)); c.stroke();
  }
}
function ensureCurve() {
  if (curve || !window.Chart) return;
  curve = new Chart($("curve"), {
    type: "line",
    data: { datasets: [{ label: "体温 °C", data: curvePts, borderColor: "#4f8cff",
      pointRadius: 0, tension: 0.25, fill: false }] },
    options: {
      animation: false, responsive: true,
      scales: {
        x: { type: "linear", ticks: { maxTicksLimit: 5, color: "#5a6472" }, grid: { color: "#232a34" } },
        y: { suggestedMin: 35, suggestedMax: 39, ticks: { color: "#5a6472" }, grid: { color: "#232a34" } },
      },
      plugins: { legend: { labels: { color: "#e8eaed" } } },
    },
  });
}
function pushCurve(body) {
  ensureCurve();
  if (!curve || body == null || !isFinite(body)) return;
  const now = Date.now() / 1000;
  if (now - curveLastT < 2) return;      /* 2s 采样 → 30min 900 点 */
  curveLastT = now;
  curvePts.push({ x: now, y: body });
  while (curvePts.length && now - curvePts[0].x > 1800) curvePts.shift();
  curve.update("none");
}

/* ====== 灰罩/Toast/心跳 ====== */
function overlay(show, count) {
  const ov = $("overlay");
  if (!show) { ov.classList.add("hidden"); return; }
  ov.classList.remove("hidden");
  $("ov-count").textContent = count || "--";
  if (count >= 6) { $("ov-title").textContent = "多次重连失败"; $("ov-sub").textContent = "请靠近设备后重试"; }
  else { $("ov-title").textContent = "连接断开"; $("ov-sub").innerHTML = `重连中 <span id="ov-count">${count}</span>s`; }
}
function toast(msg) {
  const t = $("toast");
  t.textContent = msg;
  t.classList.remove("hidden");
  clearTimeout(toast._h);
  toast._h = setTimeout(() => t.classList.add("hidden"), 2500);
}
function startUptime() {
  clearInterval(upTimer);
  upTimer = setInterval(() => {
    if (!lastStatus || !lastStatus.boot_s) return;
    const t = lastStatus.boot_s;
    $("uptime").textContent = `${String(Math.floor(t / 3600)).padStart(2, "0")}:${String(Math.floor(t / 60) % 60).padStart(2, "0")}:${String(t % 60).padStart(2, "0")}`;
  }, 1000);
}

/* HTTP 兜底：WS 起不来时先拉一次快照/注册表 */
async function httpBootstrap() {
  try {
    const st = await (await fetch("/api/status")).json();
    onStatus(st);
    const rg = await (await fetch("/api/registry")).json();
    registry = rg.list || rg || [];
    buildParams();
  } catch (e) { /* AP 未就绪时静默 */ }
}

bindButtons();
startUptime();
httpBootstrap();
wsConnect();
setInterval(() => { if (!wsAlive) overlay(true, retry); }, 1000);
