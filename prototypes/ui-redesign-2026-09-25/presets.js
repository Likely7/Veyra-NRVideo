// 预设：菜单、另存为、管理；以及切换到节点模式的确认。列表预设与节点预设完全分开保存。
(function () {
  const { h, I, R, seg, sw, DIALOGS } = window.VY;
  const KIND = { list: '列表', node: '节点' };

  VY.presetMenu = (app, anchor, kind) => {
    const st = app.st, mine = R.presets.filter(p => p.kind === kind);
    app.menu(anchor, `${KIND[kind]}预设`, [
      ...mine.map(p => ({ label: p.name, note: p.note, id: p.id, checked: p.id === (kind === 'node' ? st.nodePreset : st.preset), tag: p.builtin ? '<span class="tag">内置</span>' : '' })),
      { sep: true },
      { label: '把当前设置另存为预设…', icon: 'plus', act: 'save' },
      { label: '管理预设…', note: '重命名 · 删除 · 设为启动默认', icon: 'settings', act: 'manage' }
    ], o => {
      if (o.act === 'save') return app.dialog(kind === 'node' ? 'savePresetNode' : 'savePresetList');
      if (o.act === 'manage') return app.dialog(kind === 'node' ? 'managePresetsNode' : 'managePresetsList');
      if (kind === 'node') st.nodePreset = o.id; else st.preset = o.id;
      app.applyChange(true); app.emit('preset');
    }, 'down');
  };

  // Summary of what the current configuration contains, so the user sees exactly what gets saved.
  function currentItems(app, kind) {
    if (kind === 'node') {
      const g = app.st.graph; if (!g) return [['链路', '尚未打开节点模式']];
      const names = g.chain.map(id => g.nodes.find(n => n.id === id)).map(n => [R.types[n.type].name, R.types[n.type].summary(n.p), n.on]);
      return [['链路顺序', g.chain.length + ' 个节点，按画布连线顺序'], ...names];
    }
    return app.st.chain.map(n => [n.type === 'nr' ? 'NR 层 ' + (app.st.chain.filter(x => x.type === 'nr').indexOf(n) + 1) : R.types[n.type].name, R.types[n.type].summary(n.p), n.on]);
  }

  function saveDialog(kind) {
    return app => {
      const b = h(`<div></div>`);
      const nameRow = h(`<div class="namebox"><label for="pn-${app.st.uid}-${kind}">预设名称</label><input class="field" id="pn-${app.st.uid}-${kind}" value="${kind === 'node' ? '我的节点链 1' : '我的预设 1'}"><small data-hint class="faint">名称会显示在极简模式和导出页的预设菜单里</small></div>`);
      const inp = nameRow.querySelector('input'), hint = nameRow.querySelector('[data-hint]');
      inp.oninput = () => { const dup = R.presets.some(p => p.kind === kind && p.name === inp.value.trim()); hint.textContent = dup ? '已有同名预设，保存后会覆盖它' : inp.value.trim() ? '可以保存' : '请输入名称'; hint.className = dup ? 'warn' : ''; };
      b.appendChild(nameRow);
      b.appendChild(app.sec(`将保存的内容 · 当前正在使用的${KIND[kind]}设置`));
      const list = h(`<div class="savelist"></div>`);
      currentItems(app, kind).forEach(([n, d, on]) => list.appendChild(h(`<div class="si${on === false ? ' off' : ''}"><span class="dot${on === false ? ' off' : ''}"></span><b>${n}</b><small>${d}</small>${on === false ? '<span class="tag">已关闭</span>' : ''}</div>`)));
      b.appendChild(list);
      b.appendChild(app.sec('选项'));
      b.appendChild(app.group(
        app.row('包含调色', kind === 'node' ? '节点里的所有调色节点都会一起保存' : '列表预设默认不含调色，勾选后一并保存', sw(kind === 'node', () => { })),
        app.row('包含音频偏移', '', sw(false, () => { })),
        app.row('保存后设为启动默认', '', sw(false, () => { }))
      ));
      return { icon: 'plus', title: `另存为${KIND[kind]}预设`, sub: `${KIND[kind]}预设和${kind === 'node' ? '列表' : '节点'}预设分开保存，互不影响`, body: b, width: 540,
        actions: [{ label: '取消' }, { label: '保存预设', primary: true, icon: 'check', run: () => { R.presets.push({ id: 'u' + Date.now(), name: inp.value.trim() || '未命名', note: '刚刚保存', kind }); app.setStatus('ok', '预设已保存'); } }] };
    };
  }
  DIALOGS.savePresetList = saveDialog('list');
  DIALOGS.savePresetNode = saveDialog('node');

  function manageDialog(kind0) {
    return app => {
      let kind = kind0;
      const b = h(`<div></div>`);
      const top = h(`<div style="display:flex;align-items:center;gap:10px;margin:4px 0 10px"></div>`);
      top.append(seg(['列表预设', '节点预设'], kind === 'node' ? '节点预设' : '列表预设', v => { kind = v === '节点预设' ? 'node' : 'list'; render(); }), h(`<span style="flex:1"></span>`), h(`<button class="btn ghost">${I('import')}导入</button>`), h(`<button class="btn ghost">${I('upload')}导出</button>`));
      const list = h(`<div class="plist"></div>`);
      b.append(top, list);
      let startDefault = { list: 'max', node: 'n-grade' };
      function render() {
        list.innerHTML = '';
        R.presets.filter(p => p.kind === kind).forEach(p => {
          const r = h(`<div class="prow"><span class="pico">${I(kind === 'node' ? 'nodes' : 'layers')}</span>
            <div class="pt"><input class="field pname" aria-label="预设名称" ${p.builtin ? 'disabled' : ''}><small>${p.note}</small></div>
            ${startDefault[kind] === p.id ? '<span class="tag acc">启动默认</span>' : ''}${p.builtin ? '<span class="tag">内置</span>' : ''}
            <button class="btn ghost icon" data-def title="设为启动默认">${I('home')}</button>
            <button class="btn ghost icon" data-dup title="复制">${I('dup')}</button>
            <button class="btn ghost icon" data-del title="删除" ${p.builtin ? 'disabled' : ''}>${I('trash')}</button></div>`);
          r.querySelector('.pname').value = p.name;
          r.querySelector('.pname').onchange = e => p.name = e.target.value;
          r.querySelector('[data-def]').onclick = () => { startDefault[kind] = p.id; render(); };
          r.querySelector('[data-dup]').onclick = () => { R.presets.splice(R.presets.indexOf(p) + 1, 0, Object.assign({}, p, { id: 'c' + Date.now(), name: p.name + ' 副本', builtin: false })); render(); };
          r.querySelector('[data-del]').onclick = () => { r.classList.add('bye'); setTimeout(() => { R.presets.splice(R.presets.indexOf(p), 1); render(); }, 240); };
          list.appendChild(r);
        });
        requestAnimationFrame(() => top.querySelector('.seg')._sync(true));
      }
      render();
      b.appendChild(h(`<p class="faint" style="font-size:11.5px;margin:10px 2px 0">内置预设不能删除或改名，可以复制后修改。导出页和极简模式都从这里的预设中选择。</p>`));
      return { icon: 'settings', title: '管理预设', body: b, width: 600, actions: [{ label: '完成', primary: true }] };
    };
  }
  DIALOGS.managePresetsList = manageDialog('list');
  DIALOGS.managePresetsNode = manageDialog('node');

  DIALOGS.toNode = app => {
    const b = h(`<div class="switchinfo">
      <div class="two"><div class="side"><span class="pico">${I('list')}</span><b>列表模式</b><small>固定顺序：超分 → NR → 保护 → HDR → 补帧</small><span class="tag">当前 · 设置会原样保留</span></div>
      <span class="arrow">${I('right')}</span>
      <div class="side on"><span class="pico">${I('nodes')}</span><b>节点模式</b><small>自由排列节点，按连线顺序运行</small><span class="tag acc">上次的节点链：首尾双调色</span></div></div>
      <ul><li>两种模式的设置、预设完全分开保存，切回列表时恢复列表原来的设置。</li><li>切换会重建处理链，画面可能停顿几百毫秒。</li><li>节点模式是实验功能，自由排列的组合未经画质验证。</li></ul></div>`);
    return { icon: 'nodes', title: '切换到节点模式？', body: b, width: 560, actions: [{ label: '取消' }, { label: '切换到节点模式', primary: true, icon: 'nodes', run: () => { app.st.proView = 'node'; app.applyChange(true); app.go('node'); } }] };
  };
  DIALOGS.toList = app => {
    const b = h(`<div class="switchinfo"><ul><li>回到列表模式，恢复列表之前的设置和预设。</li><li>节点链会保留，下次切回节点模式时继续使用。</li><li>切换会重建处理链，画面可能停顿几百毫秒。</li></ul></div>`);
    return { icon: 'list', title: '切换回列表模式？', body: b, width: 480, actions: [{ label: '取消' }, { label: '切换到列表模式', primary: true, icon: 'list', run: () => { app.st.proView = 'list'; app.applyChange(true); app.go('pro'); } }] };
  };
})();
