// Page: 专业 · 节点视图 — video on top, Blender-style node canvas below.
// Every node carries all its parameters inline. Nodes can float unconnected; drag one onto a link to insert it,
// shake it / press 断开 to pull it out. Single chain: Input → … → Output, 补帧 must be last.
(function () {
  const { h, I, R, scene, seg, sw, clamp, PAGES } = window.VY;

  PAGES.node = app => {
    const st = app.st;
    const el = h(`<section class="nodeview" aria-label="专业模式节点视图">
      <div class="nv-top" data-in>
        <div class="video"></div>
        <div class="nv-bar"><button class="btn" data-srcbtn>${I('video')}<span>采集卡 · Elgato 4K60 Pro MK.2</span>${I('down')}</button>
          <div class="nv-stats">
            <span><small>输入</small><b>1920×1080 · 60.00</b></span>
            <span><small>输出</small><b>3840×2160 · HDR10</b></span>
            <span class="hi"><small>显示</small><b data-disp>119.6</b><em>fps</em></span>
            <span><small>提交</small><b>119.8</b><em>fps</em></span>
            <span><small>排队</small><b>1.2 帧</b></span>
            <span><small>链路耗时</small><b data-total></b><em>/ 16.7 ms</em></span>
          </div>
          <span class="sp"></span><div class="viewtog"></div><button class="btn" data-npreset>${I('nodes')}<span data-npv></span>${I('down')}</button></div>
      </div>
      <div class="split" title="拖动调整上下比例"></div>
      <div class="canvas" data-in style="--d:1" tabindex="0" aria-label="节点画布">
        <div class="cw"><svg class="links"></svg></div>
        <span class="canvas-tip">右键添加 · 拖到连线上插入 · 拖远（上下超过一截）或按住 Alt 松手即断开 · 滚轮缩放 · 拖空白平移</span>
        <div class="canvas-toast" role="status"></div>
        <div class="timing" aria-label="链路耗时分布"><div class="tbar"></div></div>
        <div class="canvas-tools">
          <button class="btn ghost icon" data-fit title="适配视图">${I('fit')}</button>
          <button class="btn ghost icon" data-auto title="自动排列">${I('magnet')}</button>
          <span class="dock-sep"></span>
          <button class="btn ghost" data-add>${I('plus')}添加节点</button>
          <span class="dock-sep"></span><span class="chainv" data-chainv></span>
        </div>
        <div class="ctxmenu" role="menu"><input id="node-search-${st.uid}" class="field" placeholder="搜索节点…" autocomplete="off"><div class="ctx-list"></div></div>
      </div>
    </section>`);
    el.querySelector('.video').appendChild(scene(7, 0));
    const vseg = seg(['列表', '节点'], '节点', v => { if (v === '列表') { vseg._pick('节点', false); app.dialog('toList'); } });
    vseg.querySelectorAll('button')[0].prepend(h(I('list'))); vseg.querySelectorAll('button')[1].prepend(h(I('nodes')));
    el.querySelector('.viewtog').appendChild(vseg);
    const npv = el.querySelector('[data-npv]');
    const syncPreset = () => { npv.textContent = '节点预设：' + (R.presets.find(p => p.id === st.nodePreset)?.name || '未保存'); };
    el.querySelector('[data-npreset]').onclick = e => VY.presetMenu(app, e.currentTarget, 'node');
    el.querySelector('[data-srcbtn]').onclick = e => app.menu(e.currentTarget, '片源', [{ label: '采集卡 · Elgato 4K60 Pro MK.2', checked: true, icon: 'video' }, { label: '打开文件…', icon: 'folder' }, { label: 'PS5 串流…', icon: 'gamepad', d: 'ps5' }, { label: '屏幕捕获…', icon: 'monitor', d: 'screen' }], o => o.d && app.dialog(o.d), 'down');

    // graph state is separate from the list view: `g.nodes` holds every node on the canvas, `g.chain` the ordered ids connected Input→Output
    const g = st.graph || (st.graph = (() => {
      const mk = VY_MK;
      const nodes = [mk('color', { exposure: .2, contrast: 10 }), mk('sr'), mk('nr', { intensity: .8 }), mk('nr', { intensity: .35, style: '0' }), mk('vhdr'), mk('fg'), mk('color', { saturation: 12, lut: 'Kodak 2383' }), mk('nr', { intensity: .6, style: '2' })];
      const pos = {}; const chain = nodes.slice(0, 6).map(n => n.id);
      let x = 210; nodes.slice(0, 6).forEach((n, i) => { pos[n.id] = { x, y: 30 + (i % 2) * 30 }; x += 300; });
      pos[nodes[6].id] = { x: 520, y: 640 }; pos[nodes[7].id] = { x: 1120, y: 640 };
      pos.__in = { x: 0, y: 60 }; pos.__out = { x, y: 60 };
      nodes[6].on = true;
      return { nodes, chain, pos };
    })());
    const cv = el.querySelector('.canvas'), cw = el.querySelector('.cw'), svg = el.querySelector('svg.links'), toastEl = el.querySelector('.canvas-toast'), ctx = el.querySelector('.ctxmenu');
    const view = st.nodeView || (st.nodeView = { x: 30, y: 30, k: .62 });
    const NW = 236, IOW = 150, PORT_Y = 20;
    const byId = id => g.nodes.find(n => n.id === id);
    const applyView = () => { cw.style.transform = `translate(${view.x}px,${view.y}px) scale(${view.k})`; cv.style.backgroundPosition = `${view.x}px ${view.y}px`; cv.style.backgroundSize = `${22 * view.k}px ${22 * view.k}px`; };
    const toast = (t, bad = true) => { toastEl.textContent = t; toastEl.classList.toggle('good', !bad); toastEl.classList.add('on'); clearTimeout(toast.t); toast.t = setTimeout(() => toastEl.classList.remove('on'), 2600); };
    const count = type => g.nodes.filter(n => n.type === type).length;
    const nodeLabel = n => { const same = g.nodes.filter(x => x.type === n.type); const T = R.types[n.type]; return same.length > 1 ? `${T.name.replace(' 画面增强', '')} ${same.indexOf(n) + 1}` : T.name; };
    function validate(chain) {
      const ns = chain.map(byId);
      const fi = ns.findIndex(n => n.type === 'fg'); if (fi >= 0 && fi !== ns.length - 1) return '补帧必须是最后一个节点（它在呈现阶段执行）';
      const hi = ns.findIndex(n => R.types[n.type].outHdr && n.on);
      if (hi >= 0 && ns.slice(hi + 1).some(n => n.on && n.type !== 'fg')) return 'RTX Video HDR 输出后只能接补帧';
      return '';
    }
    const elMap = new Map(); let inEl, outEl;
    // Example GPU cost per node; the real build reads per-pass GPU timestamps.
    const COST = { color: .4, sr: 3.9, nr: 5.0, protect: .2, vhdr: .9, fg: 2.3 };
    const costOf = n => +(COST[n.type] * (n.type === 'nr' ? (n.p.size === '原生' ? 2.6 : 1) * (.8 + .4 * n.p.intensity) : 1)).toFixed(1);
    function timing() {
      const live = g.chain.map(byId).filter(n => n.on);
      const total = live.reduce((a, n) => a + costOf(n), 0);
      el.querySelector('[data-total]').textContent = total.toFixed(1);
      el.querySelector('[data-total]').parentElement.classList.toggle('over', total > 16.7);
      const bar = el.querySelector('.tbar');
      bar.innerHTML = '';
      const scale = Math.max(16.7, total);
      live.forEach(n => {
        const T = R.types[n.type], ms = costOf(n);
        const seg = h(`<button class="tseg${['#4F7BFF'].includes(T.hue) ? ' dark' : ''}" style="--c:${T.hue};flex-grow:${ms}" title="${nodeLabel(n)} · ${ms} ms"><span class="tn">${nodeLabel(n)}</span><b>${ms}</b></button>`);
        seg.onclick = () => { const e = elMap.get(n.id); select(n.id); if (e) { e.classList.remove('flash'); void e.offsetWidth; e.classList.add('flash'); } };
        bar.appendChild(seg);
      });
      const spare = scale - total; if (spare > 0) bar.appendChild(h(`<span class="tspare" style="flex-grow:${spare}" title="源帧预算余量 ${spare.toFixed(1)} ms"></span>`));
      requestAnimationFrame(() => bar.querySelectorAll('.tseg').forEach(sg => { sg.classList.remove('tight', 'tiny'); if (sg.scrollWidth > sg.clientWidth) { sg.classList.add('tight'); if (sg.scrollWidth > sg.clientWidth) sg.classList.add('tiny'); } }));
      elMap.forEach((e, id) => { const n = byId(id), b = e.querySelector('.ms'); if (!b) return; b.textContent = g.chain.includes(id) && n.on ? costOf(n) + ' ms' : '未接入'; });
    }

    function makeNode(n) {
      const T = R.types[n.type], linked = g.chain.includes(n.id);
      const e = h(`<div class="bnode${linked ? '' : ' floating'}${n.on ? '' : ' mute'}${n.born ? ' born' : ''}" data-id="${n.id}" style="--c:${T.hue}" tabindex="0">
        <div class="bh"><span class="collapse">${I('down')}</span><b></b>${T.exp ? '<span class="tag exp">实验</span>' : ''}<span class="sp"></span><span class="ms"></span><button class="btn ghost icon unl" title="断开连接（放在一旁）">${I('unlink')}</button><button class="btn ghost icon del" title="删除">${I('trash')}</button></div>
        <div class="bsock"><span class="sin">画面</span><span class="sout">${T.outHdr ? 'HDR 画面' : '画面'}</span></div>
        <div class="bbody"></div>
        <span class="port in"></span><span class="port out${T.outHdr ? ' hdr' : ''}"></span></div>`);
      delete n.born;
      e.querySelector('.bh b').textContent = nodeLabel(n);
      e.querySelector('.bh').insertBefore(sw(n.on, v => { n.on = v; e.classList.toggle('mute', !v); if (g.chain.includes(n.id)) app.applyChange(true); links(); }), e.querySelector('.unl'));
      e.querySelector('.bbody').appendChild(VY.editor(app, n, true));
      e.querySelector('.collapse').onclick = ev => { ev.stopPropagation(); e.classList.toggle('closed'); requestAnimationFrame(() => { links(); settle(n.id); }); };
      e.querySelector('.del').onclick = ev => { ev.stopPropagation(); remove(n); };
      e.querySelector('.unl').onclick = ev => { ev.stopPropagation(); detach(n, true); };
      e.querySelector('.bh').addEventListener('pointerdown', ev => startDrag(ev, n, e));
      e.addEventListener('keydown', ev => { if ((ev.key === 'Delete') && ev.target === e) remove(n); });
      e.querySelectorAll('details').forEach(d => d.addEventListener('toggle', () => requestAnimationFrame(() => { links(); settle(n.id); })));
      return e;
    }
    function place(e, p) { e.style.left = p.x + 'px'; e.style.top = p.y + 'px'; }
    function portPos(id, side) {
      const e = id === '__in' ? inEl : id === '__out' ? outEl : elMap.get(id), p = g.pos[id];
      const w = e ? e.offsetWidth : NW;
      return { x: p.x + (side === 'out' ? w : 0), y: p.y + PORT_Y + 12 };
    }
    function curve(a, b) { const dx = Math.max(50, Math.abs(b.x - a.x) * .5); return `M${a.x},${a.y} C${a.x + dx},${a.y} ${b.x - dx},${b.y} ${b.x},${b.y}`; }
    let hotLink = -1;
    function links() {
      const ids = ['__in', ...g.chain, '__out']; const parts = [];
      for (let i = 0; i < ids.length - 1; i++) {
        const a = portPos(ids[i], 'out'), b = portPos(ids[i + 1], 'in');
        const src = byId(ids[i]); const hdr = src && R.types[src.type].outHdr;
        parts.push(`<path data-i="${i}" class="${hdr ? 'hdr' : ''}${i === hotLink ? ' hot' : ''}" d="${curve(a, b)}"/>`);
      }
      svg.innerHTML = parts.join('');
      el.querySelector('[data-chainv]').textContent = `链路 ${g.chain.length} 个节点 · 未接入 ${g.nodes.length - g.chain.length}`;
      timing();
    }
    function relabel() { elMap.forEach((e, id) => { const n = byId(id); if (n) e.querySelector('.bh b').textContent = nodeLabel(n); }); }
    function nearestLink(nx, ny) {
      const ids = ['__in', ...g.chain, '__out']; let best = -1, bd = 70 / view.k;
      for (let i = 0; i < ids.length - 1; i++) {
        const a = portPos(ids[i], 'out'), b = portPos(ids[i + 1], 'in');
        if (nx < a.x - 40 || nx > b.x + 40) continue;
        const t = clamp((nx - a.x) / Math.max(1, b.x - a.x), 0, 1), my = a.y + (b.y - a.y) * (3 * t * t - 2 * t * t * t);
        const d = Math.abs(ny - my); if (d < bd) { bd = d; best = i; }
      }
      return best;
    }
    function startDrag(ev, n, e) {
      if (ev.button !== 0 || ev.target.closest('button,.sw')) return;
      ev.preventDefault(); ev.stopPropagation(); select(n.id);
      const s = app.scale() * view.k, sx = ev.clientX, sy = ev.clientY, p0 = { ...g.pos[n.id] }; let moved = false;
      e.setPointerCapture(ev.pointerId);
      const wasLinked = g.chain.includes(n.id);
      const mv = m => {
        const dx = (m.clientX - sx) / s, dy = (m.clientY - sy) / s;
        if (!moved && Math.abs(dx) + Math.abs(dy) < 4) return; moved = true; e.classList.add('drag');
        g.pos[n.id] = { x: p0.x + dx, y: p0.y + dy }; place(e, g.pos[n.id]);
        // while dragging a floating node (or a linked one with Alt), highlight the link it would drop into
        const cx = g.pos[n.id].x + e.offsetWidth / 2, cy = g.pos[n.id].y + 30;
        hotLink = (!wasLinked || m.altKey) ? nearestLink(cx, cy) : -1;
        e.classList.toggle('will-detach', wasLinked && (m.altKey || Math.abs(dy) > 220));
        links();
      };
      const up = m => {
        e.removeEventListener('pointermove', mv); e.removeEventListener('pointerup', up); e.classList.remove('drag', 'will-detach');
        if (!moved) { hotLink = -1; return; }
        const drop = hotLink; hotLink = -1;
        if (wasLinked && drop < 0 && (m.altKey || Math.abs(g.pos[n.id].y - p0.y) > 220)) { detach(n, false); return; }
        if (drop >= 0) {
          const next = g.chain.filter(id => id !== n.id);
          const before = ['__in', ...g.chain, '__out'][drop]; const at = before === '__in' ? 0 : next.indexOf(before) + 1;
          next.splice(at, 0, n.id);
          const bad = validate(next); if (bad) { toast(bad); links(); return; }
          if (!wasLinked && R.types[n.type].max === 1 && g.chain.some(id => byId(id).type === n.type)) { toast(`${R.types[n.type].name} 在链路里只能有一个`); links(); return; }
          g.chain = next; e.classList.remove('floating'); app.applyChange(true); toast(`已插入：${nodeLabel(n)}`, false); relabel(); links(); settle(n.id); return;
        }
        if (wasLinked) { // reorder by x among linked nodes
          const next = g.chain.slice().sort((a, b) => g.pos[a].x - g.pos[b].x);
          const bad = validate(next);
          if (bad) { toast(bad + '，已退回'); g.pos[n.id] = p0; e.style.transition = 'left .5s var(--spring),top .5s var(--spring)'; place(e, p0); setTimeout(() => e.style.transition = '', 520); animLinks(560); return; }
          if (next.some((id, i) => id !== g.chain[i])) { g.chain = next; app.applyChange(true); }
          fitOut(); links();
        }
        settle(n.id);
      };
      e.addEventListener('pointermove', mv); e.addEventListener('pointerup', up);
    }
    function detach(n, nudge) {
      if (!g.chain.includes(n.id)) return;
      g.chain = g.chain.filter(id => id !== n.id);
      const e = elMap.get(n.id); e.classList.add('floating');
      if (nudge) { g.pos[n.id] = { x: g.pos[n.id].x, y: g.pos[n.id].y + 330 }; e.style.transition = 'left .55s var(--spring),top .55s var(--spring)'; place(e, g.pos[n.id]); setTimeout(() => e.style.transition = '', 580); animLinks(600); }
      app.applyChange(true); toast(`已断开：${nodeLabel(n)}（留在画布上，拖回连线即可重新接入）`, false); links();
    }
    function remove(n) {
      const e = elMap.get(n.id); e.classList.add('dying');
      setTimeout(() => { g.nodes = g.nodes.filter(x => x !== n); const was = g.chain.includes(n.id); g.chain = g.chain.filter(id => id !== n.id); delete g.pos[n.id]; if (was) app.applyChange(true); build(); }, 220);
    }
    function select(id) { elMap.forEach((e, k) => e.classList.toggle('sel', k === id)); }
    function rectOf(id) { const e = id === '__in' ? inEl : id === '__out' ? outEl : elMap.get(id); return { x: g.pos[id].x, y: g.pos[id].y, w: e ? e.offsetWidth : NW, h: e ? e.offsetHeight : 200 }; }
    function settle(fixedId) {
      const ids = ['__in', '__out', ...g.nodes.map(n => n.id)], GAP = 28, moved = new Set();
      for (let pass = 0; pass < 40; pass++) {
        let any = false;
        for (let i = 0; i < ids.length; i++) for (let j = i + 1; j < ids.length; j++) {
          const a = rectOf(ids[i]), b = rectOf(ids[j]);
          const ox = Math.min(a.x + a.w, b.x + b.w) + GAP - Math.max(a.x, b.x), oy = Math.min(a.y + a.h, b.y + b.h) + GAP - Math.max(a.y, b.y);
          if (ox <= 0 || oy <= 0) continue;
          any = true;
          // move the one that is not being held; prefer horizontal push for linked nodes so chain order is kept
          let mover = ids[j] === fixedId || ids[j] === '__in' ? ids[i] : ids[j];
          if (mover === '__in') mover = ids[j];
          const m = rectOf(mover), o = mover === ids[i] ? b : a;
          const horiz = ox < oy * 1.4 || (g.chain.includes(mover) || mover === '__out');
          if (horiz) g.pos[mover].x += (m.x + m.w / 2 >= o.x + o.w / 2 ? 1 : -1) * ox;
          else g.pos[mover].y += (m.y + m.h / 2 >= o.y + o.h / 2 ? 1 : -1) * oy;
          moved.add(mover);
        }
        if (!any) break;
      }
      moved.forEach(id => { const e = id === '__out' ? outEl : id === '__in' ? inEl : elMap.get(id); if (!e) return; e.style.transition = 'left .55s var(--spring),top .55s var(--spring)'; place(e, g.pos[id]); setTimeout(() => e.style.transition = '', 580); });
      if (moved.size) animLinks(620);
    }
    function fitOut() {
      const maxX = Math.max(...g.chain.map(id => g.pos[id].x + (elMap.get(id)?.offsetWidth || NW)), IOW);
      if (g.pos.__out.x < maxX + 40) { g.pos.__out.x = maxX + 60; outEl.style.transition = 'left .5s var(--spring)'; place(outEl, g.pos.__out); setTimeout(() => outEl.style.transition = '', 520); }
    }
    function animLinks(ms) { const t0 = performance.now(); const f = () => { links(); if (performance.now() - t0 < ms) requestAnimationFrame(f); }; f(); }
    function build() {
      cw.querySelectorAll('.bnode,.ionode').forEach(x => x.remove()); elMap.clear();
      inEl = h(`<div class="ionode"><div class="bh"><span class="dotc" style="--c:#3DDC84"></span><b>输入</b></div><div class="iob"><div><span>源</span><b>1080p60</b></div><div><span>色彩</span><b>SDR · BT.709</b></div></div><span class="port out"></span></div>`);
      outEl = h(`<div class="ionode"><div class="bh"><span class="dotc" style="--c:#FF8A3D"></span><b>输出</b></div><div class="iob"><div><span>显示</span><b>4K · HDR10</b></div><div><span>字幕 / OSD</span><b>补帧之后合成</b></div></div><span class="port in"></span></div>`);
      cw.append(inEl, outEl); place(inEl, g.pos.__in); place(outEl, g.pos.__out);
      g.nodes.forEach(n => { const e = makeNode(n); elMap.set(n.id, e); place(e, g.pos[n.id]); cw.appendChild(e); });
      requestAnimationFrame(() => { fitOut(); links(); if (!g.settled) { g.settled = true; settle(null); } });
    }
    // pan / zoom
    cv.addEventListener('pointerdown', e => {
      if (e.target.closest('.bnode,.ionode,.canvas-tools,.ctxmenu')) return; closeCtx(); if (e.button === 2) return;
      cv.classList.add('panning'); cv.setPointerCapture(e.pointerId); select(null);
      const s = app.scale(), sx = e.clientX - view.x * s, sy = e.clientY - view.y * s;
      const mv = m => { view.x = (m.clientX - sx) / s; view.y = (m.clientY - sy) / s; applyView(); };
      const up = () => { cv.classList.remove('panning'); cv.removeEventListener('pointermove', mv); cv.removeEventListener('pointerup', up); };
      cv.addEventListener('pointermove', mv); cv.addEventListener('pointerup', up);
    });
    cv.addEventListener('wheel', e => {
      e.preventDefault(); e.stopPropagation(); const r = cv.getBoundingClientRect(), s = app.scale(), mx = (e.clientX - r.left) / s, my = (e.clientY - r.top) / s;
      const k = clamp(view.k * Math.exp(-e.deltaY * .0015), .3, 1.6); view.x = mx - (mx - view.x) * k / view.k; view.y = my - (my - view.y) * k / view.k; view.k = k; applyView();
    }, { passive: false });
    const fit = () => {
      const xs = Object.values(g.pos).map(p => p.x), ys = Object.values(g.pos).map(p => p.y);
      const x0 = Math.min(...xs) - 20, x1 = Math.max(...xs) + NW + 20, y0 = Math.min(...ys) - 20, y1 = Math.max(...ys) + 380;
      const k = clamp(Math.min((cv.clientWidth - 40) / (x1 - x0), (cv.clientHeight - 70) / (y1 - y0)), .3, 1);
      view.k = k; view.x = (cv.clientWidth - (x1 - x0) * k) / 2 - x0 * k; view.y = 14 - y0 * k;
      cw.style.transition = 'transform .6s var(--spring-soft)'; applyView(); setTimeout(() => cw.style.transition = '', 620);
    };
    el.querySelector('[data-fit]').onclick = fit;
    el.querySelector('[data-auto]').onclick = () => {
      let x = IOW + 40; g.chain.forEach((id, i) => { g.pos[id] = { x, y: 30 + (i % 2) * 24 }; x += NW + 26; }); g.pos.__out = { x, y: 60 };
      let fx = 190; g.nodes.filter(n => !g.chain.includes(n.id)).forEach(n => { g.pos[n.id] = { x: fx, y: 470 }; fx += NW + 40; });
      const all = [...elMap.values(), outEl]; all.forEach(e => e.style.transition = 'left .6s var(--spring),top .6s var(--spring)');
      g.nodes.forEach(n => place(elMap.get(n.id), g.pos[n.id])); place(outEl, g.pos.__out); animLinks(650);
      setTimeout(() => { all.forEach(e => e.style.transition = ''); fit(); }, 660);
    };
    // resizable split between video and canvas
    const split = el.querySelector('.split');
    split.addEventListener('pointerdown', e => {
      split.setPointerCapture(e.pointerId); const s = app.scale(), top = el.querySelector('.nv-top'), y0 = e.clientY, h0 = top.offsetHeight;
      const mv = m => { top.style.height = clamp(h0 + (m.clientY - y0) / s, 180, 560) + 'px'; };
      split.addEventListener('pointermove', mv); split.addEventListener('pointerup', () => split.removeEventListener('pointermove', mv), { once: true });
    });
    // add-node menu
    let ctxAt = null;
    function openCtx(x, y, world) {
      ctxAt = world; const list = ctx.querySelector('.ctx-list'), inp = ctx.querySelector('input'); inp.value = '';
      const render = q => {
        list.innerHTML = '';
        R.categories.forEach(c => {
          const ts = Object.entries(R.types).filter(([k, t]) => t.cat === c.id && (!q || t.name.toLowerCase().includes(q.toLowerCase()))); if (!ts.length) return;
          list.appendChild(h(`<h6>${c.name}</h6>`));
          ts.forEach(([k, t]) => {
            const full = count(k) >= t.max;
            const b = h(`<button class="opt" ${full ? 'disabled' : ''}><span class="sico" style="--c:${t.hue}">${I(t.icon)}</span><span>${t.name}<small>${full ? `最多 ${t.max} 个，画布上已有` : t.max === 1 ? '只能有一个' : `可添加多个 · 当前 ${count(k)} / ${t.max}`}</small></span>${t.exp ? '<span class="tag exp">实验</span>' : ''}</button>`);
            b.onclick = () => add(k); list.appendChild(b);
          });
        });
      };
      inp.oninput = () => render(inp.value); render('');
      ctx.style.left = clamp(x, 8, cv.clientWidth - 270) + 'px'; ctx.style.top = clamp(y, 8, cv.clientHeight - 330) + 'px';
      ctx.classList.add('open'); setTimeout(() => inp.focus({ preventScroll: true }), 30);
    }
    function closeCtx() { ctx.classList.remove('open'); }
    function add(type) {
      closeCtx(); const n = VY_MK(type); n.born = true; g.nodes.push(n);
      const p = ctxAt || { x: (cv.clientWidth / 2 - view.x) / view.k - NW / 2, y: (cv.clientHeight / 2 - view.y) / view.k };
      g.pos[n.id] = p; build(); select(n.id); requestAnimationFrame(() => requestAnimationFrame(() => settle(n.id)));
      toast(`已添加 ${R.types[type].name}：尚未接入，拖到连线上即可插入`, false);
    }
    cv.addEventListener('contextmenu', e => { e.preventDefault(); if (e.target.closest('.bnode,.ionode')) return; const r = cv.getBoundingClientRect(), s = app.scale(); const x = (e.clientX - r.left) / s, y = (e.clientY - r.top) / s; openCtx(x, y, { x: (x - view.x) / view.k, y: (y - view.y) / view.k }); });
    el.querySelector('[data-add]').onclick = e => { e.stopPropagation(); const r = app.rel(e.currentTarget), c = app.rel(cv); openCtx(r.x - c.x - 80, r.y - c.y - 340, null); };
    app.on((k, d) => { if (!el.classList.contains('on')) return; if (k === 'preset') syncPreset(); if (k === 'params') timing(); if (k === 'rerender' && d && elMap.has(d.id)) { const e = elMap.get(d.id), body = e.querySelector('.bbody'); body.innerHTML = ''; body.appendChild(VY.editor(app, d, true)); body.querySelectorAll('.seg').forEach(s => requestAnimationFrame(() => s._sync(true))); requestAnimationFrame(links); } });
    return { el, onShow() { st.nodePreset = st.nodePreset || 'n-grade'; syncPreset(); st.proView = 'node'; vseg._pick('节点', false); requestAnimationFrame(() => vseg._sync(true)); applyView(); build(); requestAnimationFrame(() => requestAnimationFrame(() => { cw.querySelectorAll('.seg').forEach(s => s._sync(true)); fit(); })); } };
  };
})();
