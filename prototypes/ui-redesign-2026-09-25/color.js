// 色彩页：沿用当前软件的调色页结构（总开关 / 亮 / 颜色 / 曲线 / 混色器 / 颜色分级 / 校准 / LUT），只换 UI 风格。
(function () {
  const { h, I, seg, sw, slider, fmt } = window.VY;
  const BANDS = [['红色', '#FF5A5A'], ['橙色', '#FF9A3D'], ['黄色', '#F5D547'], ['绿色', '#4CD964'], ['浅绿色', '#3DD6C8'], ['蓝色', '#4F8BFF'], ['紫色', '#9B6BFF'], ['洋红', '#FF5AC8']];

  function cslider(app, lab, min, max, step, def, opt = {}) {
    const out = h(`<span class="val">${fmt(def, step)}</span>`);
    const s = slider(def, min, max, step, v => { out.textContent = (v > 0 && opt.center !== false && min < 0 ? '+' : '') + fmt(v, step); app.applyChange(false); }, { center: min < 0 });
    s.style.width = '150px';
    if (opt.grad) s.querySelector('.track').style.background = opt.grad;
    s.addEventListener('reset', () => s.set(def, true));
    const r = h(`<div class="row"><span class="lab">${lab}</span></div>`); r.append(s, out); return r;
  }
  function section(app, name, bit, build, open) {
    const d = h(`<div class="csec${open ? ' open' : ''}"><div class="csec-h"><button class="eye" title="临时旁路这一组（A/B 对比）">${I('eye')}</button><b>${name}</b><span class="sp"></span><button class="btn ghost icon rs" title="只还原这一组" style="width:26px;height:26px">${I('reset')}</button><span class="chev">${I('right')}</span></div><div class="acc-b"><div><div class="csec-b"></div></div></div></div>`);
    d.querySelector('.csec-h').onclick = e => { if (e.target.closest('button')) return; d.classList.toggle('open'); };
    const eye = d.querySelector('.eye'); eye.onclick = () => { const off = d.classList.toggle('bypass'); eye.innerHTML = I(off ? 'eyeoff' : 'eye'); app.applyChange(false); };
    build(d.querySelector('.csec-b')); return d;
  }
  function curve(app) {
    const wrap = h(`<div class="curvebox"><div class="curvech"></div><svg viewBox="0 0 200 200" class="curve"><defs><pattern id="cg" width="50" height="50" patternUnits="userSpaceOnUse"><path d="M50 0V50H0" fill="none" stroke="rgba(255,255,255,.08)"/></pattern></defs><rect width="200" height="200" fill="url(#cg)"/><path class="diag" d="M0 200L200 0"/><path class="cv"/></svg></div>`);
    const ch = seg(['RGB', '红', '绿', '蓝'], 'RGB', v => { stroke = { RGB: '#F3F3F5', 红: '#FF5A5A', 绿: '#4CD964', 蓝: '#4F8BFF' }[v]; draw(); });
    wrap.querySelector('.curvech').append(ch, h(`<span class="faint" style="font-size:11px">拖动控制点，双击删除</span>`));
    const svg = wrap.querySelector('svg'); let stroke = '#F3F3F5';
    const pts = [[0, 0], [.25, .2], [.72, .8], [1, 1]];
    function draw() {
      const P = pts.map(([x, y]) => [x * 200, 200 - y * 200]);
      let d = `M${P[0][0]},${P[0][1]}`;
      for (let i = 0; i < P.length - 1; i++) { const [x0, y0] = P[i], [x1, y1] = P[i + 1], mx = (x0 + x1) / 2; d += ` C${mx},${y0} ${mx},${y1} ${x1},${y1}`; }
      svg.querySelector('.cv').setAttribute('d', d); svg.querySelector('.cv').style.stroke = stroke;
      svg.querySelectorAll('circle').forEach(c => c.remove());
      P.forEach(([x, y], i) => { const c = document.createElementNS('http://www.w3.org/2000/svg', 'circle'); c.setAttribute('cx', x); c.setAttribute('cy', y); c.setAttribute('r', 5); c.dataset.i = i; svg.appendChild(c); });
    }
    svg.addEventListener('pointerdown', e => {
      const c = e.target.closest('circle'); if (!c) return; e.stopPropagation(); const i = +c.dataset.i; svg.setPointerCapture(e.pointerId);
      const mv = ev => { const r = svg.getBoundingClientRect(); const x = Math.max(0, Math.min(1, (ev.clientX - r.left) / r.width)), y = Math.max(0, Math.min(1, 1 - (ev.clientY - r.top) / r.height)); pts[i] = [i === 0 ? 0 : i === pts.length - 1 ? 1 : Math.max(pts[i - 1][0] + .02, Math.min(pts[i + 1][0] - .02, x)), y]; draw(); };
      svg.addEventListener('pointermove', mv); svg.addEventListener('pointerup', () => { svg.removeEventListener('pointermove', mv); app.applyChange(false); }, { once: true });
    });
    draw(); return wrap;
  }
  function wheel(app, name) {
    const w = h(`<div class="wheel"><div class="disc"><span class="puck"></span></div><b>${name}</b><small class="mono">H 0° · S 0</small></div>`);
    const disc = w.querySelector('.disc'), puck = w.querySelector('.puck'), out = w.querySelector('small');
    const setP = (x, y) => { puck.style.left = (50 + x * 50) + '%'; puck.style.top = (50 + y * 50) + '%'; const hue = (Math.atan2(y, x) * 180 / Math.PI + 360) % 360, s = Math.min(1, Math.hypot(x, y)); out.textContent = `H ${Math.round(hue)}° · S ${Math.round(s * 100)}`; };
    disc.addEventListener('pointerdown', e => {
      disc.setPointerCapture(e.pointerId);
      const mv = ev => { const r = disc.getBoundingClientRect(); let x = (ev.clientX - r.left) / r.width * 2 - 1, y = (ev.clientY - r.top) / r.height * 2 - 1; const m = Math.hypot(x, y); if (m > 1) { x /= m; y /= m; } setP(x, y); app.applyChange(false); };
      mv(e); disc.addEventListener('pointermove', mv); disc.addEventListener('pointerup', () => disc.removeEventListener('pointermove', mv), { once: true });
    });
    disc.addEventListener('dblclick', () => setP(0, 0));
    w.appendChild(slider(0, -100, 100, 1, () => app.applyChange(false), { center: true }));
    return w;
  }

  VY.curve = app => curve(app);
  VY.wheels = (app, compact) => { const g = h(`<div class="wheels${compact ? ' compact' : ''}"></div>`); ['阴影', '中间调', '高光', '全局'].forEach(z => g.appendChild(wheel(app, z))); return g; };
  VY.mixer = app => {
    const box = h(`<div></div>`);
    const mode = h(`<div class="row"><span class="lab">模式</span></div>`);
    const bandsRow = h(`<div class="bands"></div>`), rows = h(`<div></div>`);
    let cur = 0, bw = false;
    const fill = () => {
      rows.innerHTML = '';
      if (bw) rows.appendChild(cslider(app, `${BANDS[cur][0]} · 黑白`, -100, 100, 1, 0));
      else rows.append(cslider(app, `${BANDS[cur][0]} · 色相`, -100, 100, 1, 0), cslider(app, `${BANDS[cur][0]} · 饱和度`, -100, 100, 1, 0), cslider(app, `${BANDS[cur][0]} · 明亮度`, -100, 100, 1, 0));
    };
    mode.appendChild(seg(['HSL', '黑白'], 'HSL', v => { bw = v === '黑白'; fill(); app.applyChange(false); }));
    BANDS.forEach(([n, c], i) => { const x = h(`<button class="band${i === 0 ? ' sel' : ''}" title="${n}" style="--c:${c}"></button>`); x.onclick = e => { e.stopPropagation(); cur = i; bandsRow.querySelectorAll('.band').forEach(y => y.classList.toggle('sel', y === x)); fill(); }; bandsRow.appendChild(x); });
    fill(); box.append(mode, bandsRow, rows); return box;
  };
  VY.colorPanel = (app, body) => {
    const top = h(`<div class="ctop"><div class="row" style="min-height:44px"><span class="lab" style="color:var(--v-t1)"><b>调色</b><small style="display:block;color:var(--v-t3);font-size:11px">关闭时这条链路不存在，零开销</small></span></div>
      <div class="ctools"></div></div>`);
    top.querySelector('.row').appendChild(sw(true, v => { body.classList.toggle('disabled', !v); app.applyChange(true); }));
    const tools = top.querySelector('.ctools');
    [['undo', '撤销'], ['refresh', '重做'], ['copy', '复制'], ['import', '粘贴']].forEach(([ic, t]) => tools.appendChild(h(`<button class="btn ghost icon" title="${t}">${I(ic)}</button>`)));
    tools.appendChild(h(`<span class="sp"></span>`));
    const hold = h(`<button class="btn">${I('eye')}按住看原图</button>`); tools.appendChild(hold);
    tools.appendChild(h(`<button class="btn ghost">${I('reset')}一键还原</button>`));
    const pre = h(`<div class="row" style="min-height:40px"><span class="lab">色彩预设</span></div>`);
    pre.append(app.select('色彩预设', ['无', '电影暖调', '赛博霓虹', '褪色胶片', { sep: true }, { label: '保存为预设…' }, { label: '导入 .vpcolor…' }, { label: '导出 .vpcolor…' }], '电影暖调', null, 150));
    body.append(top, pre);
    body.appendChild(section(app, '亮', 0, b => {
      b.append(cslider(app, '曝光（EV）', -5, 5, .05, .15), cslider(app, '对比度', -100, 100, 1, 12), cslider(app, '高光', -100, 100, 1, -24), cslider(app, '阴影', -100, 100, 1, 18), cslider(app, '白色', -100, 100, 1, 0), cslider(app, '黑色', -100, 100, 1, -6));
    }, true));
    body.appendChild(section(app, '颜色', 1, b => {
      b.append(cslider(app, '色温（相对）', -100, 100, 1, 8, { grad: 'linear-gradient(90deg,#4F8BFF,#ddd,#FFB547)' }), cslider(app, '色调', -100, 100, 1, 0, { grad: 'linear-gradient(90deg,#4CD964,#ddd,#FF5AC8)' }), cslider(app, '自然饱和度', -100, 100, 1, 10, { grad: 'linear-gradient(90deg,#777,#FF9A3D)' }), cslider(app, '饱和度', -100, 100, 1, 0, { grad: 'linear-gradient(90deg,#777,#FF5A5A)' }));
      b.appendChild(h(`<p class="faint" style="font-size:11px;margin:4px 0 6px">采集卡没有拍摄白平衡，色温为相对值。</p>`));
    }, true));
    body.appendChild(section(app, '曲线', 2, b => b.appendChild(curve(app))));
    body.appendChild(section(app, '混色器', 3, b => b.appendChild(VY.mixer(app))));
    body.appendChild(section(app, '颜色分级', 4, b => {
      b.append(VY.wheels(app), cslider(app, '混合', 0, 100, 1, 50), cslider(app, '平衡', -100, 100, 1, 0));
    }));
    body.appendChild(section(app, '校准', 5, b => {
      b.appendChild(cslider(app, '阴影色调', -100, 100, 1, 0));
      [['红原色', '#FF5A5A'], ['绿原色', '#4CD964'], ['蓝原色', '#4F8BFF']].forEach(([n, c]) => b.append(cslider(app, `${n} · 色相`, -100, 100, 1, 0, { grad: `linear-gradient(90deg,#777,${c})` }), cslider(app, `${n} · 饱和度`, -100, 100, 1, 0)));
    }));
    body.appendChild(section(app, 'LUT', 6, b => {
      const r1 = h(`<div class="row"><span class="lab">3D LUT</span></div>`); r1.append(app.select('LUT', ['不使用 LUT', 'Kodak 2383.cube', 'Teal & Orange.cube', { sep: true }, { label: '导入 .cube LUT…' }], 'Kodak 2383.cube', null, 170));
      const r2 = h(`<div class="row"><span class="lab">输入空间</span></div>`); r2.append(seg(['Cineon log', 'sRGB 显示参考', 'PQ（HDR）'], 'Cineon log', () => app.applyChange(false)));
      b.append(r1, r2, cslider(app, 'LUT 强度', 0, 100, 1, 100));
    }));
  };
})();
