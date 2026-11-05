const documents = new Map();
let nativePort = null;
let reconnect = null;
let retryDelay = 1000;
let nativeConnected = false;
const publish = (id, state) => { try { nativePort?.postMessage({...state, id}); } catch {} };

async function normalizeArtwork(blob) {
  const bitmap = await createImageBitmap(blob);
  try {
    if (!bitmap.width || !bitmap.height || bitmap.width > 8192 || bitmap.height > 8192 || bitmap.width * bitmap.height > 16777216) throw new Error('image_dimensions');
    const scale = Math.min(1, 256 / Math.max(bitmap.width, bitmap.height));
    const canvas = new OffscreenCanvas(Math.max(1, Math.round(bitmap.width * scale)), Math.max(1, Math.round(bitmap.height * scale)));
    const painter = canvas.getContext('2d');
    if (!painter) throw new Error('image_canvas');
    painter.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
    const png = await canvas.convertToBlob({type: 'image/png'});
    if (!png.size || png.size > 262144) throw new Error('image_size');
    return new Uint8Array(await png.arrayBuffer());
  } finally { bitmap.close(); }
}

async function applyArtwork(id, document, blob, generation) {
  let bytes;
  try { bytes = await normalizeArtwork(blob); }
  catch { if (document.generation === generation) document.artworkStatus = 'conversion_error'; return; }
  if (documents.get(id) !== document || document.generation !== generation || !document.state) return;
  let binary = ''; for (const byte of bytes) binary += String.fromCharCode(byte);
  document.artworkData = btoa(binary);
  document.artworkStatus = 'sent';
  document.artworkBytes = bytes.length;
  document.state.artworkData = document.artworkData;
  document.state.artworkRevision = generation;
  publish(id, document.state);
}

async function openExtensions() {
  for (const url of ['browser://extensions/', 'chrome://extensions/']) {
    try { await chrome.tabs.create({url, active: true}); return true; } catch {}
  }
  return false;
}

async function attachOpenPages() {
  const tabs = await chrome.tabs.query({url: ['http://*/*', 'https://*/*']});
  await Promise.allSettled(tabs.map(async (tab) => {
    if (!Number.isInteger(tab.id)) return;
    const target = {tabId: tab.id, allFrames: true};
    await chrome.scripting.executeScript({target, world: 'MAIN', files: ['page.js']});
    await chrome.scripting.executeScript({target, world: 'ISOLATED', files: ['content.js']});
  }));
}

async function loadArtwork(id, document, url, generation, controller) {
  const timeout = setTimeout(() => controller.abort(), 5000);
  try {
    const parsed = new URL(url);
    if (!['http:', 'https:'].includes(parsed.protocol)) return;
    const response = await fetch(parsed.href, {credentials: 'omit', referrerPolicy: 'no-referrer', signal: controller.signal});
    if (!response.ok) { if (document.generation === generation) document.artworkStatus = `http_${response.status}`; return; }
    if (!response.body || !(response.headers.get('content-type') || '').toLowerCase().startsWith('image/')) { if (document.generation === generation) document.artworkStatus = 'invalid_image_type'; return; }
    const reader = response.body.getReader();
    const parts = []; let size = 0;
    while (true) {
      const {done, value} = await reader.read();
      if (done) break;
      size += value.length;
      if (size > 262144) { await reader.cancel(); if (document.generation === generation) document.artworkStatus = 'too_large'; return; }
      parts.push(value);
    }
    if (documents.get(id) !== document || document.generation !== generation || !document.state) return;
    await applyArtwork(id, document, new Blob(parts, {type: response.headers.get('content-type')}), generation);
  } catch { if (document.generation === generation) document.artworkStatus = controller.signal.aborted ? 'cancelled_or_timeout' : 'network_error'; }
  finally { clearTimeout(timeout); }
}

function connect(force = false) {
  if (nativePort || reconnect || (!force && !documents.size)) return;
  const current = chrome.runtime.connectNative('org.stillbar.media');
  nativePort = current;
  current.onMessage.addListener((message) => {
    if (message?.type === 'hello') {
      nativeConnected = true;
      retryDelay = 1000;
      for (const [id, document] of documents) {
        if (document.state) current.postMessage({...document.state, id});
        document.port.postMessage({type: 'refresh'});
      }
    } else if (message?.type === 'command' && typeof message.id === 'string') {
      documents.get(message.id)?.port.postMessage(message);
    } else if (message?.type === 'artwork_status') {
      const document = documents.get(message.id);
      if (document && message.revision === document.generation && document.artworkData) {
        document.artworkStatus = message.result === 'applied' ? 'applied' : 'native_error';
        document.artworkError = typeof message.error === 'string' ? message.error.slice(0, 16) : '';
      }
    } else if (message?.type === 'open_extensions') {
      openExtensions().then((opened) => {
        try { current.postMessage({type: 'extensions_opened', opened}); } catch {}
      });
    }
  });
  current.onDisconnect.addListener(() => {
    if (nativePort !== current) return;
    nativePort = null;
    nativeConnected = false;
    if (!documents.size) return;
    reconnect = setTimeout(() => { reconnect = null; connect(); }, retryDelay);
    retryDelay = Math.min(retryDelay * 2, 30000);
  });
  current.postMessage({type: 'hello', version: 1});
}

function validState(message) {
  return message && message.type === 'state' && typeof message.title === 'string' && message.title.length <= 1024 &&
    typeof message.artist === 'string' && message.artist.length <= 1024 && typeof message.playing === 'boolean' &&
    Number.isFinite(message.position) && message.position >= 0 && Number.isFinite(message.duration) && message.duration >= 0 &&
    ['seek', 'previous', 'next'].every((key) => typeof message[key] === 'boolean') &&
    (message.artworkData === undefined || (typeof message.artworkData === 'string' && message.artworkData.length <= 350000)) &&
    (message.artworkUrl === undefined || (typeof message.artworkUrl === 'string' && message.artworkUrl.length <= 4096));
}

chrome.runtime.onConnect.addListener((port) => {
  if (port.name !== 'media-document' || !port.sender?.tab || !Number.isInteger(port.sender.frameId)) return;
  const id = crypto.randomUUID();
  const document = {port, state: null, artworkUrl: '', artworkData: '', incomingArtwork: '', artworkStatus: 'not_provided', artworkBytes: 0, artworkError: '', generation: 0, controller: null};
  documents.set(id, document);
  port.onMessage.addListener((message) => {
    if (message?.type === 'remove') {
      document.state = null; document.artworkUrl = ''; document.artworkData = ''; document.incomingArtwork = ''; document.artworkStatus = 'not_provided'; document.artworkBytes = 0; document.artworkError = ''; ++document.generation; document.controller?.abort();
      publish(id, {type: 'remove'}); return;
    }
    if (!validState(message)) return;
    const {artworkUrl = '', artworkData = '', ...state} = message;
    if (artworkUrl !== document.artworkUrl) {
      document.controller?.abort(); document.artworkUrl = artworkUrl; document.artworkData = ''; document.incomingArtwork = ''; document.artworkBytes = 0; document.artworkError = ''; document.artworkStatus = artworkUrl ? 'waiting' : 'not_provided'; ++document.generation;
      if (artworkUrl && /^https?:\/\//i.test(artworkUrl)) {
        document.controller = new AbortController();
        void loadArtwork(id, document, artworkUrl, document.generation, document.controller);
      }
    }
    if (artworkData && artworkData !== document.incomingArtwork && !/^https?:\/\//i.test(artworkUrl)) {
      document.incomingArtwork = artworkData;
      const generation = ++document.generation;
      try {
        const bytes = Uint8Array.from(atob(artworkData), (character) => character.charCodeAt(0));
        void applyArtwork(id, document, new Blob([bytes]), generation);
      } catch { document.artworkStatus = 'conversion_error'; }
    }
    document.state = {...state, artworkData: document.artworkData, artworkRevision: document.generation};
    publish(id, document.state);
  });
  port.onDisconnect.addListener(() => {
    document.controller?.abort();
    documents.delete(id);
    try { nativePort?.postMessage({type: 'remove', id}); } catch {}
    if (!documents.size) {
      if (reconnect) clearTimeout(reconnect);
      reconnect = null;
    }
  });
  connect();
});

chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (sender.id !== chrome.runtime.id) return;
  if (message?.type === 'open_extensions') { openExtensions().then((opened) => reply({opened})); return true; }
  if (message?.type !== 'connection_status') return;
  reply({connected: nativeConnected, documents: documents.size,
    sources: [...documents.values()].filter((document) => document.state).map((document) => ({playing: document.state.playing, artwork: document.artworkStatus, artworkBytes: document.artworkBytes, artworkError: document.artworkError}))});
});

chrome.runtime.onInstalled?.addListener(() => { connect(true); void attachOpenPages().catch(() => {}); });
chrome.runtime.onStartup?.addListener(() => connect(true));
