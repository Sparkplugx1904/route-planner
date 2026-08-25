/* idb-cache.js — wrapper IndexedDB untuk cache matrix jarak & polyline.
 * Store: 'osrm-cache' — key: 'matrix|<hash>'  -> Float64Array (n*n km)
 *                          'poly|<hash>'       -> JSON string array polyline
 * Hash = FNV-1a dari "lat,lon;" berurutan + profil; cache lokal browser (tidak perlu cocok dengan native).
 */
'use strict';

const IDBCache = (() => {
    const DB_NAME = 'route-planner-cache';
    const DB_VERSION = 1;
    const STORE = 'osrm-cache';

    let dbPromise = null;

    function openDB() {
        if (dbPromise) return dbPromise;
        dbPromise = new Promise((resolve, reject) => {
            if (!('indexedDB' in window)) { reject(new Error('IndexedDB tidak didukung')); return; }
            const req = indexedDB.open(DB_NAME, DB_VERSION);
            req.onupgradeneeded = () => {
                if (!req.result.objectStoreNames.contains(STORE)) {
                    req.result.createObjectStore(STORE);
                }
            };
            req.onsuccess = () => resolve(req.result);
            req.onerror = () => reject(req.error);
        });
        return dbPromise;
    }

    async function get(key) {
        try {
            const db = await openDB();
            return await new Promise((resolve, reject) => {
                const tx = db.transaction(STORE, 'readonly');
                const r = tx.objectStore(STORE).get(key);
                r.onsuccess = () => resolve(r.result);
                r.onerror = () => reject(r.error);
            });
        } catch { return undefined; }
    }

    async function set(key, value) {
        try {
            const db = await openDB();
            await new Promise((resolve, reject) => {
                const tx = db.transaction(STORE, 'readwrite');
                tx.objectStore(STORE).put(value, key);
                tx.oncomplete = () => resolve();
                tx.onerror = () => reject(tx.error);
            });
            return true;
        } catch { return false; }
    }

    return { get, set };
})();

function fnv1a(str) {
    let hash = 0x811c9dc5;
    for (let i = 0; i < str.length; i++) {
        hash ^= str.charCodeAt(i);
        hash = Math.imul(hash, 0x01000193) >>> 0;
    }
    return hash.toString(16).padStart(8, '0');
}

// Key untuk matrix: koordinat berurutan (depot pertama).
function matrixCacheKey(coords, profile) {
    return 'matrix|' + fnv1a(coords.map(c => c.lat.toFixed(6) + ',' + c.lon.toFixed(6)).join(';') + '|' + profile);
}

function polylineCacheKey(from, to, profile) {
    return 'poly|' + fnv1a(from.lat.toFixed(6) + ',' + from.lon.toFixed(6) + '>' + to.lat.toFixed(6) + ',' + to.lon.toFixed(6) + '|' + profile);
}