/* chart.js — dependency-free canvas line chart. Touches nothing outside its own canvas.
 *   const chart = TCSChart.create(canvas);
 *   chart.setData([{ id, name, points: [[x, v], ...] }]);
 *   chart.setWindow(ms); chart.setHidden([id, ...]); chart.render(now);
 * Hover or a horizontal drag shows a crosshair with values (kept after touch; tap again to dismiss).
 * touch-action: pan-y keeps vertical page scrolling; only horizontal drags reach the chart.
 */
(function (global) {
  'use strict';

  const PAD = [46, 12, 10, 22];                     // plot margins (CSS px): left, right, top, bottom
  const GAP = 2500;                                 // a step wider than this (ms) breaks the line
  const DASH = [[], [8, 4], [2, 3]];                // line style per series, same as the legend swatches
  const MIN = [1, 2, 5, 10, 15, 30];                // x tick candidates (minutes)
  const C = { s: ['#2563EB', '#44506B', '#D97706'], grid: '#E3E7EC', text: '#44506B', tip: '#FFFFFF', bd: '#D8DEE6', cross: '#1F2937' };
  const FONT = '11px system-ui, -apple-system, "PingFang SC", "Microsoft YaHei", sans-serif';

  /* local time as HH:MM, or HH:MM:SS when sec is set */
  const clock = (ms, sec) => {
    const d = new Date(ms), p = (n) => ('0' + n).slice(-2);
    return p(d.getHours()) + ':' + p(d.getMinutes()) + (sec ? ':' + p(d.getSeconds()) : '');
  };
  /* index of the first point with x >= t (binary search) */
  const lower = (pts, t) => {
    let a = 0, b = pts.length;
    while (a < b) {
      const m = (a + b) >> 1;
      if (pts[m][0] < t) a = m + 1; else b = m;
    }
    return a;
  };
  /* point closest in time to t; null when there is no data */
  function near(pts, t) {
    if (!pts.length) return null;
    const i = lower(pts, t), j = Math.min(i, pts.length - 1);
    return i > 0 && t - pts[i - 1][0] <= pts[j][0] - t ? pts[i - 1] : pts[j];
  }
  /* "nice" step (1/2/5 x 10^k) giving about n ticks across span */
  function nice(span, n) {
    const raw = span / n, mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10)), k = raw / mag;
    return (k <= 1 ? 1 : k <= 2 ? 2 : k <= 5 ? 5 : 10) * mag;
  }

  function create(canvas) {
    const ctx = canvas.getContext('2d');
    const S = { series: [], hidden: {}, win: 300000, hover: null, hold: false, w: 0, h: 0, dpr: 1 };

    function render(now) {
      if (!S.w) return;                             // not laid out yet (its tab is hidden)
      now = now || Date.now();
      const x0 = PAD[0], x1 = S.w - PAD[1], y0 = PAD[2], y1 = S.h - PAD[3], t0 = now - S.win;
      const vis = S.series.filter((s) => !S.hidden[s.id]);
      let lo = Infinity, hi = -Infinity;
      vis.forEach((s) => {
        for (let i = Math.max(0, lower(s.points, t0) - 1); i < s.points.length; i++) {
          const v = s.points[i][1];
          if (isFinite(v)) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
        }
      });
      const empty = lo === Infinity;
      if (empty) { lo = 20; hi = 40; }
      const step = nice(Math.max(hi - lo, 0.5) * 1.2, 4), pad = (hi - lo) * 0.1;
      lo = Math.floor((lo - pad) / step) * step;    // snapped to ticks: the axis does not jitter as samples enter or leave
      hi = Math.ceil((hi + pad) / step) * step;
      const X = (t) => x0 + (t - t0) * (x1 - x0) / S.win, Y = (v) => y1 - (v - lo) * (y1 - y0) / (hi - lo);
      ctx.setTransform(S.dpr, 0, 0, S.dpr, 0, 0);
      ctx.clearRect(0, 0, S.w, S.h);

      /* axes and grid: y at the nice step, x on whole minutes */
      const ms = (MIN.find((m) => S.win / 60000 / m <= 6) || 30) * 60000, dec = (String(step).split('.')[1] || '').length;
      ctx.font = FONT; ctx.lineWidth = 1; ctx.strokeStyle = C.grid; ctx.fillStyle = C.text;
      ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
      for (let v = Math.ceil(lo / step) * step; v <= hi + step * 1e-6; v += step) {
        const y = Math.round(Y(v)) + 0.5;
        ctx.beginPath(); ctx.moveTo(x0, y); ctx.lineTo(x1, y); ctx.stroke();
        ctx.fillText(v.toFixed(dec), x0 - 6, y);
      }
      ctx.textAlign = 'center'; ctx.textBaseline = 'top';
      for (let t = Math.ceil(t0 / ms) * ms; t <= now; t += ms) {
        const x = Math.round(X(t)) + 0.5;
        ctx.beginPath(); ctx.moveTo(x, y0); ctx.lineTo(x, y1); ctx.stroke();
        ctx.fillText(clock(t), Math.min(Math.max(x, x0 + 18), x1 - 18), y1 + 6);
      }

      /* series: points outside the window are skipped; at most about one point per pixel */
      ctx.lineWidth = 2; ctx.lineJoin = 'round';
      vis.forEach((s, n) => {
        const pts = s.points, len = pts.length, from = Math.max(0, lower(pts, t0) - 1);
        const stride = Math.max(1, Math.ceil((len - from) / Math.max(1, x1 - x0)));
        let px = null;
        const plot = (p) => {
          if (!isFinite(p[1]) || (px !== null && p[0] - px > GAP)) { px = null; return; }
          if (px === null) ctx.moveTo(X(p[0]), Y(p[1])); else ctx.lineTo(X(p[0]), Y(p[1]));
          px = p[0];
        };
        ctx.strokeStyle = C.s[n % 3]; ctx.setLineDash(DASH[n % 3]);
        ctx.beginPath();
        for (let i = from; i < len; i += stride) {
          plot(pts[i]);
          if (i + stride >= len && i < len - 1) plot(pts[len - 1]);
        }
        ctx.stroke();
      });
      ctx.setLineDash([]);
      if (empty) {
        ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillStyle = C.text;
        ctx.fillText('暂无数据', (x0 + x1) / 2, (y0 + y1) / 2);
        return;
      }

      /* crosshair and value box */
      if (!S.hover) return;
      const t = t0 + (Math.min(Math.max(S.hover, x0), x1) - x0) * S.win / (x1 - x0), x = Math.round(X(t)) + 0.5;
      let ref = null;
      vis.forEach((s) => { if (!ref) ref = near(s.points, t); });
      if (!ref) return;
      const rows = [clock(ref[0], 1)], marks = [];
      let w = ctx.measureText(rows[0]).width;
      vis.forEach((s, n) => {
        const q = near(s.points, ref[0]);
        if (q && isFinite(q[1])) {
          rows.push(s.name + '  ' + q[1].toFixed(2));
          w = Math.max(w, ctx.measureText(rows[rows.length - 1]).width);
          marks.push([Y(q[1]), n]);
        }
      });
      const bw = w + 16, bh = rows.length * 16 + 10, bx = x + 12 + bw > x1 ? x - 12 - bw : x + 12;
      ctx.setLineDash([4, 3]); ctx.strokeStyle = C.cross;
      ctx.beginPath(); ctx.moveTo(x, y0); ctx.lineTo(x, y1); ctx.stroke();
      ctx.setLineDash([]);
      marks.forEach((m) => {
        ctx.beginPath(); ctx.arc(x, m[0], 4, 0, 7);
        ctx.fillStyle = C.s[m[1] % 3]; ctx.fill();
        ctx.strokeStyle = C.tip; ctx.lineWidth = 1.5; ctx.stroke();
      });
      ctx.fillStyle = C.tip; ctx.fillRect(bx, y0, bw, bh);
      ctx.strokeStyle = C.bd; ctx.lineWidth = 1; ctx.strokeRect(bx + 0.5, y0 + 0.5, bw - 1, bh - 1);
      ctx.textAlign = 'left'; ctx.textBaseline = 'middle';
      rows.forEach((r, i) => {
        ctx.fillStyle = i ? C.s[marks[i - 1][1] % 3] : C.text;
        ctx.fillText(r, bx + 8, y0 + 13 + i * 16);
      });
    }

    /* bitmap = CSS size x devicePixelRatio; drawing stays in CSS px */
    function resize() {
      const w = canvas.clientWidth, h = canvas.clientHeight, dpr = Math.min(global.devicePixelRatio || 1, 3);
      if (!w || !h || (w === S.w && h === S.h && dpr === S.dpr)) return;
      S.w = w; S.h = h; S.dpr = dpr;
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
      render();
    }

    /* Pointer: the mouse follows hover and clears on leave. Touch shows on press, follows while pressed,
     * stays after release, and tapping the same spot again dismisses it. */
    const xOf = (e) => e.clientX - canvas.getBoundingClientRect().left;
    canvas.addEventListener('pointerdown', (e) => {
      if (e.pointerType !== 'touch') return;
      const x = xOf(e);
      S.hold = Math.abs(S.hover - x) >= 12;
      S.hover = S.hold ? x : null;
      try { canvas.setPointerCapture(e.pointerId); } catch (err) { /* pointer already gone */ }
      render();
    });
    canvas.addEventListener('pointermove', (e) => {
      if (e.pointerType === 'touch' && !e.buttons) return;   // touch follows only while pressed
      S.hold = e.pointerType === 'touch' && S.hold;
      S.hover = xOf(e);
      render();
    });
    canvas.addEventListener('pointerleave', () => { if (!S.hold) { S.hover = null; render(); } });
    canvas.addEventListener('pointercancel', () => { S.hold = false; S.hover = null; render(); });
    canvas.style.touchAction = 'pan-y';
    if (global.ResizeObserver) new global.ResizeObserver(resize).observe(canvas);
    else global.addEventListener('resize', resize);
    resize();

    return {
      setData: (series) => { S.series = series || []; },
      setHidden: (ids) => { S.hidden = {}; (ids || []).forEach((id) => { S.hidden[id] = true; }); },
      setWindow: (ms) => { if (ms > 0) S.win = ms; },
      render
    };
  }

  global.TCSChart = { create };
})(typeof window !== 'undefined' ? window : this);
