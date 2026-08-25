/* worker-entry.js — Web Worker yang memuat WASM (MODULARIZE) dan mengeksekusi:
 * parseCsv, haversine, compute. Laporan progress via postMessage.
 * Dimuat sebagai "classic" worker + importScripts agar createRoutePlanner global tersedia.
 */
'use strict';

function reportProgress(stage, pct) {
    self.postMessage({ type: 'progress', stage, pct });
}

let Module = null;
let modulePromise = null;

function loadModule() {
    if (modulePromise) return modulePromise;
    modulePromise = new Promise((resolve, reject) => {
        try {
            importScripts('../../wasm/route_planner_wasm.js');
            const factory = self.createRoutePlanner;
            if (!factory) { reject(new Error('createRoutePlanner tidak ditemukan')); return; }
            factory({ locateFile: p => new URL('../../wasm/' + p, self.location.href).href })
                .then(m => { Module = m; resolve(m); }).catch(reject);
        } catch (e) {
            reject(e);
        }
    });
    return modulePromise;
}

// Salin string ke memori WASM, kembalikan {ptr, str}
async function writeString(s) {
    await loadModule();
    const len = s.length;
    const ptr = Module._malloc(len);
    Module.HEAPU8.set(new TextEncoder().encode(s), ptr);
    return ptr;
}

// Baca JSON string dari pointer WASM lalu free
function readJson(ptr) {
    const s = Module.UTF8ToString(ptr);
    Module._free_ptr(ptr);
    return JSON.parse(s);
}

// Tulis Float64Array ke memori WASM, return ptr
function writeF64(arr) {
    const ptr = Module._malloc(arr.length * 8);
    Module.HEAPF64.set(arr, ptr / 8);
    return ptr;
}

self.onmessage = async (ev) => {
    const msg = ev.data;
    try {
        switch (msg.type) {
            case 'parseCsv': {
                await loadModule();
                const ptr = Module._malloc(msg.csv.length);
                Module.HEAPU8.set(new TextEncoder().encode(msg.csv), ptr);
                const out = Module._parse_csv(ptr, msg.csv.length);
                Module._free(ptr);
                self.postMessage({ type: 'parsed', requestId: msg.requestId, json: readJson(out) });
                break;
            }
            case 'haversine': {
                await loadModule();
                const n = msg.n;
                const inP = writeF64(msg.coords);
                const out = Module._malloc(n * n * 8);
                Module._haversine_matrix_fill(inP, out, n, 1.3);
                const matrix = Module.HEAPF64.slice(out / 8, out / 8 + n * n);
                Module._free(inP); Module._free(out);
                self.postMessage({ type: 'haversineResult', requestId: msg.requestId, matrix });
                break;
            }
            case 'compute': {
                await loadModule();
                const n = msg.n;
                const coordsP = writeF64(msg.coords);
                const matrixP = writeF64(msg.matrix);
                const out = Module._compute_routes_wasm(coordsP, matrixP, n, msg.teams, msg.modeId, msg.restarts);
                Module._free(coordsP); Module._free(matrixP);
                const result = readJson(out);
                self.postMessage({ type: 'computeResult', requestId: msg.requestId, result });
                break;
            }
            default:
                self.postMessage({ type: 'error', requestId: msg.requestId, message: 'Unknown message: ' + msg.type });
        }
    } catch (e) {
        self.postMessage({ type: 'error', requestId: msg.requestId, message: String(e && e.message || e) });
    }
};

self.postMessage({ type: 'ready' });