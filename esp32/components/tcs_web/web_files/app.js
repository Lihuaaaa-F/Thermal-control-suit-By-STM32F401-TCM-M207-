/* app.js — 调温服网页逻辑（任务书 §4.3）
 * WS: 收 {"t":"st",...}(500ms) / {"t":"pv","id","v","src"}；发 {"t":"set",...}/{"t":"cmd","c"}
 * 注册表驱动参数区（PF_WEB），M207 参数自动进高级区（D6：屏不显，网页显示）。
 * 断连灰罩+重连倒计时；多次失败「请靠近设备」。急停=红色大按钮+二次确认。
 */
"use strict";
const $ = (id) => document.getElementById(id);
const ALM = { BODY: 1, M207: 2, ESTOP: 4, OVERTEMP: 8, EEPROM: 16 };
/* K10 报警文案表（与屏端 alarm_pick 同源：主句+怎么办）*/
const ALM_TXT = {
  1: "体温传感器失联 · 检查接头后确认恢复",
  2: "M207 通信失联 · 检查线缆后确认恢复",
  4: "急停已触发 · 确认恢复后重新运行",
  8: "水温过高 · 已停止加热，等待降温",
  16: "配置校验失败 · 已恢复默认参数，请重新设定",
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
    else if (m.t === "ack") onCmdAck(m);
  };
}

/* R1 P1-03：命令回执。服务端对 cmd 1–7/10 返回 {t:"ack",c,rv}，
 * rv<0 表示被拒/未实现——前端必须显示失败，不能像以前那样静默当成功。*/
function onCmdAck(m) {
  const names = { 1: "TC1 开", 2: "TC1 关", 3: "TC2 开", 4: "TC2 关",
                  5: "风扇切换", 6: "自动", 7: "手动", 8: "急停", 9: "确认",
                  10: "保存到模块" };
  const name = names[m.c] || ("cmd " + m.c);
  const ok = (m.rv === undefined) || (m.rv >= 0);
  let msg = name + (ok ? "：成功" : "：失败(rv=" + m.rv + ")");
  if (m.c === 10 && ok) msg = "保存到模块：已下发 " + m.rv + " 条（无逐条确认）";
  toast(msg);
}
/* H2 重连加固：指数退避 1,2,4,8,10,10…s（上限 10s）；
 * 连败 RELOAD_AFTER 次(~95s)自动整页刷新——清前端 JS 态（F-1 待决清账：
 * WS 卡死类停摆刷新即恢复；AP 整体离线时刷新失败由用户恢复，属物理边界）*/
const RETRY_CAP_S = 10;
const RELOAD_AFTER = 12;
function scheduleRetry() {
  overlay(true, ++retry);
  if (retry >= RELOAD_AFTER) {
    $("conn").textContent = "自动刷新…";
    location.reload();
    return;
  }
  const wait = Math.min(RETRY_CAP_S, 1 << Math.min(retry - 1, 4));
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
/* WP-P3 高级区分组（H10 id 段 → 功能组；与规格说明书 v1.2 §2.5 对齐）*/
const ADV_GROUPS = [
  { lo: 0x100, hi: 0x102, title: "PID（贴身 TC1）" },
  { lo: 0x110, hi: 0x112, title: "PID（环境 TC2）" },
  { lo: 0x103, hi: 0x10A, title: "保护（TC1）" },
  { lo: 0x113, hi: 0x11A, title: "保护（TC2）" },
  { lo: 0x120, hi: 0x122, title: "保护（PCB）" },
  { lo: 0x10B, hi: 0x10F, title: "输出/温度选项（TC1）" },
  { lo: 0x11B, hi: 0x11F, title: "输出/温度选项（TC2）" },
  { lo: 0x123, hi: 0x12F, title: "系统" },
  { lo: 0x130, hi: 0x1FF, title: "遥测（只读）" },
];
function advGroupOf(id) {
  for (const g of ADV_GROUPS) if (id >= g.lo && id <= g.hi) return g.title;
  return "其它";
}
function buildParams() {
  const norm = $("params"), adv = $("adv-params");
  norm.innerHTML = ""; adv.innerHTML = "";
  const groups = new Map();
  for (const p of registry) {
    if (!(p.flags & 0x04)) continue;              /* PF_WEB */
    const isAdv = p.id >= 0x100;                  /* M207 段=H10 0x100+（PF_M207 语义是"需下发"非分区，tc1/tc2_set 亦带该位）*/
    const row = document.createElement("div");
    row.className = "prow";
    if (isAdv && (p.flags & 0x20)) row.dataset.danger = "1";   /* PF_DANGER → 二次确认 */
    const name = p.name || p.key;
    const isMode = p.id === 6;
    const ro = isAdv && !(p.flags & 0x01);        /* 高级区无 PF_RW → 只读遥测 */
    row.innerHTML =
      `<div class="pname"><span>${name}</span><span class="pval" data-v="${p.id}">${isMode ? "" : fmtVal(p.id, p.def)}</span></div>` +
      (isMode
        ? `<div class="stepper"><button data-cmd-off="${p.id}">手动</button><button data-cmd-on="${p.id}">自动</button></div>`
        : ro
          ? `<div class="ro-hint">只读 · 由模块周期上报</div>`
          : `<input type="range" data-id="${p.id}" min="${p.min}" max="${p.max}" step="${p.step}" value="${p.def ?? p.min}">` +
            `<div class="stepper"><button data-step="-">−</button><span class="step-num" data-v="${p.id}"></span><button data-step="+">＋</button></div>`);
    if (isAdv) {
      const g = advGroupOf(p.id);
      if (!groups.has(g)) {
        const card = document.createElement("div");
        card.className = "adv-group";
        card.dataset.group = g;
        card.innerHTML = `<div class="adv-gtitle">${g}</div>`;
        adv.appendChild(card);
        groups.set(g, card);
      }
      groups.get(g).appendChild(row);
    } else {
      norm.appendChild(row);
    }
  }
  built = true;
  bindParamEvents();
}
function sliderOf(id) { return document.querySelector(`input[data-id="${id}"]`); }
function valOf(id) { return document.querySelector(`.pval[data-v="${id}"]`); }
function fmtVal(id, v) {
  const p = registry.find((x) => x.id === id);
  if (id === 6) return v !== 0 ? "自动" : "手动";
  if (id === 8) return v == 1 ? "强制布防" : v == 2 ? "强制不布防" : "自动";
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
  /* WP-P3 危险参数二次确认（PF_DANGER=0x20，H10 ✓ 列：保护/输出上限类）*/
  if (p && (p.flags & 0x20) &&
      !confirm(`「${p.name || p.key}」为保护/输出限制类参数，误改可能影响设备安全。确认改为 ${v}？`)) return;
  /* P3 客户端钳制（纵深防御；固件 clamp 仍为权威）*/
  if (p) v = Math.min(p.max, Math.max(p.min, v));
  webAckWait = { id, us: Date.now() };
  send({ t: "set", id, v });
}
function bindParamEvents() {
  document.querySelectorAll("input[data-id]").forEach((el) => {
    el.addEventListener("change", () => doSet(+el.dataset.id, +el.value));
  });
  document.querySelectorAll("button[data-step]").forEach((btn) => {
    btn.addEventListener("click", () => {
      /* H7 修复：input 与 .stepper 是兄弟（都在 .prow 下），
       * 原 parentElement 查询和 `+querySelector` NaN 双重失效，步进从未生效 */
      const inp = btn.closest(".prow") && btn.closest(".prow").querySelector("input");
      if (!inp) return;
      const id = +inp.dataset.id;
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
  $("btn-csv").onclick = exportCsv;
  $("btn-m207-save").onclick = () => {
    /* WP-P3 保存语义：仅"当前会话已下发"的值，保存后掉电保持；
     * 未保存参数掉电后回模块上次保存值（提示文案见 index.html）。 */
    if (confirm("保存全部高级参数到模块？保存后掉电保持。")) {
      send({ t: "cmd", c: 10 });   /* TCS_CMD_M207_SAVE（R5 接 M207 `!`）*/
      toast("已请求保存到模块");
    }
  };
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
  c.strokeStyle = "#D8DEE6";
  c.beginPath(); c.arc(cx, cy, r, Math.PI * 0.75, Math.PI * 2.25); c.stroke();
  if (v != null && isFinite(v)) {
    const frac = Math.max(0, Math.min(1, (v - mn) / (mx - mn)));
    c.strokeStyle = "#2563EB";
    c.beginPath(); c.arc(cx, cy, r, Math.PI * 0.75, Math.PI * (0.75 + 1.5 * frac)); c.stroke();
  }
}
function ensureCurve() {
  if (curve || !window.Chart) return;
  curve = new Chart($("curve"), {
    type: "line",
    data: { datasets: [{ label: "体温 °C", data: curvePts, borderColor: "#2563EB",
      pointRadius: 0, tension: 0.25, fill: false }] },
    options: {
      animation: false, responsive: true,
      scales: {
        x: { type: "linear", ticks: { maxTicksLimit: 5, color: "#5F6B7A", 
          callback: (v) => { const d = new Date(v * 1000); return `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`; } }, grid: { color: "#E5E7EB" } },
        y: { suggestedMin: 35, suggestedMax: 39, ticks: { color: "#5F6B7A" }, grid: { color: "#E5E7EB" } },
      },
      plugins: { legend: { labels: { color: "#1F2937" } } },
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

/* ====== H7：CSV 导出（曲线原始采样 → tcs_body_log.csv）====== */
function exportCsv() {
  const rows = ["unix_time_s,body_temp_c"];
  for (const p of curvePts) rows.push(`${p.x.toFixed(1)},${p.y.toFixed(2)}`);
  const blob = new Blob([rows.join("\n") + "\n"], { type: "text/csv" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = "tcs_body_log.csv";
  a.click();
  URL.revokeObjectURL(a.href);
  toast(`已导出 ${curvePts.length} 行`);
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
