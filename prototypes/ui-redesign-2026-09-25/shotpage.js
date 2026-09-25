/* Screenshot page for the design prototype.
 *
 * Build aid, not part of the design. Mounts exactly ONE VeyraApp with the config
 * named in ?f=<id>, on a plain page, with no board, no pan and no zoom. The board's
 * own fit animation is what defeated earlier attempts: it moves and scales the
 * world over 650ms, so a capture landed mid-flight and framed the whole canvas
 * instead of one screen.
 *
 * The app itself is the real prototype component, so what is captured is the
 * design, not a redrawing of it.
 */
(function () {
  const params = new URLSearchParams(location.search);
  const want = params.get('f');
  if (!want) return;

  const FRAMES = {
    'f-home':     { w: 1280, h: 800, cfg: { page: 'home' } },
    'f-min':      { w: 1280, h: 583, cfg: { page: 'min', aspect: 2.39 } },
    'f-min169':   { w: 1280, h: 766, cfg: { page: 'min', aspect: 16 / 9, dockPinned: true } },
    'f-cap':      { w: 1280, h: 800, cfg: { page: 'home', dialog: 'capture' } },
    'f-ps5':      { w: 1280, h: 800, cfg: { page: 'home', dialog: 'ps5' } },
    'f-scr':      { w: 1280, h: 800, cfg: { page: 'home', dialog: 'screen' } },
    'f-sub':      { w: 1280, h: 800, cfg: { page: 'min', dialog: 'subtitle' } },
    'f-aud':      { w: 1280, h: 800, cfg: { page: 'min', dialog: 'audio' } },
    'f-pro':      { w: 1280, h: 800, cfg: { page: 'pro' } },
    'f-fg':       { w: 1280, h: 800, cfg: { page: 'pro', tab: 'fg' } },
    'f-color':    { w: 1280, h: 800, cfg: { page: 'pro', tab: 'color' } },
    'f-save':     { w: 1280, h: 800, cfg: { page: 'pro', dialog: 'savePresetList' } },
    'f-manage':   { w: 1280, h: 800, cfg: { page: 'pro', dialog: 'managePresetsList' } },
    'f-tonode':   { w: 1280, h: 800, cfg: { page: 'pro', dialog: 'toNode' } },
    'f-node':     { w: 1600, h: 1150, cfg: { page: 'node', proView: 'node' } },
    'f-exp':      { w: 1280, h: 800, cfg: { page: 'exp' } },
    'f-set':      { w: 1280, h: 800, cfg: { page: 'set' } }
  };

  const frame = FRAMES[want];
  if (!frame) return;

  document.addEventListener('DOMContentLoaded', function () {
    // Strip the board down to nothing: one app on a black page.
    const style = document.createElement('style');
    style.textContent = `
      html, body { margin:0; padding:0; background:#000; overflow:hidden; }
      #board, .toolbar, .hint, .section-title, .note, .frame-label { display:none !important; }
      #shotstage { position:absolute; left:0; top:0; }
      #shotstage .vy { border-radius:0 !important; }
    `;
    document.head.appendChild(style);

    const stage = document.createElement('div');
    stage.id = 'shotstage';
    document.body.appendChild(stage);

    // The real component, with the frame's own config.
    const app = window.VeyraApp(stage, Object.assign({}, frame.cfg));
    // Static references are taken in the final state. A dialog fades in over the page
    // with a CSS transition; headless Edge captured mid-fade, so the dialog and the page
    // under it were drawn over each other. ?motion=1 keeps the transitions for motion work.
    if (params.get('motion') !== '1') app.setReduced(true);
    app.root.style.width = frame.w + 'px';
    app.root.style.height = frame.h + 'px';

    // Motion probes (G0.5): ?motion=1&probe=dock|page|switch starts one motion, then
    // seeks every running animation to fixed times through the Web Animations API and
    // reads the animated value back. &t=<ms> instead freezes the motion at that time for
    // a screenshot. The numbers land in <pre id="motion-csv"> for --dump-dom.
    const probe = params.get('probe');
    if (probe) {
      setTimeout(function () {
        try { runProbe(app, probe, params.get('t')); }
        catch (e) { const pre = document.createElement('pre'); pre.id = 'motion-csv'; pre.textContent = 'error ' + e; document.body.appendChild(pre); }
      }, 300);
      return;
    }

    // A dialog opens on a later frame; the capture waits for it.
    setTimeout(function () { document.title = 'ready'; }, 900);
  });

  function runProbe(app, probe, freezeAt) {
    const root = app.root;
    const ty = el => { const m = getComputedStyle(el).transform; return m === 'none' ? 0 : new DOMMatrix(m).m42; };
    let start, read;
    if (probe === 'dock') {
      const dock = root.querySelector('.dock');
      start = () => app.setDockPinned(true);
      read = () => ty(dock);
    } else if (probe === 'switch') {
      const sw = [...root.querySelectorAll('.sw')].find(x => x.offsetParent !== null);
      start = () => sw.click();
      read = () => { const m = getComputedStyle(sw, '::after').transform; return m === 'none' ? 0 : new DOMMatrix(m).m41; };
    } else if (probe === 'page') {
      start = () => app.go('pro');
      read = () => { const e = app.pages.pro.el.querySelector('[data-in]'); const cs = getComputedStyle(e); return cs.opacity + ',' + ty(e); };
    } else if (probe === 'seg') {
      // The first visible seg; move to its last button. Progress 0..1 of the indicator's x.
      const sg = [...root.querySelectorAll('.seg')].find(x => x.offsetParent !== null);
      const ind = sg.querySelector('.seg-ind'), btns = sg.querySelectorAll('button');
      const tx = () => { const m = getComputedStyle(ind).transform; return m === 'none' ? 0 : new DOMMatrix(m).m41; };
      const x0 = tx(), x1 = btns[btns.length - 1].offsetLeft;
      start = () => btns[btns.length - 1].click();
      read = () => ((tx() - x0) / (x1 - x0)).toFixed(4);
    } else return;
    start();
    // The page switch shows the new page 150 ms later; wait for its animations to exist.
    setTimeout(function () {
      const anims = () => document.getAnimations();
      anims().forEach(a => a.pause());
      if (freezeAt !== null) { anims().forEach(a => a.currentTime = +freezeAt); document.title = 'ready'; return; }
      const rows = ['t_ms,value'];
      for (let t = 0; t <= 1000; t += 10) { anims().forEach(a => a.currentTime = t); rows.push(t + ',' + read()); }
      const pre = document.createElement('pre'); pre.id = 'motion-csv'; pre.textContent = rows.join(String.fromCharCode(10));
      document.body.appendChild(pre); document.title = 'ready';
    }, probe === 'page' ? 200 : 30);
  }
})();
