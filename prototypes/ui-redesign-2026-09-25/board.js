// White infinite design board: lays out the Veyra app frames and annotations, handles pan / zoom.
(function () {
  const board = document.getElementById('board'), world = document.getElementById('world');
  const W = 1280, H = 800, COLX = [0, 1440, 2880], NOTEX = 4320;
  const ROWS = [
    { y: 0, title: '首页与极简模式', sub: '极简模式专门用来看电影：窗口按影片画幅适配（21:9 就只显示 21:9，没有上下黑边），大圆角播放条一半压在画面下沿。鼠标碰到顶部弹出切换栏。' },
    { y: 1250, title: '片源窗口', sub: '首页三个入口各自的窗口。采集卡“节奏”里补上了现有的“限定输入帧率”（设备帧率）。' },
    { y: 2500, title: '字幕与音轨', sub: '播放条上字幕、音轨菜单最下面的“设置…”打开。' },
    { y: 3750, title: '专业 · 列表模式', sub: '标准链路，固定顺序。底部读数缩进窗口内，帧率区显示“显示 fps”，后面跟“提交”。补帧页按现有软件功能重排。' },
    { y: 5000, title: '预设', sub: '另存为：列出将要保存的内容再命名。管理：重命名、复制、删除、设为启动默认。列表预设与节点预设分开。' },
    { y: 6250, title: '专业 · 节点模式', sub: '独立的第二套链路和预设，切换前确认。读数显示在画面下方空位，每个节点标耗时，底部彩色条按节点拆分整条链路耗时。节点放下后会自动推开，不会挤在一起。' },
    { y: 7650, title: '导出与设置', sub: '导出只从预设里选（列表或节点预设），码率可随意填写。设置去掉了采集卡页。' }
  ];
  const FRAMES = [
    { id: 'f-home', row: 0, col: 0, label: '首页', small: 'Logo 回到这里', cfg: { page: 'home' } },
    { id: 'f-min', row: 0, col: 1, label: '极简 · 2.39:1 影片', small: '窗口按画幅适配', cfg: { page: 'min', aspect: 2.39 } },
    { id: 'f-min169', row: 0, col: 2, label: '极简 · 16:9 影片', small: '顶部切换栏已弹出', cfg: { page: 'min', aspect: 16 / 9, dockPinned: true } },
    { id: 'f-cap', row: 1, col: 0, label: '采集卡', small: '', cfg: { page: 'home', dialog: 'capture' } },
    { id: 'f-ps5', row: 1, col: 1, label: 'PS5 串流', small: '', cfg: { page: 'home', dialog: 'ps5' } },
    { id: 'f-scr', row: 1, col: 2, label: '屏幕捕获', small: '', cfg: { page: 'home', dialog: 'screen' } },
    { id: 'f-sub', row: 2, col: 0, label: '字幕设置', small: '', cfg: { page: 'min', dialog: 'subtitle' } },
    { id: 'f-aud', row: 2, col: 1, label: '音频 / 音轨设置', small: '', cfg: { page: 'min', dialog: 'audio' } },
    { id: 'f-pro', row: 3, col: 0, label: '专业 · 画质', small: '固定顺序 · NR 叠层', cfg: { page: 'pro' } },
    { id: 'f-fg', row: 3, col: 1, label: '专业 · 补帧', small: '按现有功能', cfg: { page: 'pro', tab: 'fg' } },
    { id: 'f-color', row: 3, col: 2, label: '专业 · 色彩', small: '沿用现有调色页', cfg: { page: 'pro', tab: 'color' } },
    { id: 'f-save', row: 4, col: 0, label: '另存为预设', small: '', cfg: { page: 'pro', dialog: 'savePresetList' } },
    { id: 'f-manage', row: 4, col: 1, label: '管理预设', small: '', cfg: { page: 'pro', dialog: 'managePresetsList' } },
    { id: 'f-tonode', row: 4, col: 2, label: '切换到节点模式', small: '确认弹窗', cfg: { page: 'pro', dialog: 'toNode' } },
    { id: 'f-node', row: 5, col: 0, label: '专业 · 节点', small: '实验 · 单链路', cfg: { page: 'node', proView: 'node' }, wide: true },
    { id: 'f-exp', row: 6, col: 0, label: '导出', small: '按预设导出', cfg: { page: 'exp' } },
    { id: 'f-set', row: 6, col: 1, label: '设置', small: '', cfg: { page: 'set' } }
  ];
  const NOTES = [
    { row: 0, y: 0, kind: '', title: '极简模式', body: '<ul><li>进入时画面从中间展开，窗口高度弹性过渡到影片画幅。</li><li>没有片源时画面纯黑。</li><li>播放条：左边封面和片名，中间字幕 / 后退 / 播放 / 前进 / 音轨和进度条，右边预设、音量、全屏。</li><li>去掉了性能读数。</li></ul>' },
    { row: 0, y: 380, kind: 'tech', title: '技术约束', body: '真实软件中窗口大小跟随画幅，需要在打开文件时读取宽高比再调整窗口；采集卡和游戏一般是 16:9。播放条要压在视频上，得做成独立的小窗口（同全屏控制栏）。' },
    { row: 1, y: 0, kind: '', title: '采集卡', body: '补上现有面板里有但上一版漏掉的：限定输入帧率（设备帧率，0 = 沿用格式）、转为 SDR 显示、画面上下翻转、Dolby / DTS 位流。' },
    { row: 3, y: 0, kind: '', title: '补帧页（对照现有软件）', body: '<ul><li>补帧方式只有 DLSS 帧生成、Intel XeSS · 实验；AMD FSR 补帧保持隐藏。</li><li>倍率：DLSS 最高 6X；XeSS 最高 4X。</li><li>严格补帧节奏 = 帧同步，默认关。</li><li>光流来源、AMD 性能档、运动估算质量、内容节奏。</li><li>低延迟队列、显示同步、输出上限（关闭 / 跟随显示器 / 自定义，自定义可填任意帧率）。</li><li>Smooth Motion 开启方法。</li></ul>' },
    { row: 3, y: 470, kind: '', title: '帧率读数', body: '“显示 fps”放在前面，后面跟“提交 fps”。注意：显示帧率要能取到系统显示事件才算真实值，取不到时应标“未测”，不能拿提交帧率代替。' },
    { row: 4, y: 0, kind: '', title: '预设流程', body: '<ul><li>专业页右上角“预设”菜单 → 另存为 / 管理。</li><li>另存为会先列出当前正在用的每个效果和参数摘要，再填名称，重名会提示覆盖。</li><li>管理页顶部切换列表预设 / 节点预设。内置预设只能复制，不能删改。</li></ul>' },
    { row: 5, y: 0, kind: '', title: '节点模式新增', body: '<ul><li>画面下方原来空着的位置显示：输入、输出、显示 fps、提交 fps、排队、链路总耗时。</li><li>每个节点标题栏右侧显示本节点耗时；未接入的显示“未接入”。</li><li>底部彩色条：每段颜色 = 节点颜色，长度 = 耗时，斜纹 = 剩余预算。点某段会定位到节点。</li><li>调色节点带全部参数：亮、颜色、曲线、混色器、颜色分级、校准、LUT，按组折叠。</li><li>放下节点后，被压住的节点会弹开（原来只有输出节点会）。</li></ul>' },
    { row: 5, y: 560, kind: 'tech', title: '两套链路', body: '列表和节点是两套独立配置和预设。切换需要确认，会重建处理链。切回列表时恢复列表原来的设置；节点链保留到下次。' },
    { row: 6, y: 0, kind: '', title: '导出', body: '导出不再单独设补帧或效果：直接选一个预设（列表或节点），没有合适的就先保存预设。码率可以拖滑条，也可以直接输入任意数值。' }
  ];
  const apps = [];
  const frameEls = {};
  ROWS.forEach(r => {
    const t = document.createElement('div'); t.className = 'section-title';
    t.style.left = '0px'; t.style.top = (r.y - 170) + 'px';
    t.innerHTML = `${r.title}<span>${r.sub}</span>`; world.appendChild(t);
  });
  FRAMES.forEach(f => {
    const el = document.createElement('div'); el.className = 'frame'; el.id = f.id;
    el.style.left = COLX[f.col] + 'px'; el.style.top = ROWS[f.row].y + 'px'; el.style.width = (f.wide ? 1600 : W) + 'px'; el.style.height = (f.wide ? 1150 : H) + 'px';
    el.innerHTML = `<div class="frame-label">${f.label}${f.small ? `<small>${f.small}</small>` : ''}</div>`;
    world.appendChild(el); frameEls[f.id] = el;
    const app = window.VeyraApp(el, Object.assign({}, f.cfg));
    if (f.wide) { app.root.style.width = '1600px'; app.root.style.height = '1150px'; }
    app.root.classList.add('frame-shadow'); apps.push(app);
  });
  NOTES.forEach(n => {
    const el = document.createElement('div'); el.className = 'note' + (n.kind ? ' ' + n.kind : '');
    el.style.left = NOTEX + 'px'; el.style.top = (ROWS[n.row].y + n.y) + 'px';
    el.innerHTML = `<b>${n.title}</b>${n.body}`; world.appendChild(el);
  });
  const cover = document.createElement('div'); cover.className = 'section-title';
  cover.style.left = '0px'; cover.style.top = '-420px';
  cover.innerHTML = `Veyra 新界面原型 · 第 3 版<span>极简模式改为按画幅适配的影院窗口；补帧页对照现有功能；新增预设另存为 / 管理；节点模式与列表完全分开，加上耗时读数和彩色耗时条；导出改为按预设。所有数据是示例，画面是程序生成的占位图。</span>`;
  world.appendChild(cover);

  // ---------- pan / zoom ----------
  const v = { x: 0, y: 0, k: .5 };
  const zv = document.getElementById('zoomv');
  const apply = () => { world.style.transform = `translate(${v.x}px,${v.y}px) scale(${v.k})`; board.style.backgroundPosition = `${v.x}px ${v.y}px`; board.style.backgroundSize = `${24 * v.k}px ${24 * v.k}px`; zv.textContent = Math.round(v.k * 100) + '%'; };
  const zoomAt = (k, cx, cy) => { k = Math.max(.12, Math.min(2.5, k)); v.x = cx - (cx - v.x) * k / v.k; v.y = cy - (cy - v.y) * k / v.k; v.k = k; apply(); };
  let anim;
  const animateTo = (tx, ty, tk, ms = 650) => {
    cancelAnimationFrame(anim); const s = { ...v }, t0 = performance.now();
    const ease = t => 1 - Math.pow(1 - t, 4);
    const f = now => { const t = Math.min(1, (now - t0) / ms), e = ease(t); v.x = s.x + (tx - s.x) * e; v.y = s.y + (ty - s.y) * e; v.k = s.k + (tk - s.k) * e; apply(); if (t < 1) anim = requestAnimationFrame(f); };
    anim = requestAnimationFrame(f);
  };
  const fitRect = (x, y, w, h, pad = 60, top = 110) => {
    const bw = innerWidth, bh = innerHeight; const k = Math.min(1.1, (bw - pad * 2) / w, (bh - top - pad - 70) / h);
    animateTo((bw - w * k) / 2 - x * k, top + (bh - top - 70 - h * k) / 2 - y * k, k);
  };
  const fitFrame = id => { const f = frameEls[id]; fitRect(parseFloat(f.style.left), parseFloat(f.style.top) - 40, f.offsetWidth, f.offsetHeight + 40, 40, 90); };
  const fitAll = () => fitRect(-40, -440, NOTEX + 380, ROWS[ROWS.length - 1].y + H + 480, 40, 80);

  board.addEventListener('wheel', e => {
    if (e.target.closest('.vy .canvas,.vy .insp-body,.vy .setbody,.vy .insp-mini')) {
      const sc = e.target.closest('.insp-body,.setbody,.insp-mini'); if (sc && !e.ctrlKey && sc.scrollHeight > sc.clientHeight) return;
    }
    e.preventDefault(); cancelAnimationFrame(anim);
    if (e.ctrlKey || e.metaKey) zoomAt(v.k * Math.exp(-e.deltaY * .01), e.clientX, e.clientY);
    else { v.x -= e.deltaX; v.y -= e.deltaY; apply(); }
  }, { passive: false });
  const ptrs = new Map(); let pinch = null;
  board.addEventListener('pointerdown', e => {
    if (e.target.closest('.vy')) return;
    cancelAnimationFrame(anim); ptrs.set(e.pointerId, { x: e.clientX, y: e.clientY }); board.setPointerCapture(e.pointerId); board.classList.add('panning');
    if (ptrs.size === 2) { const [a, b] = [...ptrs.values()]; pinch = { d: Math.hypot(a.x - b.x, a.y - b.y), k: v.k }; }
  });
  board.addEventListener('pointermove', e => {
    if (!ptrs.has(e.pointerId)) return; const p = ptrs.get(e.pointerId);
    if (ptrs.size === 2 && pinch) { ptrs.set(e.pointerId, { x: e.clientX, y: e.clientY }); const [a, b] = [...ptrs.values()]; zoomAt(pinch.k * Math.hypot(a.x - b.x, a.y - b.y) / pinch.d, (a.x + b.x) / 2, (a.y + b.y) / 2); return; }
    v.x += e.clientX - p.x; v.y += e.clientY - p.y; ptrs.set(e.pointerId, { x: e.clientX, y: e.clientY }); apply();
  });
  const end = e => { ptrs.delete(e.pointerId); if (ptrs.size < 2) pinch = null; if (!ptrs.size) board.classList.remove('panning'); };
  board.addEventListener('pointerup', end); board.addEventListener('pointercancel', end);

  // ---------- toolbar ----------
  document.querySelectorAll('[data-f]').forEach(b => b.onclick = () => fitFrame(b.dataset.f));
  document.querySelectorAll('[data-z]').forEach(b => b.onclick = () => { const z = b.dataset.z; if (z === 'all') return fitAll(); zoomAt(v.k * (z === '+' ? 1.25 : .8), innerWidth / 2, innerHeight / 2); });
  document.querySelectorAll('[data-acc]').forEach(b => b.onclick = () => { document.querySelectorAll('[data-acc]').forEach(x => x.setAttribute('aria-pressed', x === b)); apps.forEach(a => a.setAccent(b.dataset.acc)); });
  const mot = document.getElementById('tb-motion');
  const setMotion = on => { mot.setAttribute('aria-pressed', on); apps.forEach(a => a.setReduced(on)); };
  mot.onclick = () => setMotion(mot.getAttribute('aria-pressed') !== 'true');
  try { if (matchMedia('(prefers-reduced-motion: reduce)').matches) setMotion(true); } catch (e) { }

  apply();
  requestAnimationFrame(() => innerWidth < 900 ? fitFrame('f-min') : fitAll());
})();
