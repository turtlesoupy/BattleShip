// rom-extract.js — build BattleShip.o2r in the browser from the player's own ROM.
//
// The engine package no longer ships the ROM-derived archive. The site stores
// the validated ROM bytes in IndexedDB (same origin as the engine iframe);
// this module either reuses an archive built earlier for that ROM + recipe,
// or runs Torch in a Web Worker (torch-worker.js) and caches the result.
//
// Scheduling matters more than Torch speed: extraction alone takes ~3–5 s in
// a visible tab, but overlapped with the engine's 7.5 MB wasm tier-up compile
// (or in a hidden tab, where Chrome lowers the renderer's thread priority)
// it stretches to 12–50 s. So the site calls `prewarmArchive()` right after
// validation (during the launch animation), and the engine shell resolves
// `ensureArchive()` before it inserts its own script tag, then only writes
// the bytes into MEMFS from `preRun`.
//
// IndexedDB contract (mirrored by the site's shared/rom-store.js — keep the
// two in sync):
//   db 'opensmash-rom' v1
//     roms      key sha1            { sha1, size, name, bytes: ArrayBuffer, storedAt }
//     archives  key `${recipe}:${sha1}` { key, recipe, sha1, bytes: ArrayBuffer, extras: [{path, bytes}], builtAt, ms }
//     meta      key 'current'       { key: 'current', sha1 }

const DB_NAME = 'opensmash-rom';
const DB_VERSION = 1;
export const ARCHIVE_PATH = '/BattleShip.o2r';
const RECIPE_MEMFS_PATH = '/BattleShip.o2r.recipe';
// Torch logs ~6500 lines for the US ROM; soft denominator for the progress %.
const EXPECTED_LOG_LINES = 6600;

function openDb() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);
    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains('roms')) db.createObjectStore('roms', { keyPath: 'sha1' });
      if (!db.objectStoreNames.contains('archives')) db.createObjectStore('archives', { keyPath: 'key' });
      if (!db.objectStoreNames.contains('meta')) db.createObjectStore('meta', { keyPath: 'key' });
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('IndexedDB unavailable'));
    request.onblocked = () => reject(new Error('IndexedDB upgrade blocked'));
  });
}

function idbGet(db, store, key) {
  return new Promise((resolve, reject) => {
    const request = db.transaction(store, 'readonly').objectStore(store).get(key);
    request.onsuccess = () => resolve(request.result || null);
    request.onerror = () => reject(request.error);
  });
}

function idbPut(db, store, value) {
  return new Promise((resolve, reject) => {
    const tx = db.transaction(store, 'readwrite');
    tx.objectStore(store).put(value);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

// URLs resolve against this module, not the document, so the site page can
// import /engine/rom-extract.js and still find the worker and recipe tree.
function engineUrl(relative, version) {
  const url = new URL(relative, import.meta.url);
  if (version) url.searchParams.set('v', version);
  return url.href;
}

function runTorch({ rom, files, version, onProgress }) {
  return new Promise((resolve, reject) => {
    const worker = new Worker(engineUrl('torch-worker.js', version), { type: 'module' });
    const finish = (fn, value) => { worker.terminate(); fn(value); };
    worker.onerror = (event) => finish(reject, new Error(event.message || 'Torch worker failed'));
    worker.onmessage = ({ data }) => {
      if (data.type === 'progress') onProgress?.(data);
      else if (data.type === 'done') finish(resolve, data);
      else if (data.type === 'error') finish(reject, new Error(data.message));
    };
    const recipe = { base: engineUrl('torch/', null), version, files };
    worker.postMessage({ rom, recipe }, [rom]);
  });
}

/**
 * Read the engine package manifest and return the recipe hash + package
 * version the archive must be built for. Callers outside the engine shell
 * (the site) use this so they build the same cache key the shell will look up.
 */
export async function engineRecipe() {
  const manifest = await (await fetch(engineUrl('manifest.json', null), { cache: 'no-cache' })).json();
  const entry = manifest.files.find((file) => file.path === RECIPE_MEMFS_PATH);
  const packaged = manifest.files.some((file) => file.path === ARCHIVE_PATH);
  let version = null;
  let recipe = 'unknown';
  if (entry) {
    const url = new URL(entry.url, import.meta.url);
    version = url.searchParams.get('v');
    recipe = (await (await fetch(url.href)).text()).trim();
  }
  return { recipe, version, packaged };
}

/**
 * Return the archive bytes for the stored ROM under `recipe`, building and
 * caching them if this browser has not done so yet.
 *
 * @returns {Promise<{source: 'cache'|'built', sha1: string, bytes: ArrayBuffer, ms?: number}>}
 */
export async function ensureArchive({ recipe, version, setStatus }) {
  const status = (text) => setStatus?.(text);
  let db;
  try {
    db = await openDb();
  } catch (error) {
    throw new Error(`Cannot open ROM storage (${error.message}). Private browsing may block it.`);
  }
  const current = await idbGet(db, 'meta', 'current');
  if (!current?.sha1) {
    throw new Error('No ROM staged in this browser. Upload your Smash 64 ROM on the site first.');
  }
  const sha1 = current.sha1;
  const key = `${recipe}:${sha1}`;

  const cached = await idbGet(db, 'archives', key);
  if (cached?.bytes) {
    status('assets ready (cached)');
    return { source: 'cache', sha1, bytes: cached.bytes, extras: cached.extras || [] };
  }

  const stored = await idbGet(db, 'roms', sha1);
  if (!stored?.bytes) {
    throw new Error('Your ROM is no longer stored in this browser. Upload it again on the site.');
  }

  status('reading asset recipe…');
  const listing = await fetch(engineUrl('torch/recipe.json', version));
  if (!listing.ok) throw new Error(`fetch torch/recipe.json: ${listing.status}`);
  const { files } = await listing.json();

  status('building assets from your ROM…');
  const started = performance.now();
  const result = await runTorch({
    // Copy: keep the stored ROM intact; the worker takes ownership of this one.
    rom: stored.bytes.slice(0),
    files,
    version,
    onProgress: ({ lines }) =>
      status(`building assets from your ROM… ${Math.min(99, Math.round((lines / EXPECTED_LOG_LINES) * 100))}%`),
  });
  const ms = performance.now() - started;
  try {
    await idbPut(db, 'archives', {
      key, recipe, sha1, bytes: result.archive, extras: result.extras || [], builtAt: Date.now(), ms,
      // Diagnostics: where the time went (worker phases) and how long after
      // document start the build began.
      timings: { ...result.timings, startedAtMs: Math.round(started), totalMs: Math.round(ms),
        visibility: document.visibilityState, focused: document.hasFocus() },
    });
  } catch (error) {
    console.warn('[rom-extract] could not cache archive:', error);
  }
  console.warn('[rom-extract] built archive', { ...result.timings, totalMs: Math.round(ms) });
  status(`assets built in ${(ms / 1000).toFixed(1)}s`);
  return { source: 'built', sha1, bytes: result.archive, extras: result.extras || [], ms };
}

/**
 * Site-side: build (or confirm) the archive for the current engine package
 * as soon as the ROM is stored, so the engine later finds it in the cache.
 * Resolves to null when the package still ships its own archive.
 */
export async function prewarmArchive({ setStatus } = {}) {
  const { recipe, version, packaged } = await engineRecipe();
  if (packaged) return null;
  return ensureArchive({ recipe, version, setStatus });
}

/**
 * Engine-shell convenience: write the archive plus any derived extras (the
 * port stages' CSS PNGs) from an ensureArchive result into MEMFS.
 */
export function writeArchive(FS, bytes, extras = []) {
  FS.writeFile(ARCHIVE_PATH, new Uint8Array(bytes));
  for (const extra of extras) {
    const dir = extra.path.substring(0, extra.path.lastIndexOf('/')) || '/';
    FS.mkdirTree(dir);
    FS.writeFile(extra.path, new Uint8Array(extra.bytes));
  }
}
