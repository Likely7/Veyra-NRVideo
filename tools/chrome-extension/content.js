// 页面主世界注入：记录 B 站最近一次请求播放地址时用的 qn。
//
// 不靠猜：B 站播放器每次切换画质都会重新请求 /x/player/playurl?qn=<档位>，
// 那个 qn 就是它当前真正在放的档位。这里挂 fetch 与 XHR，把最后一次的 qn
// 存到 window.__veyraQn，扩展弹窗再读它。
(() => {
  if (window.__veyraDanmakuHook) return;   // 避免重复注入
  window.__veyraDanmakuHook = true;

  const record = (url) => {
    try {
      if (typeof url !== 'string' || !url.includes('/x/player/playurl')) return;
      const m = url.match(/[?&]qn=(\d+)/);
      if (m) {
        window.__veyraQn = Number(m[1]);
        window.__veyraQnAt = Date.now();
      }
    } catch {
      /* 解析失败忽略 */
    }
  };

  // fetch
  const originalFetch = window.fetch;
  if (typeof originalFetch === 'function') {
    window.fetch = function (...args) {
      try {
        const input = args[0];
        record(typeof input === 'string' ? input : input?.url);
      } catch { /* ignore */ }
      return originalFetch.apply(this, args);
    };
  }

  // XMLHttpRequest（B 站老代码仍会走 XHR）
  const originalOpen = XMLHttpRequest.prototype.open;
  XMLHttpRequest.prototype.open = function (method, url, ...rest) {
    record(url);
    return originalOpen.call(this, method, url, ...rest);
  };
})();
