// Node driver for the wasm build: stage manifest files into MEMFS, run main.
// Usage: node scripts/node-driver.js <web-dist-dir>
// Debugging aid for the boot livelock — under node the process can be
// sampled (`sample <pid>`), profiled, and signaled.
const fs = require('fs');
const path = require('path');

const dist = path.resolve(process.argv[2] || 'web-dist');
const manifest = JSON.parse(fs.readFileSync(path.join(dist, 'manifest.json'), 'utf8'));

global.Module = {
    preRun: [function () {
        const FS = global.Module.FS;
        for (const f of manifest.files) {
            const data = fs.readFileSync(path.join(dist, decodeURIComponent(f.url)));
            const dir = f.path.substring(0, f.path.lastIndexOf('/')) || '/';
            FS.mkdirTree(dir);
            FS.writeFile(f.path, data);
        }
        console.error('[driver] staged', manifest.files.length, 'files');
    }],
    onRuntimeInitialized: function () {
        console.error('[driver] runtime initialized, calling main; pid', process.pid);
        try {
            global.Module.callMain([]);
            console.error('[driver] callMain returned');
        } catch (e) {
            console.error('[driver] callMain threw:', e && (e.stack || e.message));
        }
    },
    print: (t) => console.log(t),
    printErr: (t) => console.error(t),
};

// The glue strips query strings itself; locateFile keeps wasm lookup in dist.
global.Module.locateFile = (p) => path.join(dist, p.split('?')[0]);

process.on('unhandledRejection', (e) => {
    console.error('[driver] unhandledRejection:', e && (e.stack || e.message || e));
});
process.on('uncaughtException', (e) => {
    console.error('[driver] uncaught:', e && (e.stack || e.message));
});

// Keep the event loop alive across async wasm instantiation, and trace exits.
const keepAlive = setInterval(() => {}, 1000);
process.on('beforeExit', (c) => console.error('[driver] beforeExit', c));
process.on('exit', (c) => console.error('[driver] exit', c));
global.Module.onAbort = (w) => console.error('[driver] onAbort:', w);
global.Module.monitorRunDependencies = (n) => console.error('[driver] runDeps =', n);
global.Module.preInit = [() => console.error('[driver] preInit fired')];
setTimeout(() => {
    const M = global.Module;
    console.error('[driver] 10s status: calledRun=', M.calledRun, 'keys=', Object.keys(M).slice(0, 20).join(','));
}, 10000);
console.error('[driver] loading glue…');
// require() would let the glue's hoisted `var Module` shadow ours — inject
// Module as a parameter instead so `typeof Module != 'undefined'` finds it.
const gluePath = path.join(dist, 'BattleShip.js');
const src = fs.readFileSync(gluePath, 'utf8');
new Function('Module', 'require', 'process', '__filename', '__dirname', src)(global.Module, require, process, gluePath, dist);
console.error('[driver] glue loaded (script tail reached)');
