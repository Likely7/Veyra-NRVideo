// Custom (Chrome bridge): hand the current page URL to Veyra, with an explicit
// quality choice.
//
// Veyra listens on 127.0.0.1:18765 (see PlaybackServer.cpp) and runs the same
// 「播放地址」 path the player's own paste box uses, so Bilibili page URLs,
// direct media URLs and local files all behave identically.
//
// Quality is a direct Bilibili qn (0 = let Veyra pick the best rendition). It is
// manual on purpose: the page's own menu label lagged behind the <video> element
// and the player object is not global on current builds, so reading the page was
// unreliable. The choice is remembered in chrome.storage.
const ENDPOINT = 'http://127.0.0.1:18765';

const $url = document.getElementById('url');
const $quality = document.getElementById('quality');
const $go = document.getElementById('go');
const $status = document.getElementById('status');

// Bilibili quality menu label -> qn.
function labelToQn(label) {
  const t = (label || '').toLowerCase().replace(/\s+/g, '');
  if (t.includes('8k')) return 127;
  if (t.includes('杜比')) return 126;
  if (t.includes('hdr')) return 125;
  if (t.includes('4k')) return 120;
  if (t.includes('1080p')) return t.includes('高码率') ? 112 : t.includes('60') ? 116 : 80;
  if (t.includes('720p')) return t.includes('60') ? 74 : 64;
  if (t.includes('480p')) return 32;
  if (t.includes('360p')) return 16;
  return 0;
}

// Read the site's own quality: the selected menu item's label is what it streams.
// <video>.videoHeight lags behind the menu and player objects are not global on
// current builds, so this probes the menu first.
async function readPageQuality(tabId) {
  try {
    // 1) 最可靠：content.js 在页面里记下了播放器最近一次 playurl 请求的 qn，
    //    那是它真正在放的档位，不是猜的。
    try {
      const [hooked] = await chrome.scripting.executeScript({
        target: { tabId },
        world: 'MAIN',
        func: () => ({ qn: window.__veyraQn, at: window.__veyraQnAt })
      });
      const qn = hooked?.result?.qn;
      const at = hooked?.result?.at;
      if (Number.isFinite(qn) && qn > 0 && at && Date.now() - at < 120000) {
        return { qn, label: `播放器请求 qn=${qn}` };
      }
    } catch { /* 页面未注入时继续走下面的兜底 */ }

    // 2) 兜底：高度 + 菜单标签
    const [injection] = await chrome.scripting.executeScript({
      target: { tabId },
      func: () => {
        // Height: follows the user's selection (2160 Dolby Vision/4K, 1080 1080P...).
        let best = 0;
        for (const v of document.querySelectorAll('video')) {
          if (Number.isFinite(v.videoHeight) && v.videoHeight > best) best = v.videoHeight;
        }
        // Label: needed to tell apart renditions that share a height
        // (4K vs Dolby Vision vs HDR, 1080P vs 1080P60 vs 1080P high bitrate).
        let label = '';
        const active = document.querySelector(
          '.bpx-player-ctrl-quality-item.bpx-state-active, ' +
          '.bpx-player-ctrl-quality-menu-item.bpx-state-active, ' +
          '.bpx-player-ctrl-quality-menu .bpx-state-active, ' +
          '.bpx-player-ctrl-quality');
        if (active) {
          label = (active.textContent || '').trim();
          // `.bpx-player-ctrl-quality` 是容器，textContent 会把所有档位拼在一起
          // （"HDR 真彩 8K 超高清大会员 4K 超高清…"）。那种情况下取第一个档位关键字。
          if (label.length > 20) {
            const m = label.match(
              /(HDR\s*真彩|杜比视界|8K|4K\s*超高清|1080P\s*60帧|1080P\s*高码率|1080P|720P\s*60帧|720P|480P|360P)/);
            label = m ? m[1].replace(/\s+/g, ' ').trim() : '';
          }
        }
        return { height: best, label };
      }
    });
    const r = injection?.result || {};
    const h = r.height || 0;
    const fromLabel = r.label ? labelToQn(r.label) : 0;
    // The label refines the height only when it is consistent with it: a stale
    // "Dolby Vision" next to a 1080 height must not win.
    const consistent =
      fromLabel > 0 &&
      (h === 0 ||
       (h >= 2160 && fromLabel >= 120) ||
       (h >= 1080 && fromLabel >= 80) ||
       (h >= 720 && fromLabel >= 64) ||
       (h >= 480 && fromLabel >= 32) ||
       (h >= 360 && fromLabel >= 16));
    if (consistent) return { qn: fromLabel, label: r.label };
    return { qn: heightToQn(h), label: h ? `${h}px` : '' };
  } catch {
    return { qn: 0, label: '' };
  }
}

// Names mirror the site's own menu (screenshot of the Bilibili quality list).
const QUALITY_LABELS = {
  '-1': '当前网页画质',
  '0': '最高可用',
  '127': '8K',
  '126': '杜比视界',
  '125': 'HDR 真彩',
  '120': '4K 超高清',
  '116': '1080P 60帧',
  '112': '1080P 高码率',
  '80': '1080P 高清',
  '74': '720P 60帧',
  '64': '720P 准高清',
  '32': '480P 标清',
  '16': '360P 流畅'
};

let currentUrl = '';

function setStatus(text, ok) {
  $status.textContent = text;
  $status.className = ok ? 'ok' : 'err';
}

function isSupported(url) {
  try {
    const u = new URL(url);
    return u.protocol === 'http:' || u.protocol === 'https:';
  } catch {
    return false;
  }
}

function qualityName(qn) {
  return QUALITY_LABELS[String(qn)] || `qn=${qn}`;
}

// 选择记住，下次打开还是同一个档位。
// 默认「当前网页画质」(-1)；用户上次的选择优先。
chrome.storage.local.get({ quality: '-1' }).then(({ quality }) => {
  $quality.value = String(quality);
});
$quality.addEventListener('change', () => {
  chrome.storage.local.set({ quality: $quality.value });
});

// 从浏览器读 B 站会话，随 /play 一起发给 Veyra：这样不必在 Veyra 里再登录一次，
// 会员画质直接用浏览器里的登录状态。
async function readBilibiliCookie() {
  try {
    const cookies = await chrome.cookies.getAll({ domain: 'bilibili.com' });
    const wanted = new Set(['SESSDATA', 'bili_jct', 'DedeUserID', 'DedeUserID__ckMd5', 'sid']);
    return cookies
      .filter((c) => wanted.has(c.name) || c.name.startsWith('buvid'))
      .map((c) => `${c.name}=${c.value}`)
      .join('; ');
  } catch {
    return '';
  }
}

// 点击时先暂停网页播放，避免两边同时出声。
async function pausePageVideo(tabId) {
  try {
    await chrome.scripting.executeScript({
      target: { tabId },
      func: () => {
        for (const v of document.querySelectorAll('video')) {
          try { v.pause(); } catch { /* detached */ }
        }
      }
    });
  } catch {
    /* chrome:// pages cannot inject; nothing to pause there */
  }
}

(async () => {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  currentUrl = tab?.url || '';
  $url.textContent = currentUrl;
  $go.disabled = !isSupported(currentUrl);
  if (!isSupported(currentUrl)) setStatus('当前页面不是网页地址', false);
})();

$go.addEventListener('click', async () => {
  $go.disabled = true;
  setStatus('正在连接 Veyra...', true);
  try {
    const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
    let qn = Number($quality.value);
    let qualityLabel = qualityName(qn);
    if (qn === -1) {
      // 「当前网页画质」: read the site's own selection now and send that qn.
      // Falling back to 0 keeps the click working when the page is not Bilibili
      // or the menu cannot be found.
      const info = await readPageQuality(tab.id);
      if (info.qn > 0) {
        qn = info.qn;
        qualityLabel = info.label ? `${info.label}` : qualityName(qn);
      } else {
        qn = 0;
        qualityLabel = '最高可用（网页画质读不到）';
      }
    } else if (qn < 0) {
      qn = 0;
      qualityLabel = qualityName(qn);
    }
    await pausePageVideo(tab.id);

    let running = false;
    try {
      const ping = await fetch(ENDPOINT + '/ping', { cache: 'no-store' });
      running = ping.ok;
    } catch {
      running = false;
    }
    if (!running) {
      // Veyra not running: hand off through the veyra:// URL protocol, which the
      // registered handler starts with the URL and quality as arguments.
      const link = `veyra://play?url=${encodeURIComponent(currentUrl)}&quality=${qn}`;
      setStatus(`Veyra 未运行，正在通过协议启动…（${qualityLabel}）`, true);
      window.location.href = link;
      setTimeout(() => window.close(), 1500);
      return;
    }

    const cookie = await readBilibiliCookie();
    const r = await fetch(ENDPOINT + '/play', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ url: currentUrl, quality: qn, cookie })
    });
    const data = await r.json();
    if (!data.ok) throw new Error(data.error || 'Veyra 拒绝了请求');
    setStatus(`已发送，${qualityLabel}`, true);
    setTimeout(() => window.close(), 1200);
  } catch (e) {
    setStatus('失败：' + (e.message || e) + '（Veyra 是否在运行？）', false);
    $go.disabled = false;
  }
});
