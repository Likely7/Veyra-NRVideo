// Dialogs: 采集卡 / PS5 串流 / 屏幕捕获 / 字幕设置 / 音频设置 — fields follow the current Win32 panels.
(function () {
  const { h, I, seg, sw, slider, scene, DIALOGS } = window.VY;

  DIALOGS.capture = app => {
    const b = h(`<div></div>`);
    const prev = h(`<div class="cap-prev"><div class="video"></div><div class="cap-meta"><span class="tag ok">信号正常</span><span class="mono">1920×1080 · 59.94 fps · YUY2</span></div></div>`);
    prev.querySelector('.video').appendChild(scene(21, 1));
    b.appendChild(prev);
    const devRow = app.row('视频输入设备', '', app.select('视频输入设备', ['Elgato 4K60 Pro MK.2', 'USB3.0 HD Video Capture', 'OBS Virtual Camera'], 'Elgato 4K60 Pro MK.2', null, 230), h(`<button class="btn ghost icon" title="刷新设备">${I('refresh')}</button>`));
    b.append(app.sec('设备'), app.group(devRow));
    b.append(app.sec('格式（重连生效）'), app.group(
      app.row('设备实际支持的格式', '按设备上报列出', app.select('格式', ['1080p · 60 fps · YUY2', '1080p · 60 fps · NV12', '1080p · 30 fps · YUY2', '2160p · 30 fps · NV12', '2160p · 60 fps · P010'], '1080p · 60 fps · YUY2', null, 230)),
      app.row('输入色彩空间', '', app.select('色彩', ['自动 · 设备元数据', 'Rec.709 · SDR', 'Rec.2100 PQ · HDR10', 'Rec.2100 HLG'], '自动 · 设备元数据', null, 200)),
      app.row('色彩范围', '', seg(['自动', '有限 / Limited', '完整 / Full'], '自动', () => { })),
      app.row('设备缓冲', '变更需重连', seg(['自动', '最小 1 帧', '驱动默认'], '自动', () => { })),
      app.row('转为 SDR 显示', '收到 HDR 也按 SDR 预览，立即生效', sw(false, () => { })),
      app.row('画面上下翻转', '采集画面倒置时勾选，立即生效', sw(false, () => { }))
    ));
    b.append(app.sec('音频'), app.group(
      app.row('音频监听', '只连接明确选中的输入，不会自动切到麦克风', app.select('音频', [{ head: 'DirectShow' }, { label: '[DirectShow] Elgato 4K60 Pro MK.2 Audio' }, { head: 'WASAPI' }, { label: '[WASAPI] 采集卡音频 (Elgato)' }, { label: '[WASAPI] 麦克风阵列' }, { sep: true }, { label: '不监听音频' }], '[WASAPI] 采集卡音频 (Elgato)', null, 260)),
      app.row('尝试视频设备内置音频', '', sw(false, () => { })),
      app.row('Dolby / DTS 位流', '', app.select('位流', ['自动：优先 PCM', '强制线性 PCM', '位流优先：直通给功放'], '自动：优先 PCM', null, 180))
    ));
    b.append(app.sec('节奏'), app.group(
      app.row('限定输入帧率', '直接请求采集卡按此帧率送帧；0 = 沿用所选格式，支持小数，需重连', h(`<span class="ratebox"><input class="field mono" id="cap-fps-${app.st.uid}" aria-label="限定输入帧率" value="0" style="width:84px;text-align:right"><span class="faint">FPS</span></span>`)),
      app.row('60Hz 输入按 30fps 处理', '主机游戏 30 帧、采集卡输出 60 帧时开启', sw(false, () => { }))));

    return { icon: 'video', title: '采集卡', sub: '连接设备后，先关闭增强确认基础画面，再按需开启', body: b, foot: '关闭其他占用同一采集卡的软件', width: 620, actions: [{ label: '取消' }, { label: '连接并开始', primary: true, icon: 'play', run: () => app.go('min') }] };
  };

  DIALOGS.ps5 = app => {
    const b = h(`<div></div>`);
    const host = h(`<div class="hostcard"><span class="hico">${I('gamepad')}</span><div class="t"><b>PS5-8F21</b><small>192.168.1.42 · 已配对 · 待机中</small></div><span class="tag ok">已保存</span><button class="btn">${I('zap')}唤醒</button><button class="btn ghost icon" title="删除配对">${I('trash')}</button></div>`);
    const find = h(`<button class="btn" style="width:100%;justify-content:center;margin-top:8px">${I('search')}查找局域网主机</button>`);
    find.onclick = () => { find.innerHTML = `<span class="spin"></span>正在查找…可随时取消`; setTimeout(() => find.innerHTML = `${I('search')}找到 1 台 · 再次查找`, 1600); };
    b.append(app.sec('主机'), host, find);
    b.append(app.row('手动填写地址', 'PS5 设置 → 网络 → 连接状态 → 查看连接状态', h(`<input class="field" id="ps5-ip-${app.st.uid}" placeholder="192.168.1.x" style="width:170px">`)));
    b.append(app.sec('账号与配对'), app.group(
      app.row('PSN Account ID', '不是昵称或 Online ID', h(`<input class="field mono" id="ps5-acc-${app.st.uid}" value="bG9yZW0taXBzdW0=" style="width:190px">`), h(`<button class="btn">${I('globe')}登录 PSN</button>`)),
      app.row('8 位配对码', '首次配对需要：PS5 远程游玩 → 关联设备', h(`<input class="field mono" id="ps5-pin-${app.st.uid}" placeholder="••••••••" maxlength="8" style="width:140px;letter-spacing:.2em">`)),
      app.row('登录 PIN（可选）', '', h(`<input class="field mono" id="ps5-lpin-${app.st.uid}" placeholder="数字" style="width:140px">`))
    ));
    b.append(app.sec('串流画质'), app.group(
      app.row('输入分辨率', '主机原生串流最高 1080p', seg(['720p · 60', '1080p · 30', '1080p · 60'], '1080p · 60', () => { })),
      app.row('编码', '', seg(['H.264 · SDR', 'H.265 · SDR', 'H.265 · HDR（实验）'], 'H.265 · SDR', () => { })),
      app.sliderRow('码率请求', 40, 5, 100, 1, ' Mbps', { sub: '主机不保证达到请求值', w: 150 }),
      app.row('解码', '', seg(['自动 · 优先硬解', 'D3D12VA 硬件', 'CPU 软件'], '自动 · 优先硬解', () => { }))
    ));
    b.append(app.sec('手柄'), app.group(app.row('DualSense 转发', 'USB 优先；“仅观看”不转发手柄', seg(['转发', '仅观看'], '转发', () => { })), app.row('校准陀螺仪', '', h(`<button class="btn">校准</button>`))));
    return { icon: 'gamepad', title: 'PS5 串流', sub: '局域网 Remote Play · 凭据加密保存在本机', body: b, foot: '外网串流暂未实现', width: 640, actions: [{ label: '取消' }, { label: '连接', primary: true, icon: 'play', run: () => app.go('min') }] };
  };

  DIALOGS.screen = app => {
    const b = h(`<div></div>`);
    const kind = seg(['窗口', '显示器'], '窗口', v => render(v));
    const grid = h(`<div class="targets"></div>`);
    const W = [['Cyberpunk 2077', 'Cyberpunk2077.exe', 1, 0], ['OBS Studio 31.0', 'obs64.exe', 2, 1], ['哔哩哔哩直播', 'livehime.exe', 3, 2], ['Microsoft Edge', 'msedge.exe', 4, 1], ['Steam', 'steam.exe', 5, 0], ['Veyra 字幕调试', 'notepad.exe', 6, 2]];
    const M = [['显示器 1 · 主屏', '3840×2160 · 144Hz · HDR', 7, 0], ['显示器 2', '2560×1440 · 165Hz', 8, 1]];
    function render(v) {
      grid.innerHTML = '';
      (v === '窗口' ? W : M).forEach(([t, s, seed, pal], i) => {
        const c = h(`<button class="target${i === 0 ? ' sel' : ''}"><div class="thumb"></div><b>${t}</b><small>${s}</small></button>`);
        c.querySelector('.thumb').appendChild(scene(seed, pal));
        c.onclick = () => { grid.querySelectorAll('.target').forEach(x => x.classList.toggle('sel', x === c)); };
        grid.appendChild(c);
      });
      grid.style.gridTemplateColumns = v === '窗口' ? 'repeat(3,1fr)' : 'repeat(2,1fr)';
    }
    render('窗口');
    b.append(h(`<div style="display:flex;align-items:center;gap:10px;margin:6px 0 10px"></div>`));
    b.firstChild.append(kind, h(`<span class="sp" style="flex:1"></span>`), h(`<button class="btn ghost">${I('refresh')}刷新</button>`));
    b.appendChild(grid);
    b.append(app.sec('选项'), app.group(
      app.row('捕获方式', '', seg(['Windows Graphics Capture', 'DXGI 显示器兼容（无指针）'], 'Windows Graphics Capture', () => { })),
      app.row('帧率上限', '', seg(['30', '60', '120', '144', '240'], '60', () => { })),
      app.row('显示鼠标指针', '', sw(true, () => { })),
      app.row('裁剪 / 像素', '上 · 下 · 左 · 右', h(`<span style="display:flex;gap:6px">${['上', '下', '左', '右'].map(x => `<input class="field mono" id="crop-${x}-${app.st.uid}" aria-label="裁剪${x}" value="0" style="width:52px">`).join('')}</span>`)),
      app.row('填满窗口', '', sw(false, () => { }))
    ));
    return { icon: 'monitor', title: '屏幕捕获', sub: '把一个窗口或整块显示器作为片源', body: b, width: 700, actions: [{ label: '取消' }, { label: '开始捕获', primary: true, icon: 'play', run: () => app.go('min') }] };
  };

  DIALOGS.subtitle = app => {
    const b = h(`<div></div>`);
    const pv = h(`<div class="sub-prev"><div class="video"></div><div class="subline"><span>我们要去的地方，</span><span>地图上没有标记。</span></div></div>`);
    pv.querySelector('.video').appendChild(scene(31, 2));
    const line = pv.querySelector('.subline');
    const setSize = v => line.style.fontSize = (v * .42) + 'px';
    b.appendChild(pv);
    b.append(app.sec('字幕轨'), app.group(
      app.row('主字幕', '', app.select('主字幕', ['关闭', '简体中文 · 内嵌 ASS', 'English · 内嵌 SRT', '外部：Dune.zh.srt'], '简体中文 · 内嵌 ASS', null, 220), h(`<button class="btn ghost">${I('import')}加载</button>`)),
      app.row('主字幕延时', '', h(`<span class="stepper"><button class="btn icon">${I('minus')}</button><input class="field mono" id="sub-delay-${app.st.uid}" value="0 ms" style="width:76px;text-align:center"><button class="btn icon">${I('plus')}</button></span>`))
    ));
    const size = h(`<span class="val">48</span>`), sz = slider(48, 20, 96, 1, v => { size.textContent = v; setSize(v); }); sz.style.width = '170px';
    const bottom = h(`<span class="val">6%</span>`), bt = slider(6, 0, 30, 1, v => { bottom.textContent = v + '%'; line.style.bottom = (v + 2) + '%'; }); bt.style.width = '170px';
    b.append(app.sec('外观'), app.group(
      app.row('字体', '', app.select('字体', ['字幕原字体', 'Noto Sans SC', 'DengXian', 'SimHei', 'Segoe UI'], '字幕原字体', v => line.style.fontFamily = v === '字幕原字体' ? '' : `"${v}",sans-serif`, 180)),
      app.row('字号', '双行不自动缩小', sz, size),
      app.row('描边', '', seg(['无', '细', '中', '粗'], '中', v => line.dataset.stroke = v)),
      app.row('背景条', '', sw(false, v => line.classList.toggle('bar', v))),
      app.row('底部距离', '', bt, bottom),
      app.row('自动缩小字号以适应目标行数', '', sw(false, () => { }))
    ));
    requestAnimationFrame(() => setSize(48));
    return { icon: 'type', title: '字幕设置', sub: '实时预览，设置对所有文件生效', body: b, width: 620, actions: [{ label: '恢复默认' }, { label: '完成', primary: true }] };
  };

  DIALOGS.audio = app => {
    const b = h(`<div></div>`);
    const tracks = h(`<div class="tracks"></div>`);
    [['TrueHD 7.1', '英语 · 48 kHz', true], ['AC-3 5.1', '国语 · 48 kHz', false], ['AAC 2.0', '导演评论 · 48 kHz', false]].forEach(([t, s, on]) => {
      const r = h(`<button class="trk${on ? ' sel' : ''}"><span class="bars"><i></i><i></i><i></i><i></i></span><span><b>${t}</b><small>${s}</small></span><span class="chk">${I('check')}</span></button>`);
      r.onclick = () => tracks.querySelectorAll('.trk').forEach(x => x.classList.toggle('sel', x === r)); tracks.appendChild(r);
    });
    b.append(app.sec('音轨'), tracks);
    b.append(app.sec('输出'), app.group(
      app.row('输出设备', '', app.select('输出设备', ['跟随系统默认', '扬声器 (Realtek)', 'HDMI · LG OLED', '耳机 (DualSense)'], '跟随系统默认', null, 200)),
      app.sliderRow('音量', 70, 0, 100, 1, '%', { w: 170 }),
      app.row('多声道', '5.1 / 7.1 按设备能力输出', seg(['自动', '立体声下混'], '自动', () => { }))
    ));
    b.append(app.sec('音画同步'), app.group(
      app.row('同步方式', '', seg(['自动', '手动'], '自动', () => { })),
      app.sliderRow('手动偏移', 0, -500, 500, 10, ' ms', { w: 170, center: true, sub: '负值 = 声音提前' }),
      app.row('当前偏差', '软件内测量，不是扬声器实测', h(`<span class="val">+12 ms</span>`))
    ));
    return { icon: 'music', title: '音频设置', body: b, width: 560, actions: [{ label: '完成', primary: true }] };
  };
})();
