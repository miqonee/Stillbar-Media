(() => {
  if (globalThis.__stillbarContentV1) return;
  globalThis.__stillbarContentV1 = true;
  const channel = 'stillbar-media-v1';
  let port = null;
  let reconnect = null;
  let delay = 1000;
  let revision = 0;
  let lastArtwork = '';
  let artworkData = '';
  let state = null;
  const publish = () => { if (state && port) try { port.postMessage({...state, artworkData, artworkUrl: lastArtwork}); } catch {} };
  async function loadArtwork(url, generation) {
    try {
      const parsed = new URL(url);
      if (!['data:', 'blob:'].includes(parsed.protocol)) return;
      const response = await fetch(parsed.href, {credentials: 'omit', referrerPolicy: 'no-referrer'});
      if (!response.ok || !response.body) return;
      const reader = response.body.getReader();
      const parts = []; let size = 0;
      while (true) {
        const {done, value} = await reader.read();
        if (done) break;
        size += value.length;
        if (size > 262144) { await reader.cancel(); return; }
        parts.push(value);
      }
      if (generation !== revision) return;
      const bytes = new Uint8Array(size); let offset = 0;
      for (const part of parts) { bytes.set(part, offset); offset += part.length; }
      let binary = ''; for (const byte of bytes) binary += String.fromCharCode(byte);
      artworkData = btoa(binary); publish();
    } catch {}
  }
  const sendPage = (payload) => window.postMessage({channel, to: 'page', payload}, '*');
  function connect() {
    try {
      const current = chrome.runtime.connect({name: 'media-document'}); port = current;
      current.onMessage.addListener(sendPage);
      current.onDisconnect.addListener(() => {
        if (port !== current) return;
        port = null;
        if (reconnect) return;
        reconnect = setTimeout(() => { reconnect = null; connect(); }, delay);
        delay = Math.min(delay * 2, 30000);
      });
      delay = 1000; publish(); sendPage({type: 'refresh'});
    } catch {}
  }
  window.addEventListener('message', (event) => {
    if (event.source !== window || event.data?.channel !== channel || event.data?.to !== 'extension') return;
    const payload = event.data.payload;
    if (!payload || typeof payload !== 'object') return;
    if (payload.type === 'remove') { state = null; ++revision; lastArtwork = ''; artworkData = ''; try { port?.postMessage({type: 'remove'}); } catch {} return; }
    if (payload.type !== 'state') return;
    if (typeof payload.title !== 'string' || payload.title.length > 1024 || typeof payload.artist !== 'string' || payload.artist.length > 1024 ||
        typeof payload.playing !== 'boolean' || !Number.isFinite(payload.position) || payload.position < 0 ||
        !Number.isFinite(payload.duration) || payload.duration < 0 || !['seek', 'previous', 'next'].every((key) => typeof payload[key] === 'boolean')) return;
    const artwork = typeof payload.artwork === 'string' && payload.artwork.length <= 4096 ? payload.artwork : '';
    state = {type: 'state', title: payload.title, artist: payload.artist, playing: payload.playing, position: payload.position,
      duration: payload.duration, seek: payload.seek, previous: payload.previous, next: payload.next};
    if (typeof artwork === 'string' && artwork !== lastArtwork) {
      lastArtwork = artwork; artworkData = ''; const generation = ++revision;
      if (artwork) loadArtwork(artwork, generation);
    }
    publish();
  });
  connect();
})();
