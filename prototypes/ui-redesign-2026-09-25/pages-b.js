// Pages: 导出 (exp), 设置 (set)
(function () {
  const { h, I, R, scene, seg, sw, slider, PAGES, clamp } = window.VY;

  // ======================= 导出 =======================
  PAGES.exp = app => {
    const el = h(`<section class="exp" aria-label="导出">
      <header class="pro-head" data-in style="--d:0"><span class="h2">导出</span><span class="faint">按所选预设导出（列表或节点预设），导出期间可继续观看</span><span class="sp"></span><button class="btn">${I('plus')}添加文件</button></header>
      <div class="card queue" data-in style="--d:1"><span class="eyebrow" style="padding:4px 4px 6px">队列 · 3</span></div>
      <div class="video" data-in style="--d:2"><div class="compare"></div></div>
      <div class="card trim" data-in style="--d:3"><div style="display:flex;justify-content:space-between;font-size:12px"><span class="muted">导出范围</span><span class="mono">00:12:30 — 01:56:20 · 1:43:50</span></div><div class="trimrail"><canvas></canvas><div class="win"></div></div></div>
      <aside class="card outset" data-in style="--d:2">
        <span class="h3">输出设置</span>
        <div class="row"><span class="lab">类型</span><span data-s1></span></div>
        <div class="row"><span class="lab">编码</span><span data-s2></span></div>
        <div class="row"><span class="lab">分辨率</span><span data-s3></span></div>
        <div class="row"><span class="lab">码率</span><span data-rate></span></div>
        <div class="row"><span class="lab">码率控制</span><span data-s5></span></div>
        <div class="row"><span class="lab">增强预设</span><span data-preset></span></div>
        <div class="pinfo" data-pinfo></div>
        <div class="row"><span class="lab">音轨</span><span class="muted" style="font-size:12px">原样复制 · TrueHD 7.1</span></div>
        <div class="row"><span class="lab">保存到</span><button class="select"><span>D:\\Veyra 导出</span>${I('folder')}</button></div>
        <div class="go"><div style="display:flex;justify-content:space-between;font-size:12px"><span class="muted" data-eta>预计 38 分钟 · 约 9.8 GB</span><span class="mono" data-pct></span></div><div class="big-prog"><u></u></div><button class="btn primary" data-start style="height:42px;justify-content:center">${I('upload')}开始导出</button></div>
      </aside></section>`);
    const q = el.querySelector('.queue');
    [['Dune.Part.Two.2160p.mkv', '4K HEVC · 2:46:10', 0, 1, 'sel'], ['赛博朋克实录_0918.mp4', '1080p H.264 · 0:18:41', 100, 2, ''], ['截图_夜景.png', 'PNG · 1920×1080', 100, 0, '']].forEach(([n, m, p, pal, c]) => {
      const it = h(`<div class="qitem ${c}"><div style="position:relative;width:64px;height:40px;border-radius:7px;overflow:hidden;flex:none"></div><div class="m"><b>${n}</b><small>${m}${p === 100 ? ' · 已完成' : ''}</small><div class="qprog"><u style="width:${p}%"></u></div></div></div>`);
      it.firstElementChild.appendChild(scene(5 + pal, pal)); it.onclick = () => { q.querySelectorAll('.qitem').forEach(x => x.classList.toggle('sel', x === it)); }; q.appendChild(it);
    });
    const cmp = el.querySelector('.compare'); const a = scene(9, 1), b = scene(9, 1); b.style.filter = 'contrast(1.12) saturate(1.2)'; a.style.filter = 'blur(1.4px) saturate(.85)';
    const clip = h(`<div style="position:absolute;inset:0;clip-path:inset(0 50% 0 0)"></div>`); clip.appendChild(a); cmp.append(b, clip, h(`<span class="lb" style="left:10px">原画</span>`), h(`<span class="lb" style="right:10px">增强后（预览）</span>`));
    const cut = h(`<div class="cut"></div>`); cmp.appendChild(cut);
    cut.addEventListener('pointerdown', e => { e.stopPropagation(); cut.setPointerCapture(e.pointerId); const mv = ev => { const r = cmp.getBoundingClientRect(); const t = clamp((ev.clientX - r.left) / r.width, .05, .95); cut.style.left = t * 100 + '%'; clip.style.clipPath = `inset(0 ${100 - t * 100}% 0 0)`; }; cut.addEventListener('pointermove', mv); cut.addEventListener('pointerup', () => cut.removeEventListener('pointermove', mv), { once: true }); });
    el.querySelector('[data-s1]').appendChild(seg(['视频', '图片'], '视频', () => { }));
    el.querySelector('[data-s2]').appendChild(seg(['HEVC', 'H.264'], 'HEVC', () => { }));
    el.querySelector('[data-s3]').appendChild(seg(['源', '2K', '4K'], '4K', () => { }));
    el.querySelector('[data-s5]').appendChild(seg(['CBR', 'VBR', '恒定质量'], 'VBR', () => { }));
    // bitrate: free number + slider, any value
    const rate = h(`<span class="ratebox"><input class="field mono" id="exp-rate-${app.st.uid}" aria-label="码率" value="45"><span class="faint">Mbps</span></span>`);
    const rs = VY.slider(45, 1, 400, 1, v => rate.querySelector('input').value = v); rs.style.width = '110px'; rate.prepend(rs);
    rate.querySelector('input').onchange = e => { const v = Math.max(1, Math.min(2000, +e.target.value || 45)); e.target.value = v; rs.set(Math.min(400, v)); };
    el.querySelector('[data-rate]').appendChild(rate);
    // export uses a saved preset (list or node), nothing else
    const pinfo = el.querySelector('[data-pinfo]');
    const showPreset = id => { const p = R.presets.find(x => x.id === id); pinfo.innerHTML = `<span class="tag${p.kind === 'node' ? ' exp' : ''}">${p.kind === 'node' ? '节点预设' : '列表预设'}</span><span>${p.note}</span>`; };
    const opts = [{ head: '列表预设' }, ...R.presets.filter(p => p.kind === 'list').map(p => ({ label: p.name, note: p.note, id: p.id })), { head: '节点预设' }, ...R.presets.filter(p => p.kind === 'node').map(p => ({ label: p.name, note: p.note, id: p.id })), { sep: true }, { label: '保存当前设置为预设…', id: '__save' }];
    el.querySelector('[data-preset]').appendChild(app.select('导出使用的预设（先在专业模式保存预设）', opts, '极致', (v, o) => { if (o.id === '__save') return app.dialog('savePresetList'); showPreset(o.id); }, 170));
    showPreset('max');
    const trimCv = el.querySelector('.trimrail canvas');
    const btn = el.querySelector('[data-start]'), bar = el.querySelector('.big-prog u'), pct = el.querySelector('[data-pct]'); let tm;
    btn.onclick = () => {
      if (tm) { clearInterval(tm); tm = null; btn.innerHTML = `${I('upload')}继续导出`; btn.classList.add('primary'); return; }
      btn.classList.remove('primary'); btn.innerHTML = `${I('pause')}暂停`; let p = parseFloat(bar.style.width) || 0;
      tm = setInterval(() => { p = Math.min(100, p + .6); bar.style.width = p + '%'; pct.textContent = p.toFixed(1) + '%'; const u = q.querySelector('.qitem.sel .qprog u'); if (u) u.style.width = p + '%'; if (p >= 100) { clearInterval(tm); tm = null; btn.innerHTML = `${I('check')}已完成`; } }, 60);
    };
    return { el, onShow() { requestAnimationFrame(() => { const w = trimCv.clientWidth, hh = trimCv.clientHeight; trimCv.width = w * 2; trimCv.height = hh * 2; const g = trimCv.getContext('2d'); g.scale(2, 2); for (let i = 0; i < w; i += 36) { const c = document.createElement('canvas'); c.style.width = '36px'; c.style.height = hh + 'px'; } const grd = g.createLinearGradient(0, 0, w, 0);['#35295A', '#E8784A', '#0B2B3A', '#5B2F5E', '#2C6F7A', '#D8577A'].forEach((c, i, arr) => grd.addColorStop(i / (arr.length - 1), c)); g.fillStyle = grd; g.fillRect(0, 0, w, hh); for (let x = 0; x < w; x += 36) { g.fillStyle = 'rgba(0,0,0,.35)'; g.fillRect(x, 0, 1, hh); } }); } };
  };

  // ======================= 设置 =======================
  PAGES.set = app => {
    const st = app.st;
    const el = h(`<section class="set" aria-label="设置">
      <nav class="card setnav" data-in style="--d:0"><span class="h2">设置</span></nav>
      <div class="card setbody" data-in style="--d:1"></div></section>`);
    const nav = el.querySelector('.setnav'), body = el.querySelector('.setbody');
    const SECS = [['look', 'home', '通用与外观'], ['play', 'play', '播放'], ['ps5', 'gamepad', 'PS5 串流'], ['keys', 'zap', '快捷键'], ['comp', 'box', '组件与许可'], ['about', 'info', '关于']];
    let cur = 'look';
    SECS.forEach(([id, ic, n]) => { const b = h(`<button role="tab" data-s="${id}">${I(ic)}${n}</button>`); b.onclick = () => { cur = id; render(true); }; nav.appendChild(b); });
    const row = (lab, sub, ctl) => { const r = h(`<div class="row"><span class="lab">${lab}${sub ? `<small>${sub}</small>` : ''}</span></div>`); if (ctl) r.appendChild(typeof ctl === 'string' ? h(ctl) : ctl); return r; };
    function render(anim) {
      nav.querySelectorAll('button').forEach(b => b.setAttribute('aria-selected', b.dataset.s === cur));
      body.innerHTML = ''; const s = h(`<section></section>`); body.appendChild(s);
      if (cur === 'look') {
        s.append(h(`<div class="h1">通用与外观</div>`), h(`<p class="lead">界面随时可调，不影响播放和增强设置。</p>`));
        const g = h(`<div class="sgroup"></div>`);
        g.appendChild(row('页面切换栏', '平时隐藏；鼠标碰到窗口顶部时弹下来', seg(['自动隐藏', '始终显示'], st.dockPinned ? '始终显示' : '自动隐藏', v => app.setDockPinned(v === '始终显示'))));
        const sws = h(`<div style="display:flex;gap:10px"></div>`);
        [['orange', '#FF8A3D'], ['white', '#F3F3F5']].forEach(([id, c]) => { const b = h(`<button type="button" class="swatch" title="${id === 'orange' ? '橙色' : '白色光晕'}" style="background:${c}" aria-pressed="${st.accent === id}"></button>`); b.onclick = () => { sws.querySelectorAll('.swatch').forEach(x => x.setAttribute('aria-pressed', x === b)); app.setAccent(id); }; sws.appendChild(b); });
        g.appendChild(row('强调色', '开关、进度、选中状态', sws));
        g.appendChild(row('减少动画', '关闭弹性与转场，播放时 UI 本来就保持静止', sw(st.reduced, v => app.setReduced(v))));
        g.appendChild(row('背景渐变强度', '深黑底上的极轻渐变与抖动（防色带）', seg(['无', '轻', '中'], '轻', () => { })));
        g.appendChild(row('界面缩放', '跟随 Windows 显示缩放', seg(['自动', '100%', '125%', '150%'], '自动', () => { })));
        s.appendChild(g);
        const g2 = h(`<div class="sgroup"></div>`);
        g2.appendChild(row('打开时的默认页面', '首页 = 选择片源的页面（点顶部 Logo 也能回到这里）', seg(['首页', '极简模式', '专业模式', '上次'], '首页', () => app.applyChange(false))));
                g2.appendChild(row('界面语言', '', '<button class="select"><span>简体中文</span>' + I('down') + '</button>'));
        s.appendChild(g2);
      } else if (cur === 'play') {
        s.append(h(`<div class="h1">播放</div>`), h(`<p class="lead">文件播放与字幕的默认行为。</p>`));
        const g = h(`<div class="sgroup"></div>`);
        g.appendChild(row('解码', '自动优先硬解，失败回退软解', seg(['自动', '硬解', '软解'], '自动', () => { })));
        g.appendChild(row('记住播放位置', '', sw(true, () => { })));
        g.appendChild(row('字幕默认字号', '双行不自动缩小', seg(['小', '中', '大'], '中', () => { })));
        g.appendChild(row('截图保存位置', 'SDR 存 PNG，HDR 存 JPEG XR', '<button class="select"><span>图片\\Veyra Screenshots</span>' + I('folder') + '</button>'));
        s.appendChild(g);
      } else if (cur === 'cap') {
        s.append(h(`<div class="h1">采集卡</div>`), h(`<p class="lead">这里是默认值，当前连接在片源面板中调整。</p>`));
        const g = h(`<div class="sgroup"></div>`);
        g.appendChild(row('默认设备', '', '<button class="select"><span>Elgato 4K60 Pro MK.2</span>' + I('down') + '</button>'));
        g.appendChild(row('像素格式优先', '', seg(['YUY2', 'NV12', 'RGB24'], 'YUY2', () => { })));
        g.appendChild(row('输入色彩', '', seg(['自动', 'BT.709', 'BT.2020'], '自动', () => { })));
        g.appendChild(row('音频监听', '只连接明确选中的输入', '<button class="select"><span>[WASAPI] 采集卡音频</span>' + I('down') + '</button>'));
        g.appendChild(row('启动后自动继续上次采集', '', sw(false, () => { })));
        s.appendChild(g);
      } else if (cur === 'ps5') {
        s.append(h(`<div class="h1">PS5 串流</div>`), h(`<p class="lead">主机与 PSN 凭据加密保存在 %LOCALAPPDATA%\\Veyra\\remoteplay。</p>`));
        const g = h(`<div class="sgroup"></div>`);
        g.appendChild(row('已保存主机', 'PS5-8F21 · 192.168.1.42', '<button class="btn">管理</button>'));
        g.appendChild(row('编码', '', seg(['H.264', 'H.265'], 'H.265', () => { })));
        g.appendChild(row('请求码率', '主机不保证达到', '<span class="val" style="min-width:70px">40 Mbps</span>'));
        g.appendChild(row('PSN 账号', '已登录', '<button class="btn ghost">退出</button>'));
        s.appendChild(g);
      } else if (cur === 'keys') {
        s.append(h(`<div class="h1">快捷键</div>`), h(`<p class="lead">点击右侧按键可重新绑定。</p>`));
        const g = h(`<div class="sgroup"></div>`);
        [['播放 / 暂停', 'Space'], ['全屏', 'F'], ['锁定全屏控制条', 'Ctrl + L'], ['按住查看原画', 'V'], ['截图', 'Ctrl + S'], ['切换极简 / 专业', 'Tab']].forEach(([a, k]) => g.appendChild(row(a, '', `<span class="tag mono" style="height:26px;padding:0 10px">${k}</span>`)));
        s.appendChild(g);
      } else if (cur === 'comp') {
        s.append(h(`<div class="h1">组件与许可</div>`), h(`<p class="lead">运行组件均为实验运行时，非 NVIDIA 官方合作或认证。详细哈希见 release-runtime-manifest.json。</p>`));
        const g = h(`<div class="sgroup comp-list"></div>`);
        [['nvngx_dlssnr.dll', 'NVIDIA 原版 · 310.8.0.0 · 签名有效', 'ok'], ['nvngx_dlssnr.dll（社区）', '310.8.0.0 · HashMismatch · 社区修改版', 'exp'], ['nvngx_truehdr.dll', 'RTX Video SDK 1.1.0 · 签名有效', 'ok'], ['FFmpeg（PS5 slice 补丁）', 'n9.0.1 · LGPL · 附对应源码', 'ok'], ['Qt 6', 'LGPLv3 · 动态链接', 'ok']].forEach(([n, d, k]) => g.appendChild(row(n, d, `<span class="tag ${k === 'exp' ? 'exp' : ''}">${k === 'exp' ? '实验' : '已加载'}</span>`)));
        s.appendChild(g);
      } else {
        s.append(h(`<div style="display:flex;align-items:center;gap:18px;margin-bottom:20px"><img src="logo.png" alt="" style="width:88px;filter:drop-shadow(0 0 14px rgba(255,255,255,.4))"><div><div class="h1">Veyra</div><div class="muted">界面原型 · 基于 1.4.4 功能</div></div></div>`));
        const g = h(`<div class="sgroup"></div>`);
        g.appendChild(row('检查更新', 'GitHub · Likely7/Veyra-NRVideo', '<button class="btn">检查</button>'));
        g.appendChild(row('反馈问题', '附带诊断信息', '<button class="btn">打开</button>'));
        g.appendChild(row('交流群与赞助', '', '<button class="btn">查看二维码</button>'));
        s.appendChild(g);
      }
      s.querySelectorAll('.seg').forEach(x => requestAnimationFrame(() => x._sync(true)));
      if (anim) [...s.children].forEach((x, i) => x.animate([{ opacity: 0, transform: 'translateY(12px)' }, { opacity: 1, transform: 'none' }], { duration: 480, delay: i * 40, easing: 'cubic-bezier(.22,1.25,.36,1)', fill: 'backwards' }));
    }
    return { el, onShow() { render(false); } };
  };
})();
