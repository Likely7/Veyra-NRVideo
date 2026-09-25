// Page: 专业 · 列表视图 — fixed product order (SR → NR layers → protect → HDR → 补帧), no reordering.
(function () {
  const { h, I, R, scene, seg, sw, slider, fmt, PAGES } = window.VY;

  // ---------- shared parameter editor (list accordion + node cards) ----------
  VY.paramRow = (app, inst, d, compact) => {
    const v = inst.p[d.k];
    if (d.type === 'curve') return VY.curve(app);
    if (d.type === 'mixer') return VY.mixer(app);
    if (d.type === 'wheels') return VY.wheels(app, compact);
    const row = h(`<div class="row"><span class="lab">${d.label}${d.exp ? ' <span class="tag exp">实验</span>' : ''}${d.sub && !compact ? `<small>${d.sub}</small>` : ''}</span></div>`);
    if (d.type === 'slider') {
      const out = h(`<span class="val">${fmt(v, d.step)}</span>`);
      const s = slider(v, d.min, d.max, d.step, nv => { out.textContent = fmt(nv, d.step); inst.p[d.k] = nv; app.applyChange(false); app.emit('params', inst); }, { center: d.center });
      if (d.grad) s.querySelector('.track').style.background = d.grad;
      s.style.width = compact ? '96px' : '150px'; s.addEventListener('reset', () => s.set(d.def, true));
      row.append(s, out);
    } else if (d.type === 'seg') {
      row.append(seg(typeof d.opts === 'function' ? d.opts(inst.p) : d.opts, v, nv => { inst.p[d.k] = nv; app.applyChange(d.rebuild); app.emit('params', inst); app.emit('rerender', inst); }));
    } else if (d.type === 'select') {
      row.append(app.select(d.label, d.opts, v, nv => { inst.p[d.k] = nv; app.applyChange(d.rebuild); app.emit('params', inst); app.emit('rerender', inst); }, compact ? 120 : 150));
    } else if (d.type === 'toggle') {
      row.append(sw(v, nv => { inst.p[d.k] = nv; app.applyChange(d.rebuild); app.emit('params', inst); }));
    } else if (d.type === 'number') {
      const f = h(`<input class="field mono" style="width:${compact ? 76 : 96}px;text-align:right" aria-label="${d.label}">`); f.value = v;
      f.onchange = () => { inst.p[d.k] = f.value; app.applyChange(false); app.emit('params', inst); };
      row.append(f, h(`<span class="faint" style="font-size:11px">${d.unit || ''}</span>`));
    } else if (d.type === 'readout') {
      row.append(h(`<span class="val">${v} / 4</span>`), h(`<button type="button" class="btn" style="height:28px">${I('scan')}框选</button>`));
    }
    if (d.rebuild) row.title = '修改后需要重建效果（约数百毫秒）';
    return row;
  };
  VY.editor = (app, inst, compact) => {
    const T = R.types[inst.type], wrap = h(`<div class="editor"></div>`);
    const vis = T.params.filter(d => !d.when || d.when(inst.p));
    vis.filter(d => !d.group).forEach(d => wrap.appendChild(VY.paramRow(app, inst, d, compact)));
    const groups = [...new Set(vis.filter(d => d.group).map(d => d.group))];
    groups.forEach(g => {
      const ds = vis.filter(d => d.group === g);
      const det = h(`<details class="sub"><summary><span>${g}${g === '实验' ? ' <span class="tag exp">实验</span>' : ''}</span><span class="faint">${ds.length} 项</span>${I('down')}</summary><div class="subin"></div></details>`);
      ds.forEach(d => det.querySelector('.subin').appendChild(VY.paramRow(app, inst, d, compact)));
      wrap.appendChild(det);
    });
    return wrap;
  };

  PAGES.pro = app => {
    const st = app.st;
    const el = h(`<section class="pro" aria-label="专业模式">
      <header class="pro-head" data-in>
        <button class="btn" data-srcbtn>${I('video')}<span>采集卡 · Elgato 4K60 Pro MK.2</span>${I('down')}</button>
        <span class="tag">1080p60 · YUY2 · BT.709</span>
        <span class="sp"></span>
        <div class="viewtog"></div>
        <button class="btn" title="截图">${I('camera')}截图</button>
        <button class="btn" data-presetbtn>${I('layers')}<span>预设：极致</span>${I('down')}</button>
      </header>
      <div class="vwrap" data-in style="--d:1"><div class="video"></div>
        <div class="vbar"><button class="btn ghost icon" style="width:28px;height:28px">${I('pause')}</button><span>00:41:03</span><div class="seek"><div class="rail"><div class="buf"></div><div class="played"></div></div><div class="thumb"></div></div><span>02:46:10</span><span class="tag">源 1080p60</span><span class="tag">输出 3840×2160</span><span class="tag acc" data-fgtag></span></div>
      </div>
      <div class="meters">
        <div class="card meter" data-in style="--d:2"><span class="eyebrow">信号</span>
          <div class="kv"><span>输入</span><b>1920×1080 · 60.00</b><span>NR 内部</span><b>1920×1080</b><span>输出</span><b>3840×2160</b><span>光流</span><b>NVOF · 平衡</b><span>色彩</span><b>SDR → HDR10</b></div></div>
        <div class="card meter" data-in style="--d:3"><span class="eyebrow" style="display:flex;justify-content:space-between"><span>各阶段 GPU 耗时</span><span class="mono">预算 16.7 ms / 源帧</span></span><div class="bars" data-bars></div></div>
        <div class="card meter" data-in style="--d:4"><span class="eyebrow">帧率与节奏</span>
          <div class="fpsline"><b class="big">119.6</b><span>显示 fps</span><span class="sep"></span><b>119.8</b><span>提交</span></div>
          <div class="kv"><span>进程内排队</span><b>1.2 帧</b><span>跳过源帧</span><b>0</b></div><canvas class="spark"></canvas></div>
      </div>
      <aside class="card insp" data-in style="--d:2">
        <div class="insp-tabs" role="tablist"></div>
        <div class="insp-content"></div>
        <footer class="insp-foot"><span class="dot"></span><span data-status></span><span class="sp"></span><button class="btn ghost" style="height:26px">${I('reset')}重置本页</button></footer>
      </aside>
    </section>`);
    el.querySelector('.video').appendChild(scene(7, 0));
    const vt = el.querySelector('.viewtog');
    const vseg = seg(['列表', '节点'], '列表', v => { if (v === '节点') { vseg._pick('列表', false); app.dialog('toNode'); } });
    vseg.querySelectorAll('button')[0].prepend(h(I('list'))); vseg.querySelectorAll('button')[1].prepend(h(I('nodes'))); vt.appendChild(vseg);
    el.querySelector('[data-presetbtn]').onclick = e => VY.presetMenu(app, e.currentTarget, 'list');
    el.querySelector('[data-srcbtn]').onclick = e => app.menu(e.currentTarget, '片源', [{ label: '采集卡 · Elgato 4K60 Pro MK.2', note: '1080p60 · YUY2', checked: true, icon: 'video' }, { label: '打开文件…', icon: 'folder' }, { label: 'PS5 串流…', icon: 'gamepad', d: 'ps5' }, { label: '屏幕捕获…', icon: 'monitor', d: 'screen' }, { sep: true }, { label: '采集卡设置…', icon: 'settings', d: 'capture' }], o => { if (o.d) app.dialog(o.d); }, 'down');

    // meters
    const bars = el.querySelector('[data-bars]');
    [['光流', 1.6, '#6EA8FF'], ['超分', 3.9, '#EDEDF0'], ['NR 1', 5.2, '#FF8A3D'], ['NR 2', 4.8, '#FF8A3D'], ['HDR', .9, '#FFB547'], ['补帧', 2.3, '#3DDC84']].forEach(([n, ms, c]) =>
      bars.appendChild(h(`<div class="bar"><span>${n}</span><i><u style="width:0;--c:${c}" data-w="${ms / 8 * 100}"></u></i><b>${ms.toFixed(1)} ms</b></div>`)));
    const spark = el.querySelector('.spark');
    const drawSpark = () => {
      const w = spark.clientWidth, hh = spark.clientHeight; if (!w) return; spark.width = w * 2; spark.height = hh * 2; const g = spark.getContext('2d'); g.scale(2, 2);
      const pts = Array.from({ length: 48 }, (_, i) => 8.3 + Math.sin(i * .7) * .25 + (i === 31 ? 3.4 : 0) + (Math.random() - .5) * .3);
      const y = v => hh - 4 - (v - 6) / 7 * (hh - 8);
      g.strokeStyle = 'rgba(255,255,255,.08)'; g.beginPath(); g.moveTo(0, y(8.33)); g.lineTo(w, y(8.33)); g.stroke();
      const grd = g.createLinearGradient(0, 0, 0, hh); grd.addColorStop(0, 'rgba(61,220,132,.25)'); grd.addColorStop(1, 'rgba(61,220,132,0)');
      const line = () => { g.beginPath(); pts.forEach((v, i) => { const x = i / 47 * w; i ? g.lineTo(x, y(v)) : g.moveTo(x, y(v)); }); };
      line(); g.lineTo(w, hh); g.lineTo(0, hh); g.fillStyle = grd; g.fill();
      line(); g.strokeStyle = '#3DDC84'; g.lineWidth = 1.5; g.stroke();
      g.fillStyle = '#F5C84B'; g.beginPath(); g.arc(31 / 47 * w, y(pts[31]), 2.5, 0, 7); g.fill();
    };

    // ---------- inspector ----------
    const tabs = el.querySelector('.insp-tabs'), content = el.querySelector('.insp-content');
    [['quality', '画质'], ['fg', '补帧'], ['color', '色彩'], ['audio', '声音'], ['display', '显示']].forEach(([id, n]) => {
      const b = h(`<button role="tab" data-tab="${id}">${n}</button>`); b.onclick = () => { st.tab = id; render(true); }; tabs.appendChild(b);
    });
    const nrLayers = () => st.chain.filter(x => x.type === 'nr');
    const label = inst => inst.type === 'nr' ? `NR 层 ${nrLayers().indexOf(inst) + 1}` : R.types[inst.type].name;
    VY.label = (app2, inst) => { const list = app2.st.chain.filter(x => x.type === 'nr'); return inst.type === 'nr' ? `NR ${list.indexOf(inst) + 1}` : R.types[inst.type].name; };

    function chainBar() {
      const box = h(`<div class="chainbar"><div class="eyebrow"><span>处理顺序</span><span>点击定位</span></div><div class="chain"></div></div>`);
      const c = box.querySelector('.chain');
      const chip = (txt, cls, fn) => { const x = h(`<button class="c ${cls || ''}">${txt}</button>`); if (fn) x.onclick = fn; else x.disabled = true; return x; };
      c.appendChild(chip('输入', 'io'));
      st.chain.forEach(inst => {
        c.appendChild(h(`<span class="arr"></span>`));
        c.appendChild(chip(inst.type === 'nr' ? `NR ${nrLayers().indexOf(inst) + 1}` : R.types[inst.type].name.replace('RTX Video ', ''), inst.on ? '' : 'off', () => focus(inst)));
      });
      return box;
    }
    function focus(inst) {
      if (inst.type === 'fg') { st.tab = 'fg'; render(true); return; }
      if (st.tab !== 'quality') { st.tab = 'quality'; render(false); }
      const stage = inst.type === 'nr' ? 'nr' : inst.id;
      st.open.add(stage); if (inst.type === 'nr') st.open.add(inst.id);
      render(false);
      const card = content.querySelector(`[data-id="${inst.id}"]`), body = content.querySelector('.insp-body');
      if (card && body) {
        const s = app.scale(); body.scrollTo({ top: body.scrollTop + (card.getBoundingClientRect().top - body.getBoundingClientRect().top) / s - 8, behavior: 'smooth' });
        card.classList.remove('flash'); void card.offsetWidth; card.classList.add('flash');
      }
    }
    // accordion: one stage (SR / protect / HDR) or one NR layer inside the NR stage
    function accordion(inst, opts = {}) {
      const T = R.types[inst.type];
      const a = h(`<div class="acc${st.open.has(inst.id) ? ' open' : ''}${inst.fresh ? ' new' : ''}${opts.layer ? ' layer' : ''}" data-id="${inst.id}">
        <div class="acc-h">${opts.layer ? `<span class="lnum">${nrLayers().indexOf(inst) + 1}</span>` : `<span class="sico" style="--c:${T.hue}">${I(T.icon)}</span>`}
          <span class="ttl"><b>${opts.layer ? 'NR 层 ' + (nrLayers().indexOf(inst) + 1) : T.name}${!opts.layer && T.exp ? '<span class="tag exp">实验</span>' : ''}</b><small>${T.summary(inst.p)}</small></span>
          <span class="acts"></span><span class="chev">${I('right')}</span></div>
        <div class="acc-b"><div><div class="inner"></div></div></div></div>`);
      delete inst.fresh;
      const acts = a.querySelector('.acts');
      if (opts.layer) {
        const more = h(`<button class="btn ghost icon" title="更多" style="width:26px;height:26px">${I('dup')}</button>`);
        more.onclick = e => { e.stopPropagation(); app.menu(more, label(inst), [{ label: '复制为新层', icon: 'dup', act: 'dup', disabled: nrLayers().length >= 4 }, { label: '恢复默认', icon: 'reset', act: 'reset' }, { label: '删除这一层', icon: 'trash', act: 'del', disabled: nrLayers().length <= 1 }], o => {
          if (o.act === 'del') { a.classList.add('bye'); setTimeout(() => { st.chain.splice(st.chain.indexOf(inst), 1); app.applyChange(true); render(false); }, 260); }
          if (o.act === 'dup') addLayer(inst);
          if (o.act === 'reset') { T.params.forEach(d => inst.p[d.k] = d.def); app.applyChange(true); render(false); }
        }, 'down'); };
        acts.appendChild(more);
      }
      acts.appendChild(sw(inst.on, v => { inst.on = v; app.applyChange(true); refreshChain(); }));
      a.querySelector('.acc-h').addEventListener('click', e => { if (e.target.closest('button')) return; a.classList.toggle('open'); a.classList.contains('open') ? st.open.add(inst.id) : st.open.delete(inst.id); });
      const inner = a.querySelector('.inner'); inner.appendChild(VY.editor(app, inst));
      if (inst.type === 'nr' && inst.p.runtime === 'RTX 30 兼容') inner.appendChild(h(`<div class="unsup">${I('info')}<span>RTX 30 兼容版需持卡验收；本机为 RTX 5070，未验证。</span></div>`));
      return a;
    }
    // NR stage: one container, layers inside; new layers are appended after the last NR layer
    function addLayer(from) {
      const layers = nrLayers(); if (layers.length >= 4) return;
      const n = VY_MK('nr', from ? Object.assign({}, from.p) : {}); n.fresh = true;
      st.chain.splice(st.chain.indexOf(layers[layers.length - 1]) + 1, 0, n);
      st.open.add(n.id); app.applyChange(true); render(false);
    }
    function nrStage() {
      const layers = nrLayers(), T = R.types.nr;
      const box = h(`<div class="stage${st.open.has('nr') ? ' open' : ''}" data-stage="nr">
        <div class="stage-h"><span class="sico" style="--c:${T.hue}">${I(T.icon)}</span><span class="ttl"><b>NR 画面增强<span class="tag exp">实验</span></b><small>${layers.length} 层依次处理 · 每层参数独立</small></span><span class="lcount">${layers.map(l => `<i class="${l.on ? '' : 'off'}"></i>`).join('')}</span><span class="chev">${I('right')}</span></div>
        <div class="acc-b"><div><div class="stage-in"></div></div></div></div>`);
      box.querySelector('.stage-h').onclick = () => { box.classList.toggle('open'); box.classList.contains('open') ? st.open.add('nr') : st.open.delete('nr'); };
      const inner = box.querySelector('.stage-in');
      layers.forEach(l => inner.appendChild(accordion(l, { layer: true })));
      const add = h(`<button class="addlayer">${I('plus')}添加 NR 层<span class="faint">（加在 NR ${layers.length} 之后 · 最多 4 层）</span></button>`);
      if (layers.length >= 4) { add.disabled = true; add.style.opacity = .45; }
      add.onclick = () => addLayer(); inner.appendChild(add);
      return box;
    }
    function refreshChain() { const old = content.querySelector('.chainbar'); if (old) old.replaceWith(chainBar()); }
    function render(animate) {
      tabs.querySelectorAll('button').forEach(b => b.setAttribute('aria-selected', b.dataset.tab === st.tab));
      const prevScroll = content.querySelector('.insp-body')?.scrollTop || 0;
      content.innerHTML = '';
      const body = h(`<div class="insp-body"></div>`);
      if (st.tab === 'quality') {
        content.appendChild(chainBar());
        st.chain.forEach((inst, i) => {
          if (inst.type === 'fg') return;
          if (inst.type === 'nr') { if (nrLayers()[0] === inst) body.appendChild(nrStage()); return; }
          body.appendChild(accordion(inst));
        });
        const fg = st.chain.find(x => x.type === 'fg');
        const tail = h(`<button class="acc tail"><div class="acc-h"><span class="sico" style="--c:#3DDC84">${I('layers')}</span><span class="ttl"><b>补帧</b><small>${fg.on ? R.types.fg.summary(fg.p) : '关闭'} · 固定在最后</small></span><span class="chev">${I('right')}</span></div></button>`);
        tail.onclick = () => { st.tab = 'fg'; render(true); };
        body.appendChild(tail);
        body.appendChild(h(`<div class="grp">不适用于当前显卡</div>`));
        body.appendChild(h(`<div class="acc na"><div class="acc-h"><span class="sico">${I('sparkles')}</span><span class="ttl"><b>FSR 4.1 超分</b><small>仅 AMD 显卡开放 · 当前为 NVIDIA RTX 5070</small></span></div></div>`));
      } else if (st.tab === 'fg') {
        const fg = st.chain.find(x => x.type === 'fg');
        const g = h(`<div class="acc open static"><div class="acc-h"><span class="sico" style="--c:#3DDC84">${I('layers')}</span><span class="ttl"><b>补帧</b><small>始终在所有画质效果之后</small></span><span class="acts"></span></div><div class="acc-b"><div><div class="inner"></div></div></div></div>`);
        g.querySelector('.acts').appendChild(sw(fg.on, v => { fg.on = v; app.applyChange(true); app.emit('status'); }));
        g.querySelector('.inner').appendChild(VY.editor(app, fg)); body.appendChild(g);
        body.querySelectorAll('details.sub').forEach(d => d.open = true);
        body.appendChild(h(`<div class="grp">说明</div>`));
        const sm = h(`<details class="sub help"><summary><span>Smooth Motion · 开启方法</span>${I('down')}</summary><div class="subin"><p>只用驱动补帧时：这里选“关闭补帧”，在 NVIDIA App 打开 AI 插帧。只用 DLSS / XeSS 时：去 NVIDIA App 关闭 AI 插帧。软件不检测、不拦截叠加，也不修改驱动配置。</p></div></details>`);
        body.appendChild(sm);
        body.appendChild(h(`<p class="faint" style="font-size:11px;margin:2px 4px">AMD FSR 补帧暂时隐藏（切换后再切回会出错）。XeSS 最高 4X，6X 仅 DLSS。</p>`));
      } else if (st.tab === 'color') {
        body.classList.add('color-body'); VY.colorPanel(app, body);
      } else if (st.tab === 'audio') {
        body.append(h(`<div class="grp">声音</div>`), app.group(app.row('同步方式', '', seg(['自动', '手动'], '自动', () => { })), app.sliderRow('手动偏移', 0, -500, 500, 10, ' ms', { w: 140, center: true }), app.row('监听设备', '', app.select('监听设备', ['[WASAPI] 采集卡音频', '[DirectShow] Elgato Audio', '不监听音频'], '[WASAPI] 采集卡音频', null, 170)), app.sliderRow('音量', 70, 0, 100, 1, '%', { w: 140 })));
      } else {
        body.append(h(`<div class="grp">显示</div>`), app.group(app.row('直播兼容模式', '给 OBS 窗口捕获用', sw(false, () => { })), app.row('原画 / 增强分屏', '', sw(false, () => { })), app.row('按住 V 查看原画', '', sw(true, () => { })), app.row('画面比例', '', seg(['适应', '原始', '填充'], '适应', () => { })), app.row('强制 SDR 预览', '', sw(false, () => { }))));
      }
      content.appendChild(body);
      body.querySelectorAll('.seg').forEach(s => requestAnimationFrame(() => s._sync(true)));
      body.scrollTop = animate ? 0 : prevScroll;
      if (animate) [...body.children].forEach((x, i) => x.animate([{ opacity: 0, transform: 'translateY(10px)' }, { opacity: 1, transform: 'none' }], { duration: 460, delay: i * 30, easing: 'cubic-bezier(.22,1.25,.36,1)', fill: 'backwards' }));
      const fg = st.chain.find(x => x.type === 'fg'); el.querySelector('[data-fgtag]').textContent = fg.on ? fg.p.mult : '补帧关';
    }
    const stEl = el.querySelector('[data-status]'), stDot = el.querySelector('.insp-foot .dot');
    const syncStatus = () => { const [k, t] = st.status; stEl.textContent = t; stDot.className = 'dot' + (k === 'ok' ? '' : ' ' + k); };
    app.on((k, d) => {
      if (!el.classList.contains('on')) return;
      if (k === 'status') syncStatus();
      if (k === 'params' && d) { const s = content.querySelector(`[data-id="${d.id}"] .ttl small`); if (s) s.textContent = R.types[d.type].summary(d.p); }
      if (k === 'rerender') render(false);
    });
    return {
      el, onShow() {
        st.proView = 'list'; vseg._pick('列表', false); requestAnimationFrame(() => vseg._sync(true));
        render(false); syncStatus();
        requestAnimationFrame(() => { drawSpark(); bars.querySelectorAll('u').forEach((u, i) => setTimeout(() => u.style.width = u.dataset.w + '%', 120 + i * 60)); });
      }
    };
  };
})();
