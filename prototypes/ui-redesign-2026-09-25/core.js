// Veyra prototype — shell, shared controls, dialogs.
(function () {
  const I = window.VI, R = window.VY_REG;
  const h = html => { const t = document.createElement('template'); t.innerHTML = html.trim(); return t.content.firstElementChild; };
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const fmt = (v, step) => step >= 1 ? String(Math.round(v)) : (+v).toFixed(step >= .1 ? 1 : 2);

  // ---------- procedural "game frame" placeholder (never real screenshots) ----------
  const PAL = [
    { sky: ['#0A1230', '#35295A', '#E8784A', '#FFC98A'], sun: '#FFE2B0', hills: ['#3A2A55', '#241A3A', '#15101F', '#07060A'] },
    { sky: ['#03101A', '#0B2B3A', '#2C6F7A', '#9AD4C8'], sun: '#E8FFF6', hills: ['#1B3B46', '#11262F', '#0A171D', '#040709'] },
    { sky: ['#1A1030', '#5B2F5E', '#D8577A', '#FFB08A'], sun: '#FFE6D8', hills: ['#4A2447', '#2E1631', '#1A0D1D', '#08050A'] }
  ];
  function rng(seed) { return () => { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
  function paintScene(cv, seed = 3, pal = 0) {
    const w = cv.clientWidth, hh = cv.clientHeight; if (!w || !hh) return;
    const d = Math.min(2, window.devicePixelRatio || 1);
    cv.width = Math.round(w * d); cv.height = Math.round(hh * d);
    const g = cv.getContext('2d'); g.setTransform(d, 0, 0, d, 0, 0);
    const P = PAL[pal % PAL.length], r = rng(seed);
    const sky = g.createLinearGradient(0, 0, 0, hh);
    sky.addColorStop(0, P.sky[0]); sky.addColorStop(.42, P.sky[1]); sky.addColorStop(.66, P.sky[2]); sky.addColorStop(.8, P.sky[3]);
    g.fillStyle = sky; g.fillRect(0, 0, w, hh);
    const sx = w * (.58 + r() * .2), sy = hh * .6, sr = hh * .085;
    const gl = g.createRadialGradient(sx, sy, 0, sx, sy, hh * .7);
    gl.addColorStop(0, 'rgba(255,220,170,.55)'); gl.addColorStop(1, 'rgba(255,220,170,0)');
    g.fillStyle = gl; g.fillRect(0, 0, w, hh);
    g.fillStyle = P.sun; g.beginPath(); g.arc(sx, sy, sr, 0, 7); g.fill();
    for (let i = 0; i < 40; i++) { g.fillStyle = `rgba(255,255,255,${.2 + r() * .5})`; g.fillRect(r() * w, r() * hh * .35, 1.2, 1.2); }
    P.hills.forEach((c, li) => {
      const base = hh * (.6 + li * .085), amp = hh * (.1 - li * .012), f1 = 1.5 + r() * 2, f2 = 5 + r() * 6, ph = r() * 6;
      g.fillStyle = c; g.beginPath(); g.moveTo(0, hh);
      for (let x = 0; x <= w; x += 4) { const t = x / w; g.lineTo(x, base - amp * (.6 * Math.sin(t * f1 * Math.PI + ph) + .3 * Math.sin(t * f2 * Math.PI + ph * 2) + .15 * Math.sin(t * 23 + ph))); }
      g.lineTo(w, hh); g.fill();
    });
  }
  function scene(seed, pal) {
    const cv = document.createElement('canvas'); cv.className = 'scene';
    let tm; new ResizeObserver(() => { clearTimeout(tm); tm = setTimeout(() => paintScene(cv, seed, pal), 60); }).observe(cv);
    return cv;
  }

  // ---------- controls ----------
  function slider(val, min, max, step, onInput, opt = {}) {
    const el = h(`<div class="slider${opt.center ? ' center' : ''}" role="slider" tabindex="0" aria-valuemin="${min}" aria-valuemax="${max}"><div class="track"><div class="fill"></div><div class="knob"></div></div></div>`);
    const fill = el.querySelector('.fill'), knob = el.querySelector('.knob'), track = el.querySelector('.track');
    const zero = opt.center ? (0 - min) / (max - min) : 0;
    const set = (v, fire) => {
      v = clamp(Math.round(v / step) * step, min, max); const t = (v - min) / (max - min);
      fill.style.left = Math.min(t, zero) * 100 + '%'; fill.style.width = Math.abs(t - zero) * 100 + '%';
      if (opt.center) fill.style.background = 'var(--v-accent)';
      knob.style.left = t * 100 + '%'; el.setAttribute('aria-valuenow', v); val = v; if (fire) onInput(v);
    };
    const at = e => { const r = track.getBoundingClientRect(); set(min + clamp((e.clientX - r.left) / r.width, 0, 1) * (max - min), true); };
    el.addEventListener('pointerdown', e => { e.stopPropagation(); el.setPointerCapture(e.pointerId); el.classList.add('drag'); at(e); });
    el.addEventListener('pointermove', e => { if (el.classList.contains('drag')) at(e); });
    el.addEventListener('pointerup', () => el.classList.remove('drag'));
    el.addEventListener('keydown', e => { if (e.key === 'ArrowRight' || e.key === 'ArrowUp') { set(val + step, true); e.preventDefault(); } if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') { set(val - step, true); e.preventDefault(); } });
    el.addEventListener('dblclick', () => el.dispatchEvent(new CustomEvent('reset')));
    el.set = set; set(val); return el;
  }
  function seg(opts, val, onChange) {
    const el = h(`<div class="seg"><span class="seg-ind"></span></div>`), ind = el.firstElementChild;
    opts.forEach(o => { const b = h(`<button type="button"></button>`); b.textContent = o; b.dataset.v = o; b.onclick = e => { e.stopPropagation(); pick(o, true); }; el.appendChild(b); });
    const sync = instant => {
      const b = el.querySelector('button[aria-pressed="true"]'); if (!b || !b.offsetWidth) return;
      if (instant) ind.style.transition = 'none';
      ind.style.width = b.offsetWidth + 'px'; ind.style.transform = `translateX(${b.offsetLeft - 3}px)`;
      if (instant) requestAnimationFrame(() => ind.style.transition = '');
    };
    function pick(o, fire) { el.querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', b.dataset.v === o)); sync(); if (fire && o !== val) { val = o; onChange(o); } val = o; }
    el._sync = sync; el._pick = pick; pick(val, false); requestAnimationFrame(() => sync(true)); return el;
  }
  function sw(on, onChange) {
    const el = h(`<button type="button" class="sw" role="switch" aria-checked="${!!on}"></button>`);
    el.onclick = e => { e.stopPropagation(); const v = el.getAttribute('aria-checked') !== 'true'; el.setAttribute('aria-checked', v); onChange(v); };
    return el;
  }
  const PAGES = {}, DIALOGS = {};
  window.VY = { h, I, R, clamp, fmt, scene, paintScene, slider, seg, sw, PAGES, DIALOGS };

  // ---------- app ----------
  window.VeyraApp = function (host, cfg = {}) {
    const st = Object.assign({
      page: 'min', chain: VY_DEFAULT_CHAIN(), preset: 'max', tab: 'quality', accent: 'orange', reduced: false,
      status: ['ok', '已生效'], startPage: 'home', uid: Math.random().toString(36).slice(2, 7), open: new Set()
    }, cfg);
    st.open.add(st.chain[0].id); st.open.add(st.chain[1].id); st.open.add('nr');
    const root = h(`<div class="vy">
      <div class="dock-zone"></div><span class="dock-handle"></span>
      <nav class="dock" aria-label="页面切换">
        <button class="logo" data-go="home" aria-label="首页"><img src="logo.png" alt=""></button>
        <span class="dock-sep"></span>
        <div class="dock-items" role="tablist"><span class="dock-ind"></span></div>
        <span class="dock-sep"></span>
        <span class="dock-live" title="引擎运行中"></span>
      </nav>
      <div class="pop" role="menu"></div>
    </div>`);
    const items = root.querySelector('.dock-items'), ind = root.querySelector('.dock-ind'), pop = root.querySelector('.pop'), dock = root.querySelector('.dock');
    [['min', 'tv', '极简'], ['pro', 'sliders', '专业'], ['exp', 'upload', '导出'], ['set', 'settings', '设置']].forEach(([id, ic, tip]) =>
      items.appendChild(h(`<button class="dock-btn" role="tab" data-go="${id}" aria-label="${tip}">${I(ic)}<span class="tip">${tip}</span></button>`)));
    const app = { root, st, pages: {}, cur: null };
    app.scale = () => root.getBoundingClientRect().width / root.offsetWidth || 1;
    app.rel = el => { const a = el.getBoundingClientRect(), b = root.getBoundingClientRect(), s = app.scale(); return { x: (a.left - b.left) / s, y: (a.top - b.top) / s, w: a.width / s, h: a.height / s }; };

    // top dock: slides down when the pointer touches the top edge, hides again after leaving
    let hideT;
    const openDock = () => { clearTimeout(hideT); root.classList.add('dock-open'); requestAnimationFrame(() => app.syncDock(true)); };
    const closeDock = () => { if (st.dockPinned) return; clearTimeout(hideT); hideT = setTimeout(() => root.classList.remove('dock-open'), 450); };
    root.querySelector('.dock-zone').addEventListener('pointerenter', openDock);
    root.querySelector('.dock-handle').addEventListener('pointerenter', openDock);
    dock.addEventListener('pointerenter', openDock); dock.addEventListener('pointerleave', closeDock);
    root.querySelector('.dock-zone').addEventListener('pointerleave', e => { if (!dock.contains(e.relatedTarget)) closeDock(); });
    if (st.dockPinned) root.classList.add('dock-open');

    app.syncDock = instant => {
      const key = st.page === 'node' ? 'pro' : st.page;
      const b = items.querySelector(`[data-go="${key}"]`);
      items.querySelectorAll('.dock-btn').forEach(x => x.setAttribute('aria-selected', x === b));
      root.querySelector('.logo').setAttribute('aria-selected', key === 'home');
      ind.style.opacity = b ? 1 : 0; if (!b) return;
      if (instant) ind.style.transition = 'none';
      ind.style.transform = `translateX(${b.offsetLeft}px)`;
      if (instant) requestAnimationFrame(() => requestAnimationFrame(() => ind.style.transition = ''));
    };
    app.go = (id, instant) => {
      if (!app.pages[id]) return;
      const prev = app.cur, next = app.pages[id]; if (prev === next) return;
      st.page = id; app.syncDock(instant); app.closeMenu();
      root.classList.toggle('cine', id === 'min'); if (id !== 'min') root.style.height = '';
      if (prev) { prev.el.classList.remove('on', 'enter'); if (!instant && !st.reduced) { prev.el.classList.add('leave'); setTimeout(() => prev.el.classList.remove('leave'), 220); } }
      const show = () => { next.el.classList.add('on'); if (!instant) { next.el.classList.remove('enter'); void next.el.offsetWidth; next.el.classList.add('enter'); } next.onShow && next.onShow(); next.el.querySelectorAll('.seg').forEach(s => s._sync && s._sync(true)); };
      if (prev && !instant && !st.reduced) setTimeout(show, 150); else show();
      app.cur = next;
    };
    dock.addEventListener('click', e => { const b = e.target.closest('[data-go]'); if (!b) return; const id = b.dataset.go; app.go(id === 'pro' && st.proView === 'node' ? 'node' : id); });

    // status + events
    const listeners = []; let stT;
    app.on = fn => listeners.push(fn);
    app.emit = (k, d) => listeners.forEach(fn => fn(k, d));
    app.setStatus = (k, t) => { st.status = [k, t]; app.emit('status'); };
    app.applyChange = rebuild => { clearTimeout(stT); if (rebuild) { app.setStatus('warn', '应用中…'); stT = setTimeout(() => app.setStatus('ok', '已生效'), 750); } else app.setStatus('ok', '实时生效'); };

    // popover menu
    function closeMenu() { pop.classList.remove('open'); pop._anchor = null; }
    app.closeMenu = closeMenu;
    app.menu = (anchor, title, list, onPick, place = 'up') => {
      if (pop._anchor === anchor) return closeMenu();
      pop.innerHTML = (title ? `<h6>${title}</h6>` : '') + list.map((o, i) => o.sep ? '<hr>' : o.head ? `<h6>${o.head}</h6>` : `<button class="opt" data-i="${i}" role="menuitemradio" aria-checked="${!!o.checked}" ${o.disabled ? 'disabled' : ''}>${o.icon ? I(o.icon) : ''}<span>${o.label}${o.note ? `<small>${o.note}</small>` : ''}</span>${o.tag || ''}<span class="chk">${I('check')}</span></button>`).join('');
      pop.querySelectorAll('.opt').forEach(b => b.onclick = e => { e.stopPropagation(); const o = list[+b.dataset.i]; pop.querySelectorAll('.opt').forEach(x => x.setAttribute('aria-checked', x === b)); setTimeout(() => { closeMenu(); onPick(o); }, 130); });
      const r = app.rel(anchor); pop.style.minWidth = Math.max(220, r.w) + 'px';
      pop.classList.remove('open'); pop.style.left = '0'; pop.style.top = '0';
      const pw = pop.offsetWidth, ph = pop.offsetHeight;
      const x = clamp(r.x + r.w / 2 - pw / 2, 8, root.offsetWidth - pw - 8);
      let y = place === 'up' ? r.y - ph - 8 : r.y + r.h + 8;
      if (y < 8) y = r.y + r.h + 8; if (y + ph > root.offsetHeight - 8) y = Math.max(8, r.y - ph - 8);
      pop.style.left = x + 'px'; pop.style.top = y + 'px';
      pop.style.transformOrigin = `${r.x + r.w / 2 - x}px ${y < r.y ? '100%' : '0%'}`;
      pop._anchor = anchor; requestAnimationFrame(() => pop.classList.add('open'));
    };
    app.select = (label, opts, cur, onPick, minW) => {
      const b = h(`<button type="button" class="select"><span></span>${I('down')}</button>`); b.firstElementChild.textContent = cur;
      if (minW) b.style.minWidth = minW + 'px';
      b.onclick = e => { e.stopPropagation(); app.menu(b, label, opts.map(o => typeof o === 'string' ? { label: o, value: o, checked: o === cur } : Object.assign({ value: o.label, checked: o.label === cur }, o)), o => { cur = o.value; b.firstElementChild.textContent = o.value; onPick && onPick(o.value, o); }, 'down'); };
      return b;
    };
    root.addEventListener('pointerdown', e => { if (pop.classList.contains('open') && !pop.contains(e.target) && !(pop._anchor && pop._anchor.contains(e.target))) closeMenu(); });

    // modal dialogs (a separate native popup window in the real build)
    app.dialog = key => {
      const spec = DIALOGS[key](app); app.closeMenu();
      const scrim = h(`<div class="scrim"><div class="dlg" role="dialog" aria-modal="true">
        <div class="dlg-h"><span class="ico">${I(spec.icon)}</span><div class="t"><div class="h2">${spec.title}</div>${spec.sub ? `<small>${spec.sub}</small>` : ''}</div><button class="btn ghost icon" data-x aria-label="关闭">${I('x')}</button></div>
        <div class="dlg-b"></div><div class="dlg-f"><span class="faint" style="font-size:12px">${spec.foot || ''}</span><span class="sp"></span></div></div></div>`);
      if (spec.width) scrim.firstElementChild.style.width = spec.width + 'px';
      scrim.querySelector('.dlg-b').appendChild(spec.body);
      const close = () => { scrim.classList.remove('open'); setTimeout(() => scrim.remove(), 250); };
      (spec.actions || []).forEach(a => { const b = h(`<button class="btn ${a.primary ? 'primary' : ''}">${a.icon ? I(a.icon) : ''}${a.label}</button>`); b.onclick = () => { a.run && a.run(); if (a.close !== false) close(); }; scrim.querySelector('.dlg-f').appendChild(b); });
      scrim.querySelector('[data-x]').onclick = close;
      scrim.addEventListener('pointerdown', e => { if (e.target === scrim) close(); });
      root.appendChild(scrim); requestAnimationFrame(() => { scrim.classList.add('open'); scrim.querySelectorAll('.seg').forEach(s => s._sync && s._sync(true)); });
      return close;
    };
    // helpers for dialog/setting rows
    app.row = (lab, sub, ...ctl) => { const r = h(`<div class="row"><span class="lab">${lab}${sub ? `<small>${sub}</small>` : ''}</span></div>`); ctl.forEach(c => c && r.appendChild(typeof c === 'string' ? h(c) : c)); return r; };
    app.group = (...rows) => { const g = h(`<div class="dgroup"></div>`); rows.forEach(r => g.appendChild(r)); return g; };
    app.sec = t => h(`<div class="dsec">${t}</div>`);
    app.sliderRow = (lab, v, min, max, step, unit = '', opt = {}) => {
      const out = h(`<span class="val">${fmt(v, step)}${unit}</span>`);
      const s = slider(v, min, max, step, nv => out.textContent = fmt(nv, step) + unit, opt); s.style.width = (opt.w || 170) + 'px';
      return app.row(lab, opt.sub, s, out);
    };

    host.appendChild(root);
    Object.keys(PAGES).forEach(k => { const pg = PAGES[k](app); pg.el.classList.add('page'); root.insertBefore(pg.el, root.querySelector('.dock-zone')); app.pages[k] = pg; });
    app.setDockPinned = v => { st.dockPinned = v; root.classList.toggle('dock-open', v); requestAnimationFrame(() => app.syncDock(true)); };
    app.setAccent = a => { st.accent = a; root.classList.toggle('accent-white', a === 'white'); };
    app.setReduced = v => { st.reduced = v; root.classList.toggle('reduced', v); };
    app.setAccent(st.accent); app.setReduced(st.reduced);
    app.go(st.page, true);
    requestAnimationFrame(() => app.syncDock(true));
    if (st.dialog) requestAnimationFrame(() => app.dialog(st.dialog));
    return app;
  };
})();
