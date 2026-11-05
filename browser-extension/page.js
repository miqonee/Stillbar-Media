(() => {
  if (globalThis.__stillbarPageV1) return;
  globalThis.__stillbarPageV1 = true;
  const channel = 'stillbar-media-v1';
  const session = navigator.mediaSession;
  const handlers = new Map();
  let selected = null;
  let queued = false;
  let last = '';
  const observer = new MutationObserver(() => { if (selected && !selected.isConnected) { selected = null; observer.disconnect(); update(); } });
  const select = (media) => { selected = media; observer.disconnect(); if (selected) observer.observe(document, {childList: true, subtree: true}); };
  const text = (value) => typeof value === 'string' ? value.slice(0, 1024) : '';
  const emit = () => {
    queued = false;
    if (!selected?.isConnected || selected.ended) select([...document.querySelectorAll('audio,video')].find((item) => !item.paused && !item.ended) || null);
    const media = selected?.isConnected ? selected : null;
    if (!media || media.ended || !media.currentSrc) {
      const state = JSON.stringify({type: 'remove'});
      if (state !== last) { last = state; window.postMessage({channel, to: 'extension', payload: JSON.parse(state)}, '*'); }
      return;
    }
    const metadata = session?.metadata;
    const duration = Number.isFinite(media.duration) && media.duration > 0 ? media.duration : 0;
    const payload = {
      type: 'state', title: text(metadata?.title), artist: text(metadata?.artist),
      artwork: (() => { try { const src = text(metadata?.artwork?.[0]?.src); return src ? new URL(src, location.href).href : ''; } catch { return ''; } })(), playing: !media.paused && !media.ended,
      position: Number.isFinite(media.currentTime) ? Math.max(0, media.currentTime) : 0,
      duration, seek: duration > 0 && media.seekable.length > 0,
      previous: handlers.has('previoustrack'), next: handlers.has('nexttrack')
    };
    const state = JSON.stringify(payload);
    if (state !== last) { last = state; window.postMessage({channel, to: 'extension', payload}, '*'); }
  };
  const update = () => { if (!queued) { queued = true; queueMicrotask(emit); } };
  const pick = (event) => {
    const target = event.target;
    if (!(target instanceof HTMLMediaElement)) return;
    if (event.type === 'play' || event.type === 'playing' || !selected || selected.ended || !selected.isConnected) select(target);
    if (target === selected) update();
  };
  for (const event of ['play', 'playing', 'pause', 'ended', 'emptied', 'loadedmetadata', 'durationchange', 'seeked', 'ratechange']) {
    document.addEventListener(event, pick, true);
  }
  if (session) {
    const original = session.setActionHandler.bind(session);
    session.setActionHandler = (action, handler) => {
      original(action, handler);
      if (typeof handler === 'function') handlers.set(action, handler); else handlers.delete(action);
      update();
    };
    const proto = Object.getPrototypeOf(session);
    const descriptor = Object.getOwnPropertyDescriptor(proto, 'metadata');
    if (descriptor?.get && descriptor?.set && descriptor.configurable) {
      Object.defineProperty(proto, 'metadata', {...descriptor, set(value) { descriptor.set.call(this, value); update(); }});
    }
    if (globalThis.MediaMetadata) for (const field of ['title', 'artist', 'artwork']) {
      const p = MediaMetadata.prototype;
      const d = Object.getOwnPropertyDescriptor(p, field);
      if (d?.get && d?.set && d.configurable) Object.defineProperty(p, field, {...d, set(value) { d.set.call(this, value); update(); }});
    }
  }
  window.addEventListener('message', (event) => {
    if (event.source !== window || event.data?.channel !== channel || event.data?.to !== 'page') return;
    const command = event.data.payload;
    if (command?.type === 'refresh') { if (!selected) select([...document.querySelectorAll('audio,video')].find((item) => !item.paused && !item.ended) || null); last = ''; update(); return; }
    if (command?.type !== 'command' || !selected?.isConnected) return;
    const action = command.action;
    try {
      if (handlers.has(action)) handlers.get(action)({action, ...(action === 'seekto' ? {seekTime: command.position} : {})});
      else if (action === 'play') selected.play().catch(() => {});
      else if (action === 'pause') selected.pause();
      else if (action === 'seekto' && Number.isFinite(command.position) && selected.seekable.length) selected.currentTime = command.position;
    } catch {}
    update();
  });
  update();
})();
