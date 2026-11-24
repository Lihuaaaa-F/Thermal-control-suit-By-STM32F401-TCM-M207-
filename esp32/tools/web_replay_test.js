// web_replay_test.js — 用真实 Chrome 加载回放页面并逐项检查（配合 web_replay.py serve 使用）
//
// 检查的是"真机转储的字节，页面能不能正确解析和显示"：
//   连接状态、总览数值 = 真机 st、参数页输入框数量/小数位、步进与被拒回执、遥测、日志条数、设备页信息，
//   页面上没有 NaN/undefined，控制台 0 报错，页面发出的每一帧格式合法。
// 回放里 WebSocket 是冒充的（见 web_replay.py），所以点任何控件都不会碰到真设备。
//
// 用法（先另开一个终端跑 serve）:
//   node esp32/tools/web_replay_test.js <frames.json> <输出目录> [http://127.0.0.1:18130/]
// 输出: <输出目录>/web_replay_check.txt（逐项 PASS/FAIL）与截图 web_replay_<desktop|mobile>_<页签>.png
// 退出码: 有 FAIL、控制台报错或页面异常 → 1
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');

const [framesPath, outDir, urlArg] = process.argv.slice(2);
if (!framesPath || !outDir) {
  console.log('usage: node web_replay_test.js <frames.json> <out-dir> [url]');
  process.exit(2);
}
const url = urlArg || 'http://127.0.0.1:18130/';
const frames = JSON.parse(fs.readFileSync(framesPath, 'utf8'));
fs.mkdirSync(outDir, { recursive: true });

/* ---- 期望值：全部从真机转储里算，不另写一份"标准答案" ---- */
const ws = frames.ws.map((f) => ({ t: f.t, j: JSON.parse(f.text) }));
const lastOf = (t) => { const a = ws.filter((f) => f.t === t); return a.length ? a[a.length - 1].j : null; };
const st = lastOf('st');
const reg = lastOf('reg').list;
const logList = ws.filter((f) => f.t === 'log').map((f) => f.j.list);
const link = JSON.parse(frames.http['/api/linkstats']);
const ENUM_IDS = [6, 8];                                  // 与前端 app.js 的 ENUM 一致：这两个 id 画成分段按钮
const shown = reg.filter((p) => (p.flags & 4) !== 0);
const writable = shown.filter((p) => (p.flags & 1) !== 0);
const exp = {
  stValues: ws.filter((f) => f.t === 'st').map((f) => f.j),
  fw: st.fw, ssid: link.ssid, link,
  writableNumeric: writable.filter((p) => ENUM_IDS.indexOf(p.id) < 0).length,
  enumGroups: writable.filter((p) => ENUM_IDS.indexOf(p.id) >= 0).length,
  dangerRows: writable.filter((p) => (p.flags & 32) !== 0).length,
  teleCount: shown.filter((p) => (p.flags & 1) === 0).length,
  // 日志 = 全量帧 ∪ 之后的增量帧（时间+内容相同的只算一条）
  logCount: new Set(logList.reduce((a, l) => a.concat(l.map((e) => e.ts + '|' + e.msg)), [])).size,
  rejectId: 3,
};
const fmt2 = (x) => Number(x).toFixed(2);
exp.tc1 = exp.stValues.map((s) => fmt2(s.tc1));
exp.tc2 = exp.stValues.map((s) => fmt2(s.tc2));
exp.lost = st.body_ok === 0 ? '体温传感器' : st.m207 === 0 ? '温控模块' : '';

/* ---- 页面内的检查（整段序列化后送进浏览器执行） ---- */
async function pageChecks(exp) {
  const R = [];
  const T = (name, ok, detail) => { R.push((ok ? 'PASS ' : 'FAIL ') + name + (detail ? '  [' + detail + ']' : '')); };
  const q = (s) => document.querySelector(s);
  const qa = (s) => Array.from(document.querySelectorAll(s));
  const txt = (s) => { const e = q(s); return e ? e.textContent.replace(/\s+/g, ' ').trim() : null; };
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const tab = async (name) => { q('[data-testid="tab-' + name + '"]').click(); await sleep(400); };
  const LONG = /\.\d{7,}/;

  for (let i = 0; i < 60 && !qa('#params .prow').length; i++) await sleep(100);   // 等首推（reg）到达
  await sleep(800);

  // A 连接
  const conn = txt('[data-testid="conn-state"]');
  T('A1 连接状态显示"已连接"', !!conn && conn.indexOf('已连接') >= 0, conn);
  T('A2 断线遮罩没有出现', q('[data-testid="overlay"]').classList.contains('hidden'));
  T('A3 没有"数据过期"标记', q('[data-testid="stale-badge"]').classList.contains('hidden'));
  const na = txt('[data-testid="no-alarm"]') || '';
  T('A4 提示与部件状态一致（有部件失联时不写"运行正常"）',
    exp.lost ? na.indexOf('运行正常') < 0 && na.indexOf(exp.lost) >= 0 : na.indexOf('无报警') >= 0, na);

  // B 总览数值 = 真机 st（回放里 st 有几条在循环，取到任意一条的值都算对）
  const v1 = txt('[data-testid="val-tc1"]'), v2 = txt('[data-testid="val-tc2"]');
  T('B1 贴身水温 = 真机 st.tc1', !!v1 && exp.tc1.some((v) => v1.indexOf(v) === 0), v1 + ' ∈ ' + exp.tc1.join('/'));
  T('B2 环境水温 = 真机 st.tc2', !!v2 && exp.tc2.some((v) => v2.indexOf(v) === 0), v2 + ' ∈ ' + exp.tc2.join('/'));
  T('B3 体温探头失联时显示的是状态文字而不是 0.00', /失联|未接|断线|未知|—/.test(txt('[data-testid="val-body"]') || ''),
    txt('[data-testid="val-body"]'));
  T('B4 设定值来自真机 st（tc1_set / tc2_set）',
    (txt('[data-testid="sub-tc1"]') || '').indexOf(String(exp.stValues[0].tc1_set)) >= 0 &&
    (txt('[data-testid="sub-tc2"]') || '').indexOf(String(exp.stValues[0].tc2_set)) >= 0,
    txt('[data-testid="sub-tc1"]') + ' / ' + txt('[data-testid="sub-tc2"]'));

  // D 参数页
  await tab('params');
  const inputs = qa('#params input.num, #adv input.num');
  T('D1 数值输入框数量 = 真机注册表里的可写数值参数', inputs.length === exp.writableNumeric,
    inputs.length + ' vs ' + exp.writableNumeric);
  T('D2 分段按钮组数量 = 注册表里的枚举参数', qa('#params .seg, #adv .seg').length === exp.enumGroups,
    qa('#params .seg, #adv .seg').length + ' vs ' + exp.enumGroups);
  const longN = inputs.filter((i) => LONG.test(i.value)).length;
  T('D3 没有长小数（float32 尾数）', longN === 0, longN + ' 个');
  const badN = inputs.filter((i) => !isFinite(parseFloat(i.value))).length;
  T('D4 每个输入框都是有限数值', badN === 0, badN + ' 个');
  const maxDec = Math.max(0, ...inputs.map((i) => (String(i.value).split('.')[1] || '').length));
  T('D5 输入框小数位 ≤ 3', maxDec <= 3, '最多 ' + maxDec + ' 位');
  T('D6 保护类参数带标记', qa('#params .tag.danger, #adv .tag.danger').length === exp.dangerRows,
    qa('#params .tag.danger, #adv .tag.danger').length + ' vs ' + exp.dangerRows);
  T('D7 参数名没有空行', qa('.prow label').every((l) => l.textContent.trim().length > 0), qa('.prow label').length + ' 行');

  // E 步进：页面发 set → 回放回一条真机 pv(ok=1) → 输入框更新
  const plus = qa('#params [data-testid$="-plus"]').find((b) => {
    const row = b.closest('.prow');
    return row && !row.querySelector('.tag.danger') && row.dataset.id !== String(exp.rejectId);
  });
  if (!plus) {
    T('E0 找到可步进的参数行', false);
  } else {
    const row = plus.closest('.prow'), inp = row.querySelector('input.num'), before = inp.value;
    plus.click();
    await sleep(500);
    T('E1 点「＋」后输入框按回执更新（id ' + row.dataset.id + '）', inp.value !== before, before + ' → ' + inp.value);
    T('E2 更新后的值仍是短小数', !LONG.test(inp.value), inp.value);
    T('E3 没有出错提示', (txt('[data-testid="toast"]') || '').indexOf('未修改') < 0, txt('[data-testid="toast"]'));
  }

  // F 被约束拒绝：回放对 id 3 回的是真机那条 pv(ok=0, err=-2)
  const rejRow = qa('.prow[data-id="' + exp.rejectId + '"]')[0];
  if (!rejRow) {
    T('F0 找到被拒参数行（id ' + exp.rejectId + '）', false);
  } else {
    const ri = rejRow.querySelector('input.num'), rb = ri.value;
    qa('[data-testid="param-' + exp.rejectId + '-plus"], [data-testid="param-' + exp.rejectId + '-opt-1"]')[0].click();
    await sleep(500);
    const toast = txt('[data-testid="toast"]') || '';
    T('F1 被拒绝时有可见提示', /未修改|拒绝/.test(toast), toast);
    T('F2 被拒绝后输入框保持原值', ri.value === rb, rb + ' / ' + ri.value);
  }

  // G 遥测（只读项）
  const tele = qa('#tele .tele');
  T('G1 遥测项数量 = 注册表里的只读项', tele.length === exp.teleCount, tele.length + ' vs ' + exp.teleCount);
  const emptyTele = tele.filter((t) => { const b = t.querySelector('b'); return !b || /^(|—|NaN|undefined)$/.test(b.textContent.trim()); });
  T('G2 每项遥测都有值（探头失联的体温除外）', emptyTele.length <= 1,
    emptyTele.map((t) => t.textContent.replace(/\s+/g, ' ').trim()).join(' ; '));

  // H 日志
  await tab('log');
  await sleep(400);
  const items = qa('#log-list li').map((l) => l.textContent.replace(/\s+/g, ' ').trim());
  T('H1 日志条数 = 真机日志条数', items.length === exp.logCount, items.length + ' vs ' + exp.logCount);
  T('H2 日志没有重复', new Set(items).size === items.length, new Set(items).size + ' 条不同');
  const lc = txt('[data-testid="log-count"]') || '';
  T('H3 日志计数与列表一致', lc.indexOf(String(items.length)) === 0, lc);

  // I 设备页
  await tab('device');
  await sleep(900);
  const body = document.body.textContent;
  T('I1 设备页显示真机热点名', body.indexOf(exp.ssid) >= 0, exp.ssid);
  T('I2 设备页显示真机固件版本', body.indexOf(exp.fw) >= 0, exp.fw);
  T('I3 设备页显示屏链路收帧数', body.indexOf(String(exp.link.rx_frames)) >= 0, 'rx_frames=' + exp.link.rx_frames);
  const rdt = qa('dl[data-dev] dt').find((d) => d.textContent.trim() === '复位原因');
  const rtxt = rdt && rdt.nextElementSibling ? rdt.nextElementSibling.textContent.trim() : '';
  T('I4 复位原因显示成文字（不露原始码）', rtxt.length > 0 && rtxt !== '—' && rtxt.indexOf('复位代码') < 0, rtxt);

  // L 整页
  await tab('overview');
  const junk = (document.body.innerText.match(/\b(NaN|undefined|Infinity|null)\b/g) || []).length;
  T('L1 页面上没有 NaN/undefined/Infinity/null', junk === 0, junk + ' 处');
  return R;
}

/* ---- CDP 驱动（Node ≥ 22 自带 WebSocket） ---- */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const getJson = (u) => new Promise((res, rej) => {
  http.get(u, (r) => { let b = ''; r.on('data', (c) => { b += c; }); r.on('end', () => res(JSON.parse(b))); }).on('error', rej);
});
const CHROME = process.env.CHROME || 'C:/Program Files/Google/Chrome/Application/chrome.exe';

async function runViewport(w, h, tag, problems) {
  const port = 9333 + Math.floor(Math.random() * 500);
  const prof = fs.mkdtempSync(path.join(os.tmpdir(), 'cdp_prof_'));
  const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-first-run', '--remote-debugging-port=' + port,
    '--user-data-dir=' + prof, '--hide-scrollbars', '--window-size=' + w + ',' + h, 'about:blank'], { stdio: 'ignore' });
  let targets = null;
  for (let i = 0; i < 60 && !targets; i++) { await sleep(250); try { targets = await getJson('http://127.0.0.1:' + port + '/json'); } catch (e) { /* not up yet */ } }
  if (!targets) { proc.kill(); throw new Error('chrome did not start'); }
  const page = targets.find((t) => t.type === 'page');
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((r) => { ws.onopen = r; });
  let id = 0; const pending = new Map();
  const errs = [];
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); return; }
    if (m.method === 'Runtime.exceptionThrown') errs.push('exception: ' + (m.params.exceptionDetails.exception || {}).description);
    else if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') errs.push('console.error: ' + m.params.args.map((a) => a.value || a.description).join(' '));
    else if (m.method === 'Log.entryAdded' && m.params.entry.level === 'error') errs.push('log: ' + m.params.entry.text + ' ' + (m.params.entry.url || ''));
    else if (m.method === 'Network.loadingFailed' && !m.params.canceled) errs.push('network: ' + m.params.errorText);
  };
  const send = (method, params) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params: params || {} })); });
  await send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 2, mobile: w < 800 });
  for (const d of ['Page', 'Runtime', 'Log', 'Network']) await send(d + '.enable');
  await send('Page.navigate', { url });
  const shot = async (name) => {
    await send('Runtime.evaluate', { expression: 'document.querySelector(\'[data-testid="tab-' + name + '"]\').click()' });
    await sleep(700);
    const s = await send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(path.join(outDir, 'web_replay_' + tag + '_' + name + '.png'), Buffer.from(s.result.data, 'base64'));
  };
  const r = await send('Runtime.evaluate', { expression: '(' + pageChecks.toString() + ')(' + JSON.stringify(exp) + ')', awaitPromise: true, returnByValue: true });
  let lines;
  if (r.result && r.result.exceptionDetails) {
    lines = ['FAIL 页面内检查脚本抛异常: ' + JSON.stringify(r.result.exceptionDetails.exception || r.result.exceptionDetails.text).slice(0, 300)];
  } else {
    lines = r.result.result.value;
  }
  for (const n of ['overview', 'params', 'device', 'log']) await shot(n);
  if (errs.length) problems.push(...errs.map((e) => tag + ': ' + e.slice(0, 200)));
  const nFail = lines.filter((l) => l.indexOf('FAIL ') === 0).length;
  ws.close(); proc.kill();
  try { fs.rmSync(prof, { recursive: true, force: true }); } catch (e) { /* 临时目录，留着也无妨 */ }
  return { lines, nFail };
}

(async () => {
  const problems = [];
  const out = ['web_replay_check  ' + new Date().toISOString().slice(0, 19) + 'Z',
    'frames: ' + frames.meta.ws_frames + ' ws / ' + frames.meta.http_bodies + ' http, ' + frames.meta.bytes_crc_ok + ' bytes, bad=' + frames.meta.bad.length,
    'expect: ' + JSON.stringify({ writableNumeric: exp.writableNumeric, enumGroups: exp.enumGroups, dangerRows: exp.dangerRows,
      teleCount: exp.teleCount, logCount: exp.logCount, fw: exp.fw, ssid: exp.ssid }),
    ''];
  let fail = 0;
  for (const [w, h, tag] of [[1280, 800, 'desktop'], [375, 667, 'mobile']]) {
    out.push('--- ' + tag + ' ' + w + 'x' + h);
    const r = await runViewport(w, h, tag, problems);
    out.push(...r.lines);
    fail += r.nFail;
  }
  // M 控制台/网络
  out.push('--- browser');
  out.push((problems.length ? 'FAIL ' : 'PASS ') + 'M1 控制台 0 报错、0 未捕获异常、0 请求失败' + (problems.length ? '  [' + problems.length + ' 条，见下]' : ''));
  for (const p of problems.slice(0, 8)) out.push('    ' + p);
  fail += problems.length ? 1 : 0;
  // N 页面发出的帧
  const txPath = framesPath + '.tx.jsonl';
  out.push('--- uplink frames (what the page actually sent)');
  if (!fs.existsSync(txPath)) {
    out.push('FAIL N0 找到上行帧记录 ' + txPath); fail++;
  } else {
    const tx = fs.readFileSync(txPath, 'utf8').split('\n').filter((x) => x.trim()).map((x) => { try { return JSON.parse(x); } catch (e) { return { bad: x }; } });
    const badJson = tx.filter((m) => m.bad).length;
    const badShape = tx.filter((m) => !m.bad && !(m.t === 'ping' || (m.t === 'set' && Number.isInteger(m.id) && isFinite(Number(m.v)) && Number.isInteger(m.ref)) ||
      (m.t === 'cmd' && Number.isInteger(m.c) && Number.isInteger(m.ref)))).length;
    const kinds = {}; tx.forEach((m) => { if (!m.bad) kinds[m.t] = (kinds[m.t] || 0) + 1; });
    out.push((badJson === 0 ? 'PASS ' : 'FAIL ') + 'N1 页面发出的 ' + tx.length + ' 帧都是合法 JSON' + (badJson ? '  [' + badJson + ' 帧不是]' : ''));
    out.push((badShape === 0 ? 'PASS ' : 'FAIL ') + 'N2 每帧形状符合契约（set: 整数 id/数值 v/整数 ref；cmd: 整数 c/ref；ping）' + (badShape ? '  [' + badShape + ' 帧不符]' : ''));
    out.push('    种类: ' + JSON.stringify(kinds));
    out.push('    样例: ' + tx.filter((m) => !m.bad && m.t !== 'ping').slice(0, 3).map((m) => JSON.stringify(m)).join('  '));
    fail += (badJson ? 1 : 0) + (badShape ? 1 : 0);
  }
  const nPass = out.filter((l) => l.indexOf('PASS ') === 0).length;
  out.push('', 'web_replay_check: ' + nPass + ' PASS, ' + fail + ' FAIL');
  fs.writeFileSync(path.join(outDir, 'web_replay_check.txt'), out.join('\n') + '\n');
  console.log(out.join('\n'));
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
