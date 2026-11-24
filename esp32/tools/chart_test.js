// chart_test.js — chart.js 的判别性测试（不依赖浏览器：画布是一个记录调用的桩）。
// 覆盖三处真出过问题的地方：
//   1. 30 分钟视图在手机上空白（抽稀后的相邻点间隔 >= 3s，全被判成断线）；
//   2. 隐藏一条曲线后其余曲线变色（颜色按"可见序号"取）；
//   3. 恒定数据正好落在刻度上时纵轴范围塌成 0（空白图）。
// 用法（需要 Node >= 18，不进 run_all）：node esp32/tools/chart_test.js esp32/components/tcs_web/web_files/chart.js
// 退出码 0 = 全部通过。2026-11-24：对修复前的 chart.js 跑是 4 项红。
const path = process.argv[2];
const fs = require('fs');
const vm = require('vm');

const SER = ['#2563EB', '#44506B', '#D97706'];
const GRID = '#E3E7EC';

// ---- canvas stub: records path commands (with the stroke style current at the time) ----
function makeCanvas() {
  const calls = [], state = {};
  const ctx = new Proxy(state, {
    get(t, k) {
      if (k === 'measureText') return (s) => ({ width: String(s).length * 6 });
      if (k === 'setLineDash') return () => {};
      if (k === 'arc') return () => {};
      if (typeof k === 'string' && ['moveTo', 'lineTo', 'beginPath', 'stroke', 'fill', 'fillRect', 'strokeRect',
        'clearRect', 'fillText', 'setTransform'].includes(k))
        return (...a) => { calls.push({ op: k, a, style: t.strokeStyle }); };
      return t[k];
    },
    set(t, k, v) { t[k] = v; return true; }
  });
  return { calls, width: 0, height: 0, style: {}, clientWidth: 311, clientHeight: 220,
    getContext: () => ctx, addEventListener: () => {}, getBoundingClientRect: () => ({ left: 0 }),
    setPointerCapture: () => {} };
}
// segment commands of one series: [moveTo, lineTo] counted with that series' colour current
const seg = (cv, colour) => ({
  moves: cv.calls.filter((c) => c.op === 'moveTo' && c.style === colour).length,
  lines: cv.calls.filter((c) => c.op === 'lineTo' && c.style === colour).length,
  stroked: cv.calls.some((c) => c.op === 'stroke' && c.style === colour)
});

const sandbox = { console, Date, Math, Infinity, NaN, isFinite, Array, Object, String, Number, addEventListener: () => {} };
sandbox.window = sandbox;
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path, 'utf8'), sandbox);
const TCSChart = sandbox.TCSChart;

let pass = 0, fail = 0;
const check = (c, name) => { if (c) pass++; else { fail++; console.log('FAIL: ' + name); } };
const T0 = 1700000000000;

// ---- 1. 30-minute view after 15 minutes of 1 s samples (900 points): the line must not be blank ----
{
  const cv = makeCanvas(), ch = TCSChart.create(cv);
  const now = T0 + 900000;
  const pts = Array.from({ length: 900 }, (v, i) => [T0 + i * 1000, 20 + Math.sin(i / 50)]);
  ch.setData([{ id: 'tc1', name: '贴身水箱', points: pts }, { id: 'tc2', name: '环境水箱', points: pts.map((p) => [p[0], p[1] + 3]) }]);
  ch.setWindow(30 * 60000);
  cv.calls.length = 0;
  ch.render(now);
  const a = seg(cv, SER[0]), b = seg(cv, SER[1]);
  check(a.moves === 1 && b.moves === 1, '30 min view: one continuous line per series (moveTo ' + a.moves + '/' + b.moves + ')');
  check(a.lines > 100 && b.lines > 100, '30 min view: the lines are actually drawn (lineTo ' + a.lines + '/' + b.lines + ')');
  console.log('30 min view: tc1 moveTo=' + a.moves + ' lineTo=' + a.lines + '  tc2 moveTo=' + b.moves + ' lineTo=' + b.lines);
}

// ---- 2. a real gap (data stalled for a minute) still breaks the line ----
{
  const cv = makeCanvas(), ch = TCSChart.create(cv);
  const now = T0 + 300000, pts = [];
  for (let i = 0; i < 120; i++) pts.push([T0 + i * 1000, 20]);
  for (let i = 0; i < 60; i++) pts.push([T0 + 180000 + i * 1000, 21]);
  ch.setData([{ id: 'tc1', name: '贴身水箱', points: pts }]);
  cv.calls.length = 0;
  ch.render(now);
  const a = seg(cv, SER[0]);
  check(a.moves === 2, 'a 60 s gap breaks the line into two segments (moveTo = ' + a.moves + ')');
}

// ---- 3. hiding a series does not recolour the others ----
{
  const cv = makeCanvas(), ch = TCSChart.create(cv);
  const now = T0 + 60000;
  const mk = (id, name, off) => ({ id, name, points: Array.from({ length: 60 }, (v, i) => [T0 + i * 1000, 20 + off + i / 100]) });
  ch.setData([mk('tc1', '贴身水箱', 0), mk('tc2', '环境水箱', 3), mk('body', '体温', 17)]);
  cv.calls.length = 0;
  ch.render(now);
  SER.forEach((c, i) => check(seg(cv, c).stroked && !seg(cv, SER[(i + 1) % 3]).moves === false, 'series ' + i + ' drawn in its own colour with all visible'));
  cv.calls.length = 0;
  ch.setHidden(['tc2']);
  ch.render(now);
  check(seg(cv, SER[0]).stroked && seg(cv, SER[2]).stroked && !seg(cv, SER[1]).stroked,
    'after hiding the middle series: blue and orange are drawn, grey is not');
  const bad = cv.calls.filter((c) => c.op === 'stroke' && c.style === SER[1]);
  check(bad.length === 0, 'no line took over the hidden series\' colour');
}

// ---- 4. flat data exactly on a tick used to collapse the y range (blank chart) ----
{
  const cv = makeCanvas(), ch = TCSChart.create(cv);
  const now = T0 + 60000;
  ch.setData([{ id: 'tc1', name: '贴身水箱', points: Array.from({ length: 60 }, (v, i) => [T0 + i * 1000, 20]) }]);
  cv.calls.length = 0;
  ch.render(now);
  const a = seg(cv, SER[0]);
  check(a.lines >= 59, 'a constant 20.0 °C still draws a line (lineTo = ' + a.lines + ')');
  check(!cv.calls.some((c) => c.op === 'fillText' && c.a[0] === '暂无数据'), 'flat data is not "暂无数据"');
}

console.log('chart_test: PASS=' + pass + ' FAIL=' + fail);
process.exit(fail ? 1 : 0);
