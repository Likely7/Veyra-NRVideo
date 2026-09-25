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

    // A dialog opens on a later frame; the capture waits for it.
    setTimeout(function () { document.title = 'ready'; }, 900);
  });
})();
