// Pages: 首页 (home) and 极简 (min)
(function () {
  const { h, I, R, scene, slider, PAGES } = window.VY;

  // ======================= 首页（点 Logo 回到这里） =======================
  PAGES.home = app => {
    const el = h(`<section class="home" aria-label="首页">
      <img class="mark" src="logo.png" alt="" data-in>
      <div class="hello" data-in style="--d:1"><div class="h1">今天看点什么？</div><div class="muted">选一个片源开始，画质预设和补帧随时在播放栏切换</div></div>
      <div class="srcgrid">
        <button class="srccard" data-in style="--d:2" data-src="file"><span class="ico">${I('folder')}</span><span><b>打开视频</b><small>MP4 · MKV · 图片</small></span></button>
        <button class="srccard" data-in style="--d:3" data-dlg="capture"><span class="ico">${I('video')}</span><span><b>采集卡</b><small>Elgato 4K60 Pro MK.2</small></span></button>
        <button class="srccard" data-in style="--d:4" data-dlg="ps5"><span class="ico">${I('gamepad')}</span><span><b>PS5 串流</b><small>已保存 1 台主机</small></span></button>
        <button class="srccard" data-in style="--d:5" data-dlg="screen"><span class="ico">${I('monitor')}</span><span><b>屏幕捕获</b><small>窗口或显示器</small></span></button>
      </div>
      <div class="resume" data-in style="--d:6"><span class="rico">${I('gamepad')}</span>
        <div class="t"><b>继续上次游戏</b><small>Elgato 4K60 Pro MK.2 · 1080p60 · YUY2 · 预设「极致」</small></div><button class="btn primary" data-resume>${I('play')}开始</button></div>
      <div class="recent" data-in style="--d:7"><span class="faint">最近</span><span class="chip" data-src="file">${I('film')}Dune.Part.Two.2160p.mkv</span><span class="chip" data-src="file">${I('film')}赛博朋克实录_0918.mp4</span><span class="chip">${I('image')}截图_夜景.png</span></div>
    </section>`);
    el.querySelectorAll('[data-src],[data-resume]').forEach(b => b.onclick = () => app.go('min'));
    el.querySelectorAll('[data-dlg]').forEach(b => b.onclick = () => app.dialog(b.dataset.dlg));
    return { el };
  };

  // ======================= 极简 =======================
  // The window snaps to the film's aspect ratio (no letterbox bars); a large rounded control pill
  // straddles the bottom edge of the picture. Pure black when nothing is loaded.
  PAGES.min = app => {
    const st = app.st;
    const el = h(`<section class="min" aria-label="极简模式">
      <div class="stage"><div class="screen"><div class="video"></div></div></div>
      <div class="cine-bar">
        <div class="cb-left"><span class="art"></span><span class="meta"><b>沙丘：第二部</b><small>Dune.Part.Two.2160p.mkv</small></span></div>
        <div class="cb-mid">
          <div class="cb-btns">
            <button class="cbtn" data-cc title="字幕">${I('cc')}</button>
            <button class="cbtn" title="后退 10 秒"><svg class="i" viewBox="0 0 24 24"><path d="M11 17l-5-5 5-5"/><path d="M18 17l-5-5 5-5"/></svg></button>
            <button class="play" data-state="pause" aria-label="播放/暂停"><svg viewBox="0 0 24 24" class="pi pi-play"><polygon points="7 4 20 12 7 20"/></svg><svg viewBox="0 0 24 24" class="pi pi-pause"><rect x="6" y="4" width="4.5" height="16" rx="1.2"/><rect x="13.5" y="4" width="4.5" height="16" rx="1.2"/></svg></button>
            <button class="cbtn" title="前进 10 秒"><svg class="i" viewBox="0 0 24 24"><path d="M13 17l5-5-5-5"/><path d="M6 17l5-5-5-5"/></svg></button>
            <button class="cbtn" data-audio title="音轨">${I('music')}</button>
          </div>
          <div class="seekrow"><span>1:12:40</span><div class="seek"><div class="rail"><div class="buf"></div><div class="played"></div></div><div class="thumb"></div><div class="peek"><span>1:12:40</span></div></div><span>2:46:10</span></div>
        </div>
        <div class="cb-right">
          <button class="pill" data-preset><span class="dot"></span><b data-pv></b></button>
          <div class="vol">${I('vol')}</div>
          <button class="cbtn" title="全屏">${I('max')}</button>
        </div>
      </div>
    </section>`);
    el.querySelector('.video').appendChild(scene(7, 0));
    el.querySelector('.art').appendChild(scene(7, 0));
    el.querySelector('.peek').prepend(scene(11, 2));
    el.querySelector('.vol').appendChild(slider(.7, 0, 1, .01, () => { }));
    const play = el.querySelector('.play'); play.onclick = () => play.dataset.state = play.dataset.state === 'pause' ? 'play' : 'pause';
    const pv = el.querySelector('[data-pv]');
    const sync = () => {
      pv.textContent = R.presets.find(p => p.id === st.preset)?.name || '自定义';
      const [k, t] = st.status; const d = el.querySelector('.pill .dot'); d.className = 'dot' + (k === 'ok' ? '' : ' ' + k); d.title = t;
    };
    const presetMenu = (anchor) => app.menu(anchor, '预设', [
      { head: '列表预设' }, ...R.presets.filter(p => p.kind === 'list').map(p => ({ label: p.name, note: p.note, id: p.id, checked: p.id === st.preset })),
      { head: '节点预设' }, ...R.presets.filter(p => p.kind === 'node').map(p => ({ label: p.name, note: p.note, id: p.id, checked: p.id === st.preset, tag: '<span class="tag">节点</span>' })),
      { sep: true }, { label: '去专业模式管理预设…', icon: 'sliders', id: '__pro' }
    ], o => { if (o.id === '__pro') return app.go('pro'); st.preset = o.id; app.applyChange(true); sync(); });
    el.querySelector('[data-preset]').onclick = e => presetMenu(e.currentTarget);
    el.querySelector('[data-cc]').onclick = e => app.menu(e.currentTarget, '字幕', [{ label: '关闭' }, { label: '简体中文 · 内嵌 ASS', checked: true }, { label: 'English · 内嵌 SRT' }, { label: '加载外部字幕…', icon: 'import' }, { sep: true }, { label: '字幕设置…', note: '字体、字号、描边、位置、延时', icon: 'type', act: 'dlg' }], o => { if (o.act) app.dialog('subtitle'); });
    el.querySelector('[data-audio]').onclick = e => app.menu(e.currentTarget, '音轨', [{ label: 'TrueHD 7.1 · 英语', checked: true }, { label: 'AC-3 5.1 · 国语' }, { label: 'AAC 2.0 · 导演评论' }, { sep: true }, { label: '音频设置…', note: '输出设备、音画同步、偏移', icon: 'music', act: 'dlg' }], o => { if (o.act) app.dialog('audio'); });
    // aspect: the whole window snaps to the film (default 2.39:1). Height = picture + the lower half of the control pill.
    const BAR_BELOW = 46;
    const fitAspect = () => {
      const ar = st.aspect || 2.39, W = app.root.offsetWidth;
      app.root.style.height = Math.round(W / ar + BAR_BELOW) + 'px';
      el.style.setProperty('--pic-h', Math.round(W / ar) + 'px');
    };
    app.setAspect = ar => { st.aspect = ar; if (el.classList.contains('on')) fitAspect(); };
    app.on(k => { if (k === 'status' && el.classList.contains('on')) sync(); });
    return {
      el, onShow() {
        sync();
        requestAnimationFrame(() => requestAnimationFrame(fitAspect));
      }
    };
  };
})();
