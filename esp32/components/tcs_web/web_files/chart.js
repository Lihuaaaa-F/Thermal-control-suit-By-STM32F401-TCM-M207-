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
  const GAP = 2500;                                 // a step wider than this between neighbouring raw points breaks the line
  const DASH = [[], [8, 4], [2, 3]];                // line style per series, same as the legend swatches
  const MIN = [1, 2, 5, 10, 15, 30];                // x tick candidates (minutes)
  const HARD_C = 48;                                // over-temperature hard cut-off (control.h)
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

    /* everything the drawing helpers need about this frame's geometry */
    function geometry(now) {
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
      // 48 °C 硬切断阈值必须始终在纵轴上（W-23，与屏端一致）：只向上扩到最近的刻度，不重算步长。
      if (hi < HARD_C + 0.5) hi = Math.ceil((HARD_C + 0.5) / step) * step;
      if (hi - lo < step * 1e-6) { lo -= step; hi += step; }   // a flat line sitting exactly on a tick: give it a band
      return {
        x0, x1, y0, y1, t0, now, vis, empty, step, lo, hi,
        X: (t) => x0 + (t - t0) * (x1 - x0) / S.win,
        Y: (v) => y1 - (v - lo) * (y1 - y0) / (hi - lo)
      };
    }

    /* axes and grid: y at the nice step, x on whole minutes */
    function drawAxes(G) {
      const ms = (MIN.find((m) => S.win / 60000 / m <= 6) || 30) * 60000, dec = (String(G.step).split('.')[1] || '').length;
      ctx.font = FONT; ctx.lineWidth = 1; ctx.strokeStyle = C.grid; ctx.fillStyle = C.text;
      ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
      for (let v = G.lo; v <= G.hi + G.step * 1e-6; v += G.step) {
        const y = Math.round(G.Y(v)) + 0.5;
        ctx.beginPath(); ctx.moveTo(G.x0, y); ctx.lineTo(G.x1, y); ctx.stroke();
        ctx.fillText(v.toFixed(dec), G.x0 - 6, y);
      }
      ctx.textAlign = 'center'; ctx.textBaseline = 'top';
      for (let t = Math.ceil(G.t0 / ms) * ms; t <= G.now; t += ms) {
        const x = Math.round(G.X(t)) + 0.5;
        ctx.beginPath(); ctx.moveTo(x, G.y0); ctx.lineTo(x, G.y1); ctx.stroke();
        ctx.fillText(clock(t), Math.min(Math.max(x, G.x0 + 18), G.x1 - 18), G.y1 + 6);
      }
    }

    /* 48 C reference line (W-23): drawn only when the auto range includes it. */
    function drawThreshold(G) {
      if (HARD_C < G.lo || HARD_C > G.hi) return;
      const y = Math.round(G.Y(HARD_C)) + 0.5;
      ctx.save();
      ctx.strokeStyle = '#DC2626'; ctx.lineWidth = 1.5; ctx.setLineDash([6, 4]);
      ctx.beginPath(); ctx.moveTo(G.x0, y); ctx.lineTo(G.x1, y); ctx.stroke();
      ctx.setLineDash([]);
      ctx.font = FONT; ctx.fillStyle = '#B45309'; ctx.textAlign = 'right'; ctx.textBaseline = 'bottom';
      ctx.fillText('48 °C 阈值', G.x1, y - 3);
      ctx.restore();
    }

    /* series: points outside the window are skipped; at most about one point per pixel */
    function drawSeries(G) {
      ctx.lineWidth = 2; ctx.lineJoin = 'round';
      G.vis.forEach((s) => {
        // colour and dash follow the series itself. By position among the visible ones, hiding one used to
        // recolour all the others (the legend no longer matched the lines).
        const k = Math.max(0, S.series.indexOf(s));
        const pts = s.points, len = pts.length, from = Math.max(0, lower(pts, G.t0) - 1);
        const stride = Math.max(1, Math.ceil((len - from) / Math.max(1, G.x1 - G.x0)));
        let px = null, prevT = null;
        const plot = (p, keep) => {
          // A gap is judged between NEIGHBOURING RAW points, never between the thinned ones: with a stride of 3
          // or more (30 min view on a phone) the thinned points are >= 3 s apart, wider than GAP, and every
          // segment came out "broken" — a blank chart.
          const gap = prevT !== null && p[0] - prevT > GAP;
          prevT = p[0];
          if (!keep) return;                         // thinned out: only its time matters (for the gap test)
          if (!isFinite(p[1]) || gap) { px = null; return; }   // no value, or a gap before this point: restart
          if (px === null) ctx.moveTo(G.X(p[0]), G.Y(p[1])); else ctx.lineTo(G.X(p[0]), G.Y(p[1]));
          px = p[0];
        };
        ctx.strokeStyle = C.s[k % 3]; ctx.setLineDash(DASH[k % 3]);
        ctx.beginPath();
        for (let i = from; i < len; i++) plot(pts[i], (i - from) % stride === 0 || i === len - 1);
        ctx.stroke();
        // dot on the newest point inside the window: "now" is readable without hovering
        for (let i = len - 1; i >= from; i--) {
          if (isFinite(pts[i][1]) && pts[i][0] >= G.t0) {
            ctx.beginPath(); ctx.arc(G.X(pts[i][0]), G.Y(pts[i][1]), 3, 0, 7);
            ctx.fillStyle = C.s[k % 3]; ctx.fill();
            break;
          }
        }
      });
      ctx.setLineDash([]);
    }

    /* crosshair and value box at S.hover */
    function drawCrosshair(G) {
      if (S.hover === null) return;
      const t = G.t0 + (Math.min(Math.max(S.hover, G.x0), G.x1) - G.x0) * S.win / (G.x1 - G.x0), x = Math.round(G.X(t)) + 0.5;
      let ref = null;
      G.vis.forEach((s) => { if (!ref) ref = near(s.points, t); });
      if (!ref) return;
      const rows = [clock(ref[0], 1)], marks = [];
      let w = ctx.measureText(rows[0]).width;
      G.vis.forEach((s) => {
        const q = near(s.points, ref[0]);
        if (q && isFinite(q[1])) {
          rows.push(s.name + '  ' + q[1].toFixed(2));
          w = Math.max(w, ctx.measureText(rows[rows.length - 1]).width);
          marks.push([G.Y(q[1]), Math.max(0, S.series.indexOf(s))]);
        }
      });
      const bw = w + 16, bh = rows.length * 16 + 10, bx = x + 12 + bw > G.x1 ? x - 12 - bw : x + 12;
      ctx.setLineDash([4, 3]); ctx.strokeStyle = C.cross;
      ctx.beginPath(); ctx.moveTo(x, G.y0); ctx.lineTo(x, G.y1); ctx.stroke();
      ctx.setLineDash([]);
      marks.forEach((m) => {
        ctx.beginPath(); ctx.arc(x, m[0], 4, 0, 7);
        ctx.fillStyle = C.s[m[1] % 3]; ctx.fill();
        ctx.strokeStyle = C.tip; ctx.lineWidth = 1.5; ctx.stroke();
      });
      ctx.fillStyle = C.tip; ctx.fillRect(bx, G.y0, bw, bh);
      ctx.strokeStyle = C.bd; ctx.lineWidth = 1; ctx.strokeRect(bx + 0.5, G.y0 + 0.5, bw - 1, bh - 1);
      ctx.textAlign = 'left'; ctx.textBaseline = 'middle';
      rows.forEach((r, i) => {
        ctx.fillStyle = i ? C.s[marks[i - 1][1] % 3] : C.text;
        ctx.fillText(r, bx + 8, G.y0 + 13 + i * 16);
      });
    }

    function render(now) {
      if (!S.w) return;                             // not laid out yet (its tab is hidden)
      now = now || Date.now();
      const G = geometry(now);
      ctx.setTransform(S.dpr, 0, 0, S.dpr, 0, 0);
      ctx.clearRect(0, 0, S.w, S.h);
      drawAxes(G);
      if (G.empty) {
        ctx.font = FONT; ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillStyle = C.text;
        ctx.fillText('暂无数据', (G.x0 + G.x1) / 2, (G.y0 + G.y1) / 2);
        return;
      }
      drawThreshold(G);
      drawSeries(G);
      drawCrosshair(G);
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
