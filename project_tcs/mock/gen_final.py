# -*- coding: utf-8 -*-
"""gen_final.py — K0 终版全页面 mock 生成器（任务书 v9.5 K0，token=C 仪表）

定稿 token（三眼评审 1/2 眼通过，3 眼=K7 用户异步）：
  bg #EEF1F5 | 卡 #F2F3F5 描边 #D8DEE6 底饰线 2px | 字 #1F2937/#6B7280 hero #111827
  蓝 #2563EB=交互(按钮边/选中/拖动) | 绿 #16A34A | 灰 #9CA3AF | 黄 #D97706 | 红 #DC2626
  横幅 #FEE2E2/#991B1B 与 #FEF3C7/#92400E | 按住 #DBEAFE
布局=第五章像素级（与固件 ui_layout.h 逐值对应）；8px 网格；字号 96/48/32/24/20。
页面: home(?state=normal|alarm|safe|lost|badge|press) tune(?screen=1|2&sel=0..3&drag=1)
      connect + dlg_estop/dlg_mode/dlg_set/dlg_reset
用法: python mock/gen_final.py
"""
import pathlib

OUT = pathlib.Path(__file__).resolve().parent / "final"

CSS = """
* { margin:0; padding:0; box-sizing:border-box; }
html,body { width:1024px; height:600px; overflow:hidden;
  font-family:"Microsoft YaHei","PingFang SC",sans-serif; -webkit-font-smoothing:antialiased; }
:root {
  --bg:#EEF1F5; --card:#F2F3F5; --border:#D8DEE6; --cline:#D8DEE6;
  --tx:#1F2937; --tx2:#5F6B7A; --hero:#111827; --white:#FFFFFF;
  --blue:#2563EB; --press:#DBEAFE; --green:#16A34A; --gray:#9CA3AF;
  --yellow:#D97706; --red:#DC2626; --red-dk:#B91C1C;
  --alm-bg:#FEE2E2; --alm-tx:#991B1B; --warn-bg:#FEF3C7; --warn-tx:#92400E;
  --track:#D8DEE6; --track-fill:#BFDBFE;
}
.abs { position:absolute; }
/* ============ 顶栏 56px ============ */
.mode-pill { left:24px; top:16px; width:160px; height:40px; background:var(--white);
  border:1px solid var(--border); border-radius:20px;
  display:flex; align-items:center; justify-content:center; gap:8px;
  font-size:24px; color:var(--tx); }
.mode-dot { width:8px; height:8px; border-radius:4px; background:var(--green); }
.mode-dot.man { background:var(--gray); }
.banner { left:208px; top:8px; width:552px; height:56px; border-radius:12px;
  display:flex; align-items:center; padding:0 16px; gap:12px; }
.banner.red { background:var(--alm-bg); color:var(--alm-tx); }
.banner.yellow { background:var(--warn-bg); color:var(--warn-tx); }
.b-ic { font-size:24px; } .b-tx { line-height:1.15; }
.b-msg { font-size:24px; font-weight:600; } .b-act { font-size:24px; opacity:.85; line-height:1.0; }
.b-count { margin-left:auto; font-size:20px; border:1px solid currentColor;
  border-radius:10px; padding:2px 8px; opacity:.8; }
.link { display:flex; align-items:center; gap:6px; font-size:20px; color:var(--tx2); top:18px; }
.ldot { width:8px; height:8px; border-radius:4px; }
.estop-ic { left:940px; top:20px; width:24px; height:24px; }
/* ============ 主页卡片 ============ */
.hero-card { left:32px; top:88px; width:460px; height:240px; background:var(--card);
  border:1px solid var(--border); border-bottom:2px solid var(--cline); border-radius:8px; }
.hero-card.r { left:532px; }
.card-title { position:absolute; left:24px; top:20px; font-size:32px; color:var(--tx); }
.tec-pill { position:absolute; right:24px; top:24px; width:88px; height:40px; border-radius:20px;
  display:flex; align-items:center; justify-content:center; gap:6px;
  font-size:24px; color:var(--white); }
.tec-pill.on { background:var(--green); } .tec-pill.off { background:var(--gray); }
.hero-num { position:absolute; left:24px; top:72px; height:96px; font-size:96px;
  line-height:96px; color:var(--hero); font-weight:600; display:flex; align-items:baseline; }
.hero-num .unit { font-size:24px; color:var(--tx2); margin-left:8px; }
.hero-num.lostv, .lostv { color:var(--gray); letter-spacing:2px; }
.fresh { position:absolute; right:32px; top:80px; width:8px; height:8px; border-radius:4px;
  background:var(--green); }
.fresh.stale { background:var(--gray); }
.set-line { position:absolute; left:24px; top:184px; font-size:24px; color:var(--tx2); }
.set-line b { color:var(--tx); font-weight:600; }
.sub-card { top:352px; width:460px; height:96px; background:var(--card);
  border:1px solid var(--border); border-bottom:2px solid var(--cline); border-radius:8px; }
.sub-card.l { left:32px; } .sub-card.r { left:532px; }
.sub-title { position:absolute; left:24px; top:12px; font-size:24px; color:var(--tx); }
.hero48 { position:absolute; left:120px; top:20px; font-size:48px; line-height:56px;
  color:var(--hero); font-weight:600; display:flex; align-items:baseline; }
.hero48 .unit { font-size:20px; color:var(--tx2); margin-left:6px; }
.fresh2 { position:absolute; left:296px; top:40px; width:8px; height:8px; border-radius:4px;
  background:var(--green); }
.fresh2.stale { background:var(--gray); }
.lost-mark { position:absolute; right:24px; top:28px; width:88px; height:40px; border-radius:20px;
  background:var(--warn-bg); color:var(--warn-tx); font-size:24px;
  display:flex; align-items:center; justify-content:center; }
.fan-label { position:absolute; left:24px; top:50px; font-size:24px; color:var(--tx2); }
.fan-track { position:absolute; right:24px; top:16px; width:128px; height:64px;
  border-radius:32px; background:var(--gray); }
.fan-track.on { background:var(--green); }
.fan-ball { position:absolute; top:0; width:64px; height:64px; border-radius:32px;
  background:var(--white); box-shadow:0 1px 4px rgba(0,0,0,.25);
  display:flex; align-items:center; justify-content:center; font-size:24px; color:var(--tx); }
/* ============ 底栏 ============ */
.dock-btn { top:472px; height:96px; background:var(--white);
  border:1px solid var(--blue); border-radius:8px;
  display:flex; align-items:center; justify-content:center; font-size:32px; color:var(--blue); }
.dock-btn.dim { color:var(--gray); border-color:var(--border); }
.dock-btn.press { background:var(--press); }
#b-tune { left:40px; width:208px; } #b-mode { left:272px; width:208px; }
#b-conn { left:504px; width:208px; }
#b-estop { left:768px; top:472px; width:216px; height:112px; background:var(--red);
  border-color:var(--red); color:var(--white); font-weight:600; }
#b-estop.press { background:var(--red-dk); }
/* ============ POST 角标 ============ */
.badge { left:128px; top:0; width:640px; height:40px; background:var(--white);
  border:1px solid var(--border); border-top:none; border-radius:0 0 8px 8px;
  display:flex; align-items:center; justify-content:center; gap:24px;
  font-size:20px; color:var(--tx); }
.badge .ok { color:var(--green); } .badge .ng { color:var(--warn-tx); }
/* ============ 调参页 ============ */
.back-btn { left:24px; top:8px; width:120px; height:40px; background:var(--white);
  border:1px solid var(--border); border-radius:8px; display:flex; align-items:center;
  justify-content:center; font-size:24px; color:var(--tx); }
.ctx-line { left:176px; top:22px; font-size:24px; color:var(--tx2); }
.clock { right:32px; top:24px; font-size:20px; color:var(--tx2);
  font-variant-numeric:tabular-nums; }
.prow { left:32px; width:960px; height:88px; background:var(--card);
  border:1px solid var(--border); border-bottom:2px solid var(--cline); border-radius:8px; }
.prow.sel { background:var(--press); border-color:var(--blue); }
.prow.sel::before { content:""; position:absolute; left:-1px; top:-1px; width:4px;
  height:90px; background:var(--blue); border-radius:8px 0 0 8px; }
.p-name { position:absolute; left:24px; top:12px; font-size:24px; color:var(--tx); }
.p-val { position:absolute; left:24px; top:46px; font-size:32px; line-height:32px;
  color:var(--hero); font-weight:600; }
.p-val .unit { font-size:20px; color:var(--tx2); font-weight:400; margin-left:4px; }
/* 左文字列(24..280) + 右滑条(320..752)：80px 拇指球任意位置不压文字 */
.track { position:absolute; left:320px; top:36px; width:432px; height:16px;
  border-radius:8px; background:var(--track); }
.fill { position:absolute; left:0; top:0; height:16px; border-radius:8px;
  background:var(--track-fill); }
.thumb { position:absolute; top:-32px; width:80px; height:80px; border-radius:40px;
  background:var(--blue); box-shadow:0 2px 6px rgba(37,99,235,.35);
  display:flex; align-items:center; justify-content:center;
  font-size:24px; color:var(--white); font-weight:600; }
.thumb.rest { background:var(--gray); box-shadow:0 2px 6px rgba(0,0,0,.25); }
/* 蓝=选中/拖动（§0b）：选中行拇指蓝，非选中行灰静态预览；拖动时蓝底内嵌实时值 */
.tip-line { position:absolute; left:56px; top:404px; font-size:24px; color:var(--tx2); }
.mini-key { position:absolute; top:12px; width:64px; height:64px; background:var(--white);
  border:1px solid var(--border); border-radius:8px; display:flex; align-items:center;
  justify-content:center; font-size:32px; color:var(--tx); }
.mk-minus { right:104px; } .mk-plus { right:24px; }
/* 底部区 */
.zone-btn { top:472px; height:96px; background:var(--white); border:1px solid var(--blue);
  border-radius:8px; display:flex; align-items:center; justify-content:center;
  font-size:48px; color:var(--blue); }
#z-minus { left:32px; width:96px; } #z-plus { left:896px; width:96px; }
.half-btn { top:488px; height:64px; background:var(--white); border:1px solid var(--blue);
  border-radius:8px; display:flex; align-items:center; justify-content:center;
  font-size:24px; color:var(--blue); }
.half-btn.ghost { border-color:var(--border); color:var(--tx2); }
#z-step { left:160px; width:176px; }
#z-next { left:360px; width:240px; }
#z-prev { left:360px; width:200px; }
#z-reset { left:584px; width:240px; }
/* ============ 连接页 ============ */
.qr-box { left:392px; top:88px; width:240px; height:240px; background:var(--white);
  border:1px solid var(--border); border-radius:8px; padding:20px; }
.qr-core { width:200px; height:200px;
  background:conic-gradient(var(--tx) 25%, var(--white) 0 50%, var(--tx) 0 75%, var(--white) 0);
  background-size:16px 16px; position:relative; }
.finder { position:absolute; width:56px; height:56px; background:var(--white);
  border:16px solid var(--tx); }
.f1 { left:0; top:0; } .f2 { right:0; top:0; } .f3 { left:0; bottom:0; }
.cred { left:0; width:1024px; text-align:center; font-size:20px; color:var(--tx2); }
.cred b { color:var(--tx); font-weight:600; font-family:Consolas,monospace; }
.cred .cl { font-size:24px; color:var(--tx); margin-right:8px; }
.fw-ver { right:32px; top:520px; font-size:20px; color:var(--tx2); }
/* ============ 弹窗 ============ */
.mask { left:0; top:0; width:1024px; height:600px;
  background-image:radial-gradient(var(--border) 1px, transparent 1px);
  background-size:3px 3px; }
.dlg { left:256px; top:136px; width:512px; height:320px; background:var(--white);
  border:1px solid var(--border); border-bottom:2px solid var(--cline); border-radius:12px; }
.dlg-title { position:absolute; left:40px; top:32px; font-size:32px; font-weight:600; }
.dlg-title.red { color:var(--red); }
.dlg-body { position:absolute; left:40px; top:96px; font-size:24px; color:var(--tx); line-height:1.4; }
.dlg-btn { position:absolute; top:208px; width:192px; height:88px; border-radius:8px;
  display:flex; align-items:center; justify-content:center; font-size:24px; }
.dlg-ok { left:40px; color:var(--white); font-weight:600; }
.dlg-ok.red { background:var(--red); border:1px solid var(--red); }
.dlg-ok.blue { background:var(--blue); border:1px solid var(--blue); }
.dlg-cancel { left:280px; background:var(--white); border:1px solid var(--blue);
  color:var(--blue); }
.hidden { display:none !important; }
"""

BASE_JS = """
const q = new URLSearchParams(location.search);
const $ = (id) => document.getElementById(id);
"""

HOME_BODY = """
<div class="screen">
  <div class="abs mode-pill"><span class="mode-dot" id="mdot"></span><span id="mtxt">自动</span></div>
  <div class="abs banner yellow hidden" id="bn-y">
    <span class="b-ic">▲</span>
    <span class="b-tx"><div class="b-msg" id="by-msg">M207 通信失联</div>
    <div class="b-act" id="by-act">检查线缆接头 · 点击横幅确认恢复</div></span>
    <span class="b-count hidden" id="by-count">还有 1 条</span>
  </div>
  <div class="abs banner red hidden" id="bn-r">
    <span class="b-ic">■</span>
    <span class="b-tx"><div class="b-msg">急停已触发</div>
    <div class="b-act">已断电制动 · 点击横幅确认恢复</div></span>
    <span class="b-count hidden" id="br-count">还有 1 条</span>
  </div>
  <div class="abs link" style="left:776px" id="links">
    <span class="ldot" id="ld-m207" style="background:var(--green)"></span>M207
    <span class="ldot" id="ld-esp" style="background:var(--green);margin-left:8px"></span>ESP
  </div>
  <svg class="abs estop-ic" id="eic" viewBox="0 0 24 24"></svg>

  <div class="abs hero-card">
    <div class="card-title">贴身水箱</div>
    <div class="tec-pill off" id="tp1">○ 停止</div>
    <div class="fresh" id="fr1"></div>
    <div class="hero-num"><span id="t1">24.8</span><span class="unit">°C</span></div>
    <div class="set-line">设定 <b id="s1">25.0</b> °C</div>
  </div>
  <div class="abs hero-card r">
    <div class="card-title">环境水箱</div>
    <div class="tec-pill off" id="tp2">○ 停止</div>
    <div class="fresh" id="fr2"></div>
    <div class="hero-num"><span id="t2">25.2</span><span class="unit">°C</span></div>
    <div class="set-line">设定 <b id="s2">25.0</b> °C</div>
  </div>
  <div class="abs sub-card l">
    <div class="sub-title">体温</div>
    <div class="fresh2" id="frb"></div>
    <div class="hero48"><span id="tb">36.8</span><span class="unit">°C</span></div>
    <div class="lost-mark hidden" id="bm-lost">▲ 失联</div>
  </div>
  <div class="abs sub-card r">
    <div class="sub-title">风扇</div>
    <div class="fan-label" id="fl">运行中</div>
    <div class="fan-track on" id="ft"><div class="fan-ball" id="fb" style="left:64px">开</div></div>
  </div>

  <div class="abs dock-btn" id="b-tune">调参</div>
  <div class="abs dock-btn" id="b-mode">切手动</div>
  <div class="abs dock-btn" id="b-conn">连接</div>
  <div class="abs dock-btn" id="b-estop">急停</div>
  <div class="abs badge hidden" id="badge">
    <span class="ok">✓ TMP117</span><span class="ok">✓ M207</span><span class="ok">✓ EEPR</span>
    <span class="ok">✓ TABLE</span><span class="ng">✗ SCR</span><span class="ok">✓ VER</span>
  </div>
</div>
<script>
""" + BASE_JS + """
const st = q.get("state") || "normal";
function eic(active) {
  $("eic").innerHTML = active
    ? '<circle cx="12" cy="12" r="10" fill="#DC2626"/><rect x="8" y="8" width="8" height="8" fill="#fff"/>'
    : '<circle cx="12" cy="12" r="10" fill="none" stroke="#9CA3AF" stroke-width="2"/><rect x="8" y="8" width="8" height="8" fill="#9CA3AF"/>';
}
eic(false);
function grayHero(ids) { for (const id of ids) { $(id).textContent = "--.-"; $(id).classList.add("lostv"); } }
if (st === "alarm") {          // 黄警告：M207 失联 + 体温失联灰显；链路点并入横幅（隐藏）
  $("bn-y").classList.remove("hidden"); $("links").classList.add("hidden");
  $("ld-m207").style.background = "var(--gray)";
  grayHero(["tb"]); $("frb").classList.add("stale");
  $("bm-lost").classList.remove("hidden");
} else if (st === "alarm2") {  // 黄警告×2：横幅主条 + 计数角标
  $("bn-y").classList.remove("hidden"); $("links").classList.add("hidden");
  $("by-msg").textContent = "EEPROM 配置校验失败";
  $("by-act").textContent = "已恢复默认参数 · 请重新设定";
  $("by-count").classList.remove("hidden");
} else if (st === "safe") {    // 红 SAFE：急停
  $("bn-r").classList.remove("hidden"); $("links").classList.add("hidden");
  eic(true);
} else if (st === "safe2") {   // 红 SAFE + 次级计数
  $("bn-r").classList.remove("hidden"); $("links").classList.add("hidden");
  $("br-count").classList.remove("hidden"); eic(true);
} else if (st === "lost") {    // 失联灰显（无横幅：提示性）
  $("ld-m207").style.background = "var(--gray)";
  grayHero(["t1","t2"]); $("fr1").classList.add("stale"); $("fr2").classList.add("stale");
} else if (st === "badge") {
  $("badge").classList.remove("hidden");
} else if (st === "press") {   // 按住态（触摸反馈 <100ms）
  $("b-tune").classList.add("press");
} else if (st === "run") {     // 运行态：TEC1 运行
  $("tp1").className = "tec-pill on"; $("tp1").textContent = "● 运行";
}
</script>
</body></html>
"""

TUNE_BODY = """
<div class="screen">
  <div class="abs back-btn">‹ 返回</div>
  <div class="abs ctx-line" id="ctx">选中：贴身水箱设定温度 · 范围 5.0 ~ 45.0 °C</div>
  <div class="abs clock">00:57:12</div>
  <div id="rows"></div>
  <div class="abs zone-btn" id="z-minus">−</div>
  <div class="abs zone-btn" id="z-plus">＋</div>
  <div class="abs half-btn" id="z-step">步进 0.1</div>
  <div class="abs half-btn" id="z-next">下一页 ▸</div>
  <div class="abs half-btn hidden" id="z-prev">◂ 上一页</div>
  <div class="abs half-btn ghost hidden" id="z-reset">恢复默认</div>
</div>
<script>
""" + BASE_JS + """
const SCR1 = [
  { n:"贴身水箱设定温度", v:"25.0", u:"°C", frac:0.50 },
  { n:"环境水箱设定温度", v:"25.0", u:"°C", frac:0.50 },
  { n:"体温过热阈值",     v:"37.5", u:"°C", frac:0.375 },
  { n:"体温过冷阈值",     v:"36.5", u:"°C", frac:0.375 },
];
const SCR2 = [
  { n:"滞回回差",   v:"0.2", u:"°C", frac:0.111 },
  { n:"控制模式",   v:"自动", u:"",  frac:1.0 },
  { n:"布防模式",   v:"强制布防", u:"", frac:1.0 },
];
const scr = q.get("screen") || "1";
const sel = parseInt(q.get("sel") || "0");
const drag = q.get("drag") === "1";
const items = scr === "1" ? SCR1 : SCR2;
const ys = [56, 160, 264, 368];
$("ctx").textContent = `选中：${items[sel].n} · 范围 ${scr === "1" ? (sel < 2 ? "5.0 ~ 45.0" : sel === 2 ? "36.0 ~ 40.0" : "35.0 ~ 39.0") : (sel === 0 ? "0.1 ~ 1.0 °C" : sel === 1 ? "手动 / 自动" : "自动 / 强制布防 / 强制不布防")}${scr === "1" ? " °C" : ""}`;
$("z-prev").classList.toggle("hidden", scr === "1");
$("z-next").classList.toggle("hidden", scr === "2");
$("z-reset").classList.toggle("hidden", scr === "1");
if (scr === "2") document.getElementById("rows").insertAdjacentHTML("beforeend",
  '<div class="tip-line">拖动滑条或点 ± 调整选中参数 · 实时生效 · 断电自动保持</div>');
const R = document.getElementById("rows");
items.forEach((it, i) => {
  const y = ys[i];
  const thumbX = Math.round(it.frac * (432 - 80));
  const fillW = Math.round(it.frac * 432);
  R.insertAdjacentHTML("beforeend", `
  <div class="abs prow ${i === sel ? "sel" : ""}" style="top:${y}px">
    <div class="p-name">${it.n}</div>
    <div class="p-val">${it.v}${it.u ? `<span class="unit">${it.u}</span>` : ""}</div>
    <div class="track"><div class="fill" style="width:${fillW}px"></div>
      <div class="thumb ${i === sel ? "" : "rest"}" style="left:${thumbX}px">${i === sel && drag ? it.v : ""}</div>
    </div>
    <div class="mini-key mk-minus">−</div>
    <div class="mini-key mk-plus">＋</div>
  </div>`);
});
</script>
</body></html>
"""

CONNECT_BODY = """
<div class="screen">
  <div class="abs card-title" style="left:32px;top:16px">连接</div>
  <div class="abs qr-box"><div class="qr-core">
    <div class="finder f1"></div><div class="finder f2"></div><div class="finder f3"></div>
  </div></div>
  <div class="abs cred" style="top:352px"><span class="cl">热点名称</span><b>TCS-3C71</b></div>
  <div class="abs cred" style="top:392px"><span class="cl">热点密码</span><b>12345678</b></div>
  <div class="abs cred" style="top:432px"><span class="cl">控制页</span><b>http://192.168.4.1</b></div>
  <div class="abs dock-btn" style="left:40px;width:208px" id="bk">‹ 返回</div>
  <div class="abs fw-ver">fw v1.1.0</div>
</div>
</body></html>
"""

DLG_TPL = """
<div class="screen" style="%(under)s">
%(snapshot)s
</div>
<div class="abs mask"></div>
<div class="abs dlg">
  <div class="dlg-title%(redc)s">%(title)s</div>
  <div class="dlg-body">%(body)s</div>
  <div class="dlg-btn dlg-ok %(okc)s">%(ok)s</div>
  <div class="dlg-btn dlg-cancel">%(cancel)s</div>
</div>
</body></html>
"""

HOME_SNAPSHOT = """
  <div class="abs mode-pill"><span class="mode-dot"></span>自动</div>
  <div class="abs link" style="left:776px">
    <span class="ldot" style="background:var(--green)"></span>M207
    <span class="ldot" style="background:var(--green);margin-left:8px"></span>ESP
  </div>
  <svg class="abs estop-ic" viewBox="0 0 24 24">
    <circle cx="12" cy="12" r="10" fill="none" stroke="#9CA3AF" stroke-width="2"/>
    <rect x="8" y="8" width="8" height="8" fill="#9CA3AF"/></svg>
  <div class="abs hero-card"><div class="card-title">贴身水箱</div>
    <div class="tec-pill off">○ 停止</div><div class="fresh"></div>
    <div class="hero-num"><span>24.8</span><span class="unit">°C</span></div>
    <div class="set-line">设定 <b>25.0</b> °C</div></div>
  <div class="abs hero-card r"><div class="card-title">环境水箱</div>
    <div class="tec-pill off">○ 停止</div><div class="fresh"></div>
    <div class="hero-num"><span>25.2</span><span class="unit">°C</span></div>
    <div class="set-line">设定 <b>25.0</b> °C</div></div>
  <div class="abs sub-card l"><div class="sub-title">体温</div><div class="fresh2"></div>
    <div class="hero48"><span>36.8</span><span class="unit">°C</span></div></div>
  <div class="abs sub-card r"><div class="sub-title">风扇</div>
    <div class="fan-label">运行中</div>
    <div class="fan-track on"><div class="fan-ball" style="left:64px">开</div></div></div>
  <div class="abs dock-btn" style="left:40px;width:208px">调参</div>
  <div class="abs dock-btn" style="left:272px;width:208px">切手动</div>
  <div class="abs dock-btn" style="left:504px;width:208px">连接</div>
  <div class="abs dock-btn" style="left:768px;width:216px;height:112px;background:var(--red);border-color:var(--red);color:var(--white);font-weight:600">急停</div>
"""

TUNE_SNAPSHOT = """
  <div class="abs back-btn">‹ 返回</div>
  <div class="abs ctx-line">选中：贴身水箱设定温度 · 范围 5.0 ~ 45.0 °C</div>
  <div class="abs clock">00:57:12</div>
""" + "".join(
    f"""
  <div class="abs prow{" sel" if i == 0 else ""}" style="top:{y}px">
    <div class="p-name">{n}</div><div class="p-val">{v}<span class="unit">°C</span></div>
    <div class="track"><div class="fill" style="width:{w}px"></div>
      <div class="thumb{" rest" if i else ""}" style="left:{tx}px"></div></div>
    <div class="mini-key mk-minus">−</div><div class="mini-key mk-plus">＋</div>
  </div>"""
    for i, (n, v, frac) in enumerate([
        ("贴身水箱设定温度", "25.0", 0.50), ("环境水箱设定温度", "25.0", 0.50),
        ("体温过热阈值", "37.5", 0.375), ("体温过冷阈值", "36.5", 0.375)])
    for y, w, tx in [(56 + i * 104, round(frac * 432), round(frac * (432 - 80)))]
)

DLGS = {
    "dlg_estop": ("确认急停？", "TEC 将全部停止，风扇全开。", "确认急停", "取消", "red", "red", HOME_SNAPSHOT),
    "dlg_mode": ("切换到自动模式？", "自动模式按体温闭环控制制冷。", "确认切换", "取消", "blue", "", HOME_SNAPSHOT),
    "dlg_set": ("设定 42.0 °C 超过确认阈值", "高温设定需二次确认，确认后本会话放行。", "确认设定", "取消", "red", "red", TUNE_SNAPSHOT),
    "dlg_reset": ("恢复默认参数？", "全部参数将回到出厂值，立即生效。", "恢复默认", "取消", "red", "red", TUNE_SNAPSHOT),
}

def page(name, body):
    html = ('<!DOCTYPE html>\n<html lang="zh"><head><meta charset="utf-8">\n'
            f'<title>{name}</title>\n<style>{CSS}</style></head>\n<body>\n{body}')
    (OUT / f"{name}.html").write_text(html, encoding="utf-8")
    print("OK", OUT / f"{name}.html")

def main():
    OUT.mkdir(exist_ok=True)
    page("home", HOME_BODY)
    page("tune", TUNE_BODY)
    page("connect", CONNECT_BODY)
    for name, (title, body, ok, cancel, okc, redc, snap) in DLGS.items():
        page(name, DLG_TPL % {"under": "", "snapshot": snap, "title": title,
                              "body": body, "ok": ok, "cancel": cancel,
                              "okc": okc, "redc": " red" if redc else ""})

if __name__ == "__main__":
    main()
