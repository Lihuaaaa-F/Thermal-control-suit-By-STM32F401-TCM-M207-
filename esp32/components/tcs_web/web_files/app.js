/* app.js — thermal suit web console (protocol: WS_PROTOCOL.md v2).
 * store is the single state source. handleMessage(msg) is the single entry for WS frames and HTTP
 * snapshots and dispatches on msg.t. render*() read the store and patch text nodes only.
 * WS: reconnect after 1/2/4/8/10 s with a countdown; no st for 2 s => data marked stale; no frame
 * for 10 s => drop the socket. set/cmd carry a client ref and count as done only on their pv/ack.
 */
(function () {
  'use strict';

  const VERSION = '2.0.0', TABS = ['overview', 'params', 'device', 'log'], BACKOFF = [1, 2, 4, 8, 10];
  const CMD = { 1: '贴身 TEC 开', 2: '贴身 TEC 关', 3: '环境 TEC 开', 4: '环境 TEC 关', 5: '风扇切换', 6: '切到自动', 7: '切到手动', 8: '急停', 9: '解除报警', 10: '保存到模块' };
  const SW_CMD = { tec1: [2, 1], tec2: [4, 3], fan: [5, 5], mode: [7, 6] };
  const ENUM = { 6: ['手动', '自动'], 8: ['自动', '强制布防', '强制不布防'] };
  const GROUPS = [[0x100, 0x102, 'PID（贴身）'], [0x110, 0x112, 'PID（环境）'], [0x103, 0x10A, '保护（贴身）'], [0x113, 0x11A, '保护（环境）'],
    [0x10B, 0x10F, '输出/温度选项（贴身）'], [0x11B, 0x11F, '输出/温度选项（环境）'], [0x120, 0x126, '系统/PCB']];
  // alarm bit => [bit, key, title, what to do, latched]. latched: needs 解除; otherwise it clears by itself about 2 s after its source recovers.
  // "what to do" is a function of safe (= the device is in its safe state). 通信类故障在未布防时不进安全态
  // （设备照常按手动设定运行），所以"已停止温控"只在 safe 时说，否则是吓唬人。
  const stopped = (safe) => (safe ? '已停止温控并进入安全态。' : '自动温控不会启动。');
  const ALARMS = [
    [1, 'body', '体温传感器失联', (z) => stopped(z) + '请插紧体温传感器接头；接好后自动恢复。', 0],
    [2, 'm207', '温控模块通信中断', (z) => stopped(z) + '请检查温控模块的通信线与供电；接好后自动恢复。', 0],
    [4, 'estop', '急停已触发', () => 'TEC 已全部停止、风扇全速，设备已切到手动模式。确认安全后点「解除」。', 1],
    [8, 'overtemp', '水温过高', () => '水温超过 48 °C，已停止温控。等水温回落到 48 °C 以下再点「解除」。', 1],
    [16, 'eeprom', '参数存储校验失败', () => '已恢复默认参数。请重新核对各参数后点「确认恢复」。', 1],
    [32, 'probe', '水温探头断线', (z) => stopped(z) + '请检查水温探头接线；接好后自动恢复。', 0]];
  const LATCHED = 28;
  const LEVELS = { info: '● ', warn: '■ ', alarm: '▲ ' };
  const store = {
    conn: 'wait', retry: 0, nextAt: 0, ever: false, reconn: 0, timer: 0,
    st: null, stAt: 0, rxAt: 0, stale: false, boot: NaN, rtt: null, pingAt: 0,
    sig: '', tab: '', win: 300000, hidden: [], last: 0,
    reg: new Map(),
    pending: new Map(),
    logs: [], logFrom: 0, logTo: 0, logPause: false,
    series: ['贴身水箱', '环境水箱', '体温'].map((name, i) => ({ id: ['tc1', 'tc2', 'body'][i], name: name, points: [] }))
  };
  const rows = new Map();

  const $ = (id) => document.getElementById(id), on = (id, ev, fn) => $(id).addEventListener(ev, fn);
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const num = (v, d) => { const n = v == null ? NaN : Number(v); return isFinite(n) ? n : d; };
  const fix = (v, d) => { v = num(v, NaN); return isFinite(v) ? v.toFixed(d) : '—'; };
  const txt = (n, s) => { if (n.textContent !== s) n.textContent = s; };
  const vis = (n, show) => n.classList.toggle('hidden', !show);
  const set = (n, v) => { v = Array.isArray(v) ? v : ['', v]; n.dataset.s = v[0]; txt(n, v[1]); };
  const hms = (ms) => new Date(ms).toTimeString().slice(0, 8);
  const nameOf = (p) => (p && (p.name || p.key)) || '参数';

  // toast: one at a time, up to 3 queued, 2.5 s each
  const tq = [];
  let toastShown = null;
  function toast(msg, kind) {
    if (msg === (tq.length ? tq[tq.length - 1].msg : toastShown)) return;
    if (tq.length >= 3) tq.shift();
    tq.push({ msg: msg, kind: kind || 'info' });
    if (!toastShown) nextToast();
  }
  function nextToast() {
    const t = tq.shift(), n = $('toast');
    toastShown = t ? t.msg : null;
    vis(n, !!t);
    if (!t) return;
    txt(n, t.msg);
    n.dataset.kind = t.kind;
    setTimeout(() => { vis(n, false); setTimeout(nextToast, 200); }, 2500);
  }

  // confirmation sheet: non-blocking (data behind it keeps updating), resolves to the choice
  let modalDone = null;
  function confirmDialog(title, body, ok, danger) {
    if (modalDone) modalDone(false);
    return new Promise((done) => {
      modalDone = done;
      txt($('modal-title'), title);
      txt($('modal-body'), body);
      txt($('modal-ok'), ok || '确定');
      $('modal-ok').classList.toggle('danger', !!danger);
      vis($('modal'), true);
    });
  }
  function closeModal(ok) {
    const done = modalDone;
    if (!done) return;
    modalDone = null;
    vis($('modal'), false);
    done(ok);
  }

  // sending: set/cmd get an increasing ref and wait for the pv/ack that carries it
  let ws = null, ref = 0, chart = null;
  const frames = [];
  const logf = (dir, msg) => { frames.push({ dir: dir, at: Date.now(), msg: msg }); if (frames.length > 120) frames.shift(); };
  function send(obj, label, node, hint) {
    const tracked = obj.t === 'set' || obj.t === 'cmd', p = { ref: 0, id: obj.id, c: obj.c, v: obj.v, label: label, hint: hint, node: node, timer: 0 };
    if (tracked) p.ref = obj.ref = ++ref;
    logf('tx', obj);
    if (!(ws && ws.readyState === 1)) { if (tracked) unconfirmed(p, '未连接设备：'); return 0; }
    ws.send(JSON.stringify(obj));
    if (!tracked) return 1;
    store.pending.set(p.ref, p);
    p.timer = setTimeout(() => unconfirmed(p, ''), 1500);
    if (node) node.classList.add('pending');
    return p.ref;
  }
  function drop(p) {
    store.pending.delete(p.ref);
    clearTimeout(p.timer);
    if (p.node) p.node.classList.remove('pending');
  }
  function unconfirmed(p, msg) {
    drop(p);
    toast((msg || '设备未确认 ') + p.label + (p.hint ? '，' + p.hint : ''), p.hint || msg ? 'error' : 'warn');
    renderParam(p.id);
  }

  // handleMessage: the single entry for every incoming frame
  function handleMessage(msg, http) {
    try {
      if (typeof msg === 'string') msg = JSON.parse(msg);
      logf('rx', msg);
      if (!msg || typeof msg !== 'object') return;
      store.rxAt = Date.now();
      if (msg.t === 'st') onStatus(msg, http);
      else if (msg.t === 'pv') onPv(msg);
      else if (msg.t === 'ack') onAck(msg);
      else if (msg.t === 'reg') onReg(msg);
      else if (msg.t === 'log') onLog(msg.list);
      else if (msg.t === 'pong') { if (store.pingAt) store.rtt = Date.now() - store.pingAt; store.pingAt = 0; renderStatus(); }
      else if (msg.t === 'err') toast('设备无法解析上一条指令', 'warn');
    } catch (e) {
      console.error('bad frame', msg, e);
    }
  }

  function onStatus(s, http) {
    const c = store.st;
    if (http && c && num(s.seq) < num(c.seq) && num(s.ts) >= num(c.ts)) return;   // late HTTP snapshot: keep the newer WS frame
    store.st = s;
    store.stAt = Date.now();
    store.boot = store.stAt - num(s.ts);
    if (store.stAt - store.last >= 1000) {          // one chart sample per second (about 1800 per 30 min)
      store.last = store.stAt;
      [s.m207 ? s.tc1 : NaN, s.m207 ? s.tc2 : NaN, s.body_ok ? s.body : NaN].forEach((v, i) => {
        const pts = store.series[i].points;
        pts.push([store.last, num(v, NaN)]);
        while (pts.length && pts[0][0] < store.last - 1860000) pts.shift();
      });
      renderChart();
    }
    renderStatus();
  }

  function onPv(m) {
    const id = num(m.id), p = store.reg.get(id), v = num(m.v, NaN), pend = store.pending.get(m.ref);
    if (pend) {
      drop(pend);
      const nm = '「' + nameOf(p) + '」';
      if (m.ok === 0) toast(nm + (m.err === -1 ? '只读，未修改' : m.err === -2 ? '未修改：过冷阈值必须小于过热阈值' : '修改被拒绝'), 'error');
      else if (m.clamped) toast(nm + '超出范围，已按 ' + fmt(p, v) + ' 生效', 'warn');
    }
    if (p && isFinite(v)) p.def = v;
    renderParam(id);
    renderStatus();
  }

  function onAck(m) {
    const p = store.pending.get(m.ref);
    if (!p) return;
    drop(p);
    const rv = num(m.rv);
    if (rv === -2) toast(p.c === 10 ? '温控模块离线，未保存' : '安全态下不可操作，请先解除报警', 'error');
    else if (rv < 0) toast('设备不支持「' + p.label + '」', 'error');
    else if (p.c === 8) toast('急停已执行：TEC 全停、风扇全速', 'error');
    else if (p.c === 9) setTimeout(released, 700);
    else if (p.c === 10) toast('已保存到模块（' + rv + ' 条），掉电保持');
  }
  function released() {
    const s = store.st, a = s ? num(s.alarm) : 0;
    if (!a) return;                                   // everything cleared: nothing to warn about
    // 急停要等其余故障都恢复才能解除，所以说清是哪一项挡着（与设备屏幕同一口径）
    const why = a & 1 ? '体温传感器仍失联' : a & 32 ? '水温探头仍断线' : a & 2 ? '温控模块仍失联' : a & 8 ? '水温仍高于 48 °C' : a & 4 ? '' : '仍有故障未恢复';
    toast(why ? '未解除：' + why + (a & 4 ? '（急停要等其余故障都恢复后才能解除）' : '') : '急停仍未解除，请再点一次', 'warn');
  }

  // overview: value cards, switches, alarms. FIELDS = [element, formatter] for every status-driven text.
  const tank = (on, ok) => (ok ? [on ? 'on' : 'off', on ? '▶ TEC 运行' : '○ TEC 停止'] : ['bad', '● 模块离线']);
  // 源不在线时固件给的是最后一次有效值：不当作实时值显示，给"—"（卡片第三行写着原因）
  const FIELDS = [
    ['v-tc1', (s) => (s.m207 ? fix(s.tc1, 2) : '—')], ['v-tc2', (s) => (s.m207 ? fix(s.tc2, 2) : '—')], ['v-body', (s) => (s.body_ok ? fix(s.body, 2) : '—')],
    ['sub-tc1', (s) => '设定 ' + fix(s.tc1_set, 1)], ['sub-tc2', (s) => '设定 ' + fix(s.tc2_set, 1)],
    ['sub-body', (s) => '阈值 ' + fix(val(4, 36.5), 1) + ' – ' + fix(val(3, 37.5), 1)],
    ['state-tc1', (s) => tank(s.tec1, s.m207)], ['state-tc2', (s) => tank(s.tec2, s.m207)],
    ['state-body', (s) => (!s.body_ok ? ['bad', '● 传感器失联'] : s.body > val(3, 37.5) ? ['warn', '▲ 高于阈值'] : s.body < val(4, 36.5) ? ['warn', '▲ 低于阈值'] : ['on', '▶ 在阈值带内'])]
  ].map((f) => [$(f[0]), f[1]]);

  let almKey = '';
  function renderStatus() {
    const s = store.st || {}, safe = num(s.state) === 3;
    FIELDS.forEach((f) => set(f[0], f[1](s)));
    [['tec1', s.tec1], ['tec2', s.tec2], ['fan', s.fan], ['mode', s.mode]].forEach((w) => {
      const b = $('sw-' + w[0]), lock = safe && w[0] !== 'mode';
      b.setAttribute('aria-pressed', !!w[1]);
      txt(b.lastElementChild, (w[1] ? '● ' : '○ ') + (w[0] === 'mode' ? (w[1] ? '自动' : '手动') : lock ? (w[0] === 'fan' ? '全速' : '强制关') : (w[1] ? '开' : '关')));
    });
    const key = [s.alarm, s.latch, s.body_ok, s.m207, s.sens_open, num(s.tc1) > 48, num(s.state) === 3, store.stale].join();
    if (key !== almKey) { almKey = key; renderAlarms(); }
  }

  function buildAlarms() {
    $('alarms').innerHTML = ALARMS.map((a) => '<div class="alarm' + (a[4] ? '' : ' auto') + '" data-testid="alarm-' + a[1] + '"><div class="alarm-head">' + a[2] +
      '</span></div><div class="alarm-act">' + a[3](false) + '</div><div class="alarm-foot">' + (a[4] ? '<button type="button" class="btn primary" data-ack="1">' + (a[1] === 'eeprom' ? '确认恢复' : '解除') +
      '</button>' : '<span class="tag">将自动恢复</span>') + '</div></div>').join('');
  }
  function renderAlarms() {
    const alm = num(store.st && store.st.alarm), safe = num(store.st && store.st.state) === 3, cards = $('alarms').children;
    let shown = 0;
    ALARMS.forEach((a, i) => {
      vis(cards[i], alm & a[0]);
      if (alm & a[0]) { shown++; txt(cards[i].querySelector('.alarm-act'), a[3](safe)); }
    });
    $('alarms').querySelectorAll('button').forEach((b) => b.removeAttribute('data-testid'));
    ALARMS.some((a, i) => {
      if (!(alm & a[0] && a[4])) return false;
      cards[i].querySelector('button').setAttribute('data-testid', 'btn-ack');
      return true;
    });
    vis($('no-alarm'), !shown);
  }

  // chart and CSV export
  function renderChart() {
    if (store.tab === 'overview') chart.render();
  }
  function exportCsv() {
    const pts = store.series.map((sr) => sr.points), out = ['time,tc1_c,tc2_c,body_c'], d = new Date();
    pts[0].forEach((p, i) => {
      d.setTime(p[0]);
      out.push(d.getFullYear() + '-' + ('0' + (d.getMonth() + 1)).slice(-2) + '-' + ('0' + d.getDate()).slice(-2) + ' ' + hms(p[0]) + ',' +
        pts.map((s) => (s[i] && isFinite(s[i][1])) ? s[i][1].toFixed(2) : '').join(','));
    });
    const a = document.createElement('a'), url = URL.createObjectURL(new Blob([out.join('\n')], { type: 'text/csv' }));
    a.href = url;
    a.download = 'tcs_' + hms(Date.now()).replace(/\D/g, '') + '.csv';
    document.body.appendChild(a).click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    toast(out.length > 1 ? '已导出 ' + (out.length - 1) + ' 行' : '还没有可导出的数据');
  }

  // parameters: registry => controls
  const dec = (p) => (String(p.step).split('.')[1] || '').length;
  const fmt = (p, v) => (isFinite(v) ? v.toFixed(dec(p)) : '—') + (p.unit ? ' ' + p.unit : '');
  const val = (id, d) => { const p = store.reg.get(id); return p ? num(p.def, d) : d; };
  const pendOf = (id) => { let v; store.pending.forEach((p) => { if (p.id === id) v = p; }); return v; };
  const shownVal = (id) => { const p = pendOf(id); return p ? p.v : val(id, NaN); };

  function onReg(msg) {
    const list = (Array.isArray(msg.list) ? msg.list : []).filter((p) => p && isFinite(num(p.id))), next = new Map();
    if (!list.length) return;                       // an empty registry would wipe the parameter page until the next push
    const sig = list.map((p) => [p.id, p.type, p.flags, p.min, p.max, p.step, p.key, p.name, p.unit].join('|')).join();
    list.forEach((p) => {
      p.id = num(p.id); p.min = num(p.min, 0); p.max = num(p.max, 0); p.step = num(p.step, 1) || 1; p.def = num(p.def, NaN);
      next.set(p.id, p);
    });
    store.reg = next;
    if (sig !== store.sig) { store.sig = sig; buildParams(); }   // same parameter set: keep the DOM (open groups, focused input)
    else next.forEach((p) => renderParam(p.id));
    renderStatus();
  }

  function rowHtml(p) {
    const id = p.id, t = (s) => ' data-testid="param-' + id + (s ? '-' + s : '') + '"';
    const head = '<div class="prow" data-id="' + id + '"' + t() + '><label>' + esc(nameOf(p)) + (p.flags & 32 ? '<span class="tag danger">保护类</span>' : '') + '</label>';
    if (ENUM[id]) {
      return head + '<span class="prow-range"></span><div class="seg">' + ENUM[id].map((x, v) => '<button type="button" data-v="' + v + '"' + t('opt-' + v) + '>' + x + '</button>').join('') + '</div></div>';
    }
    return head + '<input class="num" type="text" inputmode="decimal"' + t('input') + '><span class="u">' + esc(p.unit || '') + '</span><span class="prow-range"></span>' +
      '<div class="prow-body"><button type="button" class="step" data-d="-1"' + t('minus') + '>－</button><input type="range" min="' + p.min + '" max="' + p.max + '" step="' + p.step + '"' + t('slider') +
      '><button type="button" class="step" data-d="1"' + t('plus') + '>＋</button></div></div>';
  }

  function buildParams() {
    const open = Array.from($('adv').children, (d) => d.open), adv = GROUPS.map(() => []), other = [], basic = [], tele = [];
    store.reg.forEach((p) => {
      if (!(p.flags & 4)) return;
      if (!(p.flags & 1)) { if (p.type !== 3) tele.push(p); return; }
      if (p.id < 256) basic.push(p);
      else { const g = GROUPS.findIndex((gr) => p.id >= gr[0] && p.id <= gr[1]); (g < 0 ? other : adv[g]).push(p); }
    });
    $('params').innerHTML = basic.map(rowHtml).join('');
    $('tele').innerHTML = tele.map((p) => '<span class="tele" data-id="' + p.id + '" data-testid="tele-' + p.id + '"><span>' + esc(nameOf(p)) + '</span><b></b></span>').join('');
    $('adv').innerHTML = GROUPS.map((g, i) => [g[2], adv[i], i + 1]).concat([['其它', other, 'other']]).filter((g) => g[1].length)
      .map((g) => '<details class="group" data-testid="group-' + g[2] + '"><summary><span>' + g[0] + '</span><span class="muted">' + g[1].length + ' 项' + (g[1].some((p) => p.flags & 32) ? ' · 含保护类' : '') +
        '</span></summary>' + g[1].map(rowHtml).join('') + '</details>').join('');
    Array.from($('adv').children).forEach((d, i) => { d.open = !!open[i]; });
    rows.clear();
    ['params', 'adv'].forEach((k) => $(k).querySelectorAll('.prow').forEach((row) => {
      const q = (s) => row.querySelector(s);
      rows.set(+row.dataset.id, { row: row, range: q('.prow-range'), slider: q('[type=range]'), input: q('[type=text]'), steps: row.querySelectorAll('.step'), segs: row.querySelectorAll('.seg button') });
    }));
    $('tele').querySelectorAll('.tele').forEach((c) => rows.set(+c.dataset.id, { tele: c.lastElementChild }));
    store.reg.forEach((p) => renderParam(p.id));
  }

  function renderParam(id) {
    const r = rows.get(id), p = store.reg.get(id);
    if (!r || !p) return;
    if (r.tele) { txt(r.tele, fmt(p, p.def)); return; }
    const pd = pendOf(id), v = pd ? pd.v : p.def, ok = isFinite(v);
    if (r.segs.length) r.segs.forEach((b) => b.setAttribute('aria-pressed', ok && +b.dataset.v === v));
    else {
      if (!r.drag) r.slider.value = ok ? v : p.min;
      if (document.activeElement !== r.input) r.input.value = ok ? v.toFixed(dec(p)) : '';
      r.steps[0].disabled = !ok || v <= p.min;
      r.steps[1].disabled = !ok || v >= p.max;
    }
    if (r.pending !== !!pd) {
      r.pending = !!pd;
      r.range.innerHTML = pd ? '<span class="spin"></span> 写入中…' : p.min + ' ~ ' + p.max + (p.unit ? ' ' + p.unit : '') + ' · 步进 ' + p.step;
    }
  }

  async function requestSet(id, raw) {
    const p = store.reg.get(id);
    if (!p || !isFinite(raw)) return;
    const clamped = Math.min(p.max, Math.max(p.min, raw));
    const v = +((p.min + Math.round((clamped - p.min) / p.step) * p.step).toFixed(dec(p) + 1)), text = ENUM[id] ? ENUM[id][v] : fmt(p, v), name = '「' + nameOf(p) + '」', thr = val(7, 40);
    // thr = high-temperature confirmation threshold (parameter 7). Cancelled or interrupted => the control snaps back.
    if ((id === 1 || id === 2) && v > thr && !(await confirmDialog('高温设定确认', '把' + name + '设为 ' + text + '？它超过了高温确认阈值 ' + thr + ' °C，可能烫伤皮肤。', '确认设为 ' + text, true)) ||
        p.flags & 32 && !(await confirmDialog('修改保护类参数', name + '是保护/输出限制类参数，改错会影响安全。确认改为 ' + text + '？', '确认修改', true))) return renderParam(id);
    if (v !== raw) toast(name + '超出范围，已按 ' + text + ' 设置', 'warn');
    send({ t: 'set', id: id, v: v }, name, rows.get(id).row);
    renderParam(id);
  }

  // log: event timeline, newest first
  function onLog(list) {
    if (!Array.isArray(list)) return;
    list.forEach((e) => {
      if (e && e.msg) store.logs.push({ ts: num(e.ts, NaN), lvl: LEVELS[e.lvl] ? e.lvl : 'info', ev: String(e.ev || ''), msg: String(e.msg) });
    });
    const cut = store.logs.length - 200;
    if (cut > 0) { store.logs.splice(0, cut); store.logFrom = Math.max(0, store.logFrom - cut); store.logTo = Math.max(0, store.logTo - cut); }
    if (store.tab === 'log') renderLog();
  }
  function renderLog() {
    const to = store.logPause ? store.logTo : store.logs.length, list = store.logs.slice(store.logFrom, to);
    $('log-list').innerHTML = list.reverse().map((e) => {
      const t = store.boot + e.ts;
      return '<li data-lvl="' + e.lvl + '"><span class="lt">' + (isFinite(t) ? hms(t) : '--:--:--') + '</span><span class="lm">' + LEVELS[e.lvl] + esc(e.msg) +
        '</span><span class="le">' + esc(e.ev) + '</span></li>';
    }).join('');
    txt($('log-count'), list.length + ' 条');
    vis($('log-paused'), store.logPause);
    txt($('log-paused'), '已暂停，期间新增 ' + (store.logs.length - to) + ' 条');
    vis($('log-empty'), !list.length);
    txt($('btn-log-pause'), store.logPause ? '继续' : '暂停');
    $('btn-log-pause').setAttribute('aria-pressed', store.logPause);
  }

  // device page: the rows come from one table; a missing value is always shown as "—"
  const RESET = ['未知', '上电', '外部复位', '软件复位', '程序异常', '中断看门狗', '任务看门狗', '其它看门狗', '深度睡眠唤醒', '欠压'];
  const STATE = ['待机', '制冷', '制热', '安全态'], ARMED = ['未布防', '已布防', '布防但源丢失报警中'];
  const LATCH = [[1, '超温锁存'], [2, '体温传感器丢失锁存'], [4, '温控模块/探头丢失锁存'], [8, '急停锁存']];
  /* [label, formatter]; a formatter gives text, or [state colour, text] (colour first: that is what set() expects) */
  const DEV = [
    ['ESP32 固件', (s) => s.fw || '—'],
    ['屏间协议', (s) => (isFinite(num(s.ver)) ? 'v' + s.ver : '—')],
    ['屏幕固件', () => '—（屏幕固件暂未上报版本）'],
    ['温控模块固件', () => { const v = val(0x135, NaN); return isFinite(v) ? '原始码 ' + v : '—'; }],
    ['运行时长', (s) => { const t = num(s.up_s); return isFinite(t) ? (t >= 86400 ? Math.floor(t / 86400) + ' 天 ' + Math.floor(t / 3600) % 24 + ' 小时' : new Date(t * 1000).toISOString().slice(11, 19)) : '—'; }],
    ['复位原因', (s) => { const c = num(s.reset); return isFinite(c) ? RESET[c] || '复位代码 ' + c : '—'; }],
    ['空闲内存', (s) => { const b = num(s.heap); return isFinite(b) ? (b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB' : Math.round(b / 1024) + ' KB') : '—'; }],
    ['控制状态', (s) => STATE[num(s.state)] || '—'],
    ['安全锁存', (s) => LATCH.filter((l) => num(s.latch) & l[0]).map((l) => l[1]).join('、') || '无'],
    ['布防', (s) => ARMED[num(s.armed)] || '—'],
    ['温控模块', (s) => [s.m207 ? 'ok' : 'bad', '● ' + (s.m207 ? '在线' : '离线')]],
    ['体温传感器', (s) => [s.body_ok ? 'ok' : 'bad', '● ' + (s.body_ok ? '正常' : '失联')]],
    ['水温探头', (s) => [s.sens_open ? 'bad' : 'ok', '● ' + (s.sens_open ? '断线' : '正常')]],
    ['屏幕', (s) => [s.screen ? 'ok' : 'warn', s.screen ? '● 已连接' : '○ 未连接（设备照常独立控温）']],
    ['往返延迟', () => { const r = store.rtt; return isFinite(r) ? [r <= 100 ? 'ok' : r <= 400 ? 'warn' : 'bad', r + ' ms'] : '—'; }],
    ['水温采样龄', (s) => (num(s.age) >= 65535 || !s.m207 ? '离线 / 未知' : isFinite(num(s.age)) ? s.age + ' ms' : '—')],
    ['重连次数', () => store.reconn + ' 次']];
  function buildDevice() {
    document.querySelectorAll('dl[data-dev]').forEach((dl) => {
      const r = dl.dataset.dev.split(':').map(Number);
      dl.innerHTML = DEV.slice(r[0], r[1]).map((d) => '<dt>' + d[0] + '</dt><dd>—</dd>').join('');
      dl.querySelectorAll('dd').forEach((dd, i) => FIELDS.push([dd, (s) => (store.st ? DEV[r[0] + i][1](s) : '—')]));
    });
  }

  // 热点信息与"屏幕↔主控"链路计数不在 st 里（它们不常变）：设备页打开时拉一次 /api/linkstats，之后每 5 s 刷新
  const LINK = [
    ['dev-ssid', (k) => k.ssid || '—（固件未上报）'], ['dev-pass', (k) => k.pass || '—（固件未上报）'], ['dev-url', (k) => k.url || '—（固件未上报）'],
    ['link-rx', (k) => (isFinite(num(k.rx_frames)) ? k.rx_frames + ' 帧' : '—')],
    ['link-crc', (k) => (isFinite(num(k.crc_err)) ? k.crc_err + ' 次' : '—')],
    ['link-echo', (k) => (isFinite(num(k.echo_drop)) ? k.echo_drop + ' 次' : '—')],
    ['link-retry', (k) => (isFinite(num(k.retry)) ? k.retry + ' 次' : '—')],
    ['link-drop', (k) => (isFinite(num(k.txq_drop)) ? (k.txq_drop + num(k.uart_txdrop, 0)) + ' 次' : '—')]];
  function loadLink() {
    fetch('/api/linkstats', { cache: 'no-store' }).then((r) => r.json())
      .then((k) => LINK.forEach((d) => txt($(d[0]), d[1](k && k.t === 'link' ? k : {})))).catch(() => {});
  }

  // tabs
  function setTab(name) {
    store.tab = TABS.indexOf(name) < 0 ? 'overview' : name;
    closeModal(false);
    TABS.forEach((t) => { $('tab-' + t).setAttribute('aria-selected', t === store.tab); vis($('view-' + t), t === store.tab); });
    if (store.tab === 'log') renderLog();
    else renderChart();
    if (store.tab === 'device') loadLink();
  }

  // connection: WS, backoff, staleness
  function renderConn() {
    const down = store.conn === 'down';
    $('conn').dataset.conn = store.conn;
    txt($('conn'), (down || store.conn === 'open' ? '● ' : '○ ') + (store.conn === 'open' ? '已连接' : down ? '连接断开' : '连接中…'));
    vis($('offline'), down);
    vis($('offline-sub'), down);
    txt($('offline-title'), store.ever ? '连接断开' : '无法连接设备');
  }
  function setStale(on) {
    store.stale = on;
    vis($('stale'), on);
    $('page').classList.toggle('stale', on);
  }
  function tick() {
    const now = Date.now(), age = now - store.stAt, stale = store.conn === 'open' && !!store.st && age > 2000;
    if (store.conn === 'down') txt($('offline-count'), String(Math.max(1, Math.ceil((store.nextAt - now) / 1000))));
    if (store.st) txt($('age'), age < 1000 ? '刚刚更新' : Math.round(age / 1000) + ' 秒前');
    if (stale !== store.stale) setStale(stale);
    if (stale) renderChart();
    if (store.conn === 'open' && now - store.rxAt > 10000) ws.close();   // silent socket: reconnect
  }
  function reconnect() {
    const wait = BACKOFF[Math.min(store.retry++, 4)];
    store.nextAt = Date.now() + wait * 1000;
    store.timer = setTimeout(connect, wait * 1000);
    renderConn();
  }
  function connect() {
    clearTimeout(store.timer);
    let sock;
    try { sock = new WebSocket((location.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + location.host + '/ws'); } catch (e) { return reconnect(); }
    ws = sock;
    sock.onopen = () => {
      store.conn = 'open'; store.retry = 0; store.ever = true; store.rxAt = Date.now();
      store.logs.length = 0; store.logFrom = store.logTo = 0;   // the history comes again: start the list over
      if (store.tab === 'log') renderLog();
      renderConn();
    };
    sock.onmessage = (ev) => { if (ws === sock) handleMessage(ev.data); };
    sock.onclose = () => {
      if (ws !== sock) return;
      ws = null;
      store.conn = 'down'; store.reconn++; store.rtt = null;
      setStale(false);
      Array.from(store.pending.values()).forEach((p) => unconfirmed(p, '连接断开：'));   // nothing stays pending, least of all 急停
      reconnect();
    };
  }

  // wiring and start-up
  const cmd = (c, node, hint) => { closeModal(false); send({ t: 'cmd', c: c }, CMD[c], node, hint); };
  function init() {
    buildAlarms();
    buildDevice();
    chart = TCSChart.create($('chart'));
    chart.setData(store.series);
    TABS.forEach((t) => on('tab-' + t, 'click', () => setTab(t)));
    Object.keys(SW_CMD).forEach((k) => on('sw-' + k, 'click', () => {
      const c = SW_CMD[k][k !== 'fan' && store.st && store.st[k] ? 0 : 1];
      send({ t: 'cmd', c: c }, CMD[c], $('sw-' + k));
    }));
    /* 急停与设备屏幕同一口径：先确认再执行（手机在口袋里误触、小孩乱点都不该直接把设备停掉）。
       确认页不挡底栏；真要急停时多一次点击换来的是不误停。*/
    on('btn-estop', 'click', async () => {
      if (await confirmDialog('确认急停？', 'TEC 将全部停止、风扇全开，设备切到手动模式。确认安全后点「解除」才能恢复控制。', '确认急停', true))
        cmd(8, null, '请改用设备屏幕上的急停');
    });
    on('alarms', 'click', (e) => { if (e.target.hasAttribute('data-ack')) cmd(9); });   // every 解除 is cmd 9; the device decides what clears
    on('btn-save', 'click', () => cmd(10, $('btn-save')));
    on('btn-csv', 'click', exportCsv);
    on('btn-retry', 'click', connect);
    on('btn-log-pause', 'click', () => { store.logPause = !store.logPause; store.logTo = store.logs.length; renderLog(); });
    on('btn-log-clear', 'click', () => { store.logFrom = store.logs.length; renderLog(); toast('已清空视图；设备上的记录不受影响'); });
    on('modal-ok', 'click', () => closeModal(true));
    on('modal-cancel', 'click', () => closeModal(false));
    on('modal', 'click', (e) => { if (e.target === e.currentTarget) closeModal(false); });
    document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeModal(false); });
    /* every parameter control is delegated; a row knows its parameter through data-id */
    const onParam = (e) => {
      const t = e.target, row = t.closest('.prow'), p = row && store.reg.get(+row.dataset.id), r = rows.get(p && p.id);
      if (!r || !p) return;
      if (e.type === 'click') {
        if (t.classList.contains('step')) requestSet(p.id, shownVal(p.id) + +t.dataset.d * p.step);
        else if (t.parentNode.classList.contains('seg')) requestSet(p.id, +t.dataset.v);
      } else if (t.type === 'range') {              // dragging only previews the value; releasing sends it
        r.drag = e.type === 'input';
        r.input.value = (+t.value).toFixed(dec(p));
        if (e.type === 'change') requestSet(p.id, +t.value);
      } else if (t.type === 'text' && e.type !== 'change') {
        const v = parseFloat(t.value);
        if (e.key === 'Enter') t.blur();
        else if (e.key === 'Escape') { r.skip = true; t.blur(); }
        else if (e.type === 'focusout') { if (r.skip) { r.skip = false; renderParam(p.id); } else if (isFinite(v) && v !== shownVal(p.id)) requestSet(p.id, v); else renderParam(p.id); }
      }
    };
    ['click', 'input', 'change', 'focusout', 'keydown'].forEach((ev) => $('view-params').addEventListener(ev, onParam));
    document.querySelectorAll('[data-win]').forEach((b) => b.addEventListener('click', () => {
      store.win = +b.dataset.win;
      chart.setWindow(store.win);
      document.querySelectorAll('[data-win]').forEach((x) => x.setAttribute('aria-pressed', x === b));
      renderChart();
    }));
    document.querySelectorAll('[data-series]').forEach((b) => b.addEventListener('click', () => {
      const id = b.dataset.series;
      store.hidden = store.hidden.indexOf(id) < 0 ? store.hidden.concat(id) : store.hidden.filter((h) => h !== id);
      b.setAttribute('aria-pressed', store.hidden.indexOf(id) < 0);
      chart.setHidden(store.hidden);
      renderChart();
    }));
    setTab('overview');
    renderConn();
    /* HTTP bootstrap: fill the page before the WS is up; snapshots go through the same handleMessage */
    ['status', 'registry', 'log'].forEach((k) => fetch('/api/' + k, { cache: 'no-store' }).then((r) => r.json()).then((m) => handleMessage(m, true)).catch(() => {}));
    connect();
    setInterval(tick, 500);
    setInterval(() => { if (send({ t: 'ping' })) store.pingAt = Date.now(); }, 5000);
    setInterval(() => { if (store.tab === 'device') loadLink(); }, 5000);
  }

  window.TCS = {
    version: VERSION,
    getState: () => ({ conn: store.conn, retry: store.retry, stale: store.stale, tab: store.tab, rtt: store.rtt, reconn: store.reconn, windowMs: store.win,
      hidden: store.hidden, pending: Array.from(store.pending.values(), (p) => p.label), logCount: store.logs.length, st: store.st }),
    handleMessage, send, setTab,
    lastFrames: (n) => frames.slice(-(n || 20))
  };
  init();
})();
