// torch-worker.js — runs Torch (compiled to wasm) off the main thread to turn
// the player's own ROM into BattleShip.o2r inside the browser.
//
// Protocol (one job per worker):
//   in : { rom: ArrayBuffer, recipe: { base, version, files: [path...] } }
//   out: { type: 'progress', lines, text }   (Torch log lines, throttled)
//        { type: 'done', archive: ArrayBuffer, ms }
//        { type: 'error', message }
//
// `recipe.base` is the URL prefix that holds config.yml + yamls/<region>/*.yml
// (the same recipe tree standalone Torch reads with -s). Those files are
// port metadata, not ROM data, so they ship with the engine package.

import createTorchModule from './torch/torch.js';

const SRC_DIR = '/src';
const OUT_DIR = '/out';
const ARCHIVE_NAME = 'BattleShip.o2r';

self.onmessage = async (event) => {
  const { rom, recipe } = event.data || {};
  try {
    if (!(rom instanceof ArrayBuffer) || rom.byteLength === 0) throw new Error('no ROM bytes supplied');
    if (!recipe || !Array.isArray(recipe.files)) throw new Error('no recipe file list supplied');

    let lines = 0;
    let lastPost = 0;
    const t = { start: performance.now() };
    const samples = [];
    const version = recipe.version ? `?v=${encodeURIComponent(recipe.version)}` : '';
    const Module = await createTorchModule({
      locateFile: (path) => new URL(`./torch/${path}${version}`, self.location.href).href,
      print: (text) => {
        lines += 1;
        if (lines % 250 === 1) samples.push([lines, Math.round(performance.now() - t.start), text.slice(-60)]);
        const now = performance.now();
        if (now - lastPost > 100 || /Done!|error/i.test(text)) {
          lastPost = now;
          self.postMessage({ type: 'progress', lines, text });
        }
      },
      printErr: (text) => self.postMessage({ type: 'progress', lines, text: `stderr: ${text}` }),
    });

    t.moduleReady = performance.now();
    const FS = Module.FS;
    FS.mkdirTree(SRC_DIR);
    FS.mkdirTree(OUT_DIR);
    await Promise.all(recipe.files.map(async (path) => {
      const url = `${recipe.base}${path}${version}`;
      const response = await fetch(url);
      if (!response.ok) throw new Error(`fetch ${url}: ${response.status}`);
      const target = `${SRC_DIR}/${path}`;
      FS.mkdirTree(target.substring(0, target.lastIndexOf('/')));
      FS.writeFile(target, new Uint8Array(await response.arrayBuffer()));
    }));

    t.recipeStaged = performance.now();
    const bytes = new Uint8Array(rom);
    const ptr = Module._malloc(bytes.length);
    Module.HEAPU8.set(bytes, ptr);
    const started = performance.now();
    let rc;
    try {
      rc = Module.ccall('torch_extract_o2r', 'number',
        ['number', 'number', 'string', 'string'], [ptr, bytes.length, SRC_DIR, OUT_DIR]);
    } catch (thrown) {
      const message = typeof thrown === 'number' && Module.getExceptionMessage
        ? Module.getExceptionMessage(thrown).join(': ')
        : String(thrown?.message || thrown);
      throw new Error(`Torch threw: ${message}`);
    } finally {
      Module._free(ptr);
    }
    if (rc !== 0) throw new Error(`Torch exited with code ${rc}`);
    const outPath = `${OUT_DIR}/${ARCHIVE_NAME}`;
    if (!FS.analyzePath(outPath).exists) {
      throw new Error('Torch finished without writing an archive (unsupported ROM?)');
    }
    t.extracted = performance.now();
    const archive = FS.readFile(outPath);
    // Port-added stages' select-screen art is ROM-derived too; build it here
    // rather than shipping it. Failure is non-fatal: the engine falls back to
    // the ROM-resident sprite when a PNG is missing.
    const extras = [];
    try {
      const ptr2 = Module._malloc(bytes.length);
      Module.HEAPU8.set(bytes, ptr2);
      try {
        const n = Module.ccall('torch_derive_stage_assets', 'number', ['number', 'number', 'string'], [ptr2, bytes.length, OUT_DIR]);
        if (n > 0) {
          const dir = `${OUT_DIR}/assets/css_icons`;
          for (const name of FS.readdir(dir)) {
            if (!name.endsWith('.png')) continue;
            extras.push({ path: `/assets/css_icons/${name}`, bytes: FS.readFile(`${dir}/${name}`).slice().buffer });
          }
        }
      } finally {
        Module._free(ptr2);
      }
    } catch (error) {
      self.postMessage({ type: 'progress', lines, text: `stage assets skipped: ${error?.message || error}` });
    }
    const ms = performance.now() - started;
    // Copy out of the wasm heap so the buffer can be transferred.
    const out = archive.slice().buffer;
    t.readBack = performance.now();
    const timings = {
      moduleInitMs: Math.round(t.moduleReady - t.start),
      recipeStageMs: Math.round(t.recipeStaged - t.moduleReady),
      extractMs: Math.round(t.extracted - t.recipeStaged),
      readBackMs: Math.round(t.readBack - t.extracted),
      // Coarse timeline (every 250 log lines) for diagnosing slow builds.
      samples,
    };
    self.postMessage({ type: 'done', archive: out, extras, ms, lines, timings }, [out, ...extras.map((e) => e.bytes)]);
  } catch (error) {
    self.postMessage({ type: 'error', message: error?.message || String(error) });
  }
};
