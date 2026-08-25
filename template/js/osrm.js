/* osrm.js — fetch matrix jarak & polyline rute dari OSRM publik.
 * - Matrix: chunk 60 koordinat per request (sama seperti native).
 * - Cache IndexedDB (key = hash koordinat + profile).
 * - Fallback: haversine matrix dari WASM (waspadai rate-limit/CORS).
 */
'use strict';

const OSRM = (() => {
    const BASE = 'https://router.project-osrm.org';
    const CHUNK = 60;

    // Decode Google polyline -> [[lat, lon], ...]
    function decodePolyline(encoded) {
        const coords = [];
        let index = 0, lat = 0, lng = 0;
        while (index < encoded.length) {
            let b, shift = 0, result = 0;
            do {
                b = encoded.charCodeAt(index++) - 63;
                result |= (b & 0x1f) << shift;
                shift += 5;
            } while (b >= 0x20);
            const dlat = ((result & 1) ? ~(result >> 1) : (result >> 1));
            lat += dlat;
            shift = 0; result = 0;
            do {
                b = encoded.charCodeAt(index++) - 63;
                result |= (b & 0x1f) << shift;
                shift += 5;
            } while (b >= 0x20);
            const dlng = ((result & 1) ? ~(result >> 1) : (result >> 1));
            lng += dlng;
            coords.push([lat * 1e-5, lng * 1e-5]);
        }
        return coords;
    }

    // Fetch satu chunk matrix. isi result[srcIdx][j] km (0 jika sama).
    // allCoords: [{lat, lon}], sourceIdx: Array<number> (indeks global).
    async function fetchChunk(allCoords, sources, profile) {
        const coordsStr = allCoords.map(c => c.lon.toFixed(6) + ',' + c.lat.toFixed(6)).join(';');
        const url = `${BASE}/table/v1/${profile}/${coordsStr}?sources=${sources.join(';')}&annotations=distance`;
        const res = await fetch(url);
        if (!res.ok) throw new Error('OSRM HTTP ' + res.status);
        const j = await res.json();
        if (j.code !== 'Ok') throw new Error('OSRM code: ' + j.code);
        const data = j.distances || j.durations;
        const rows = sources.map((_, i) => {
            const row = new Float64Array(allCoords.length);
            for (let k = 0; k < allCoords.length; k++) {
                row[k] = data[i][k] / 1000.0; // meter -> km
            }
            return row;
        });
        return rows;
    }

    // Build matrix penuh (Float64Array n*n, km, depot index 0).
    // progressCb(selesai, total)
    async function fetchMatrix(coords, profile, progressCb) {
        const n = coords.length;
        const matrix = new Float64Array(n * n);
        const chunks = [];
        for (let s = 0; s < n; s += CHUNK) chunks.push(Array.from({ length: Math.min(CHUNK, n - s) }, (_, i) => s + i));

        let done = 0;
        for (const chunk of chunks) {
            const rows = await fetchChunk(coords, chunk, profile);
            rows.forEach((row, i) => matrix.set(row, chunk[i] * n));
            done += chunk.length;
            if (progressCb) progressCb(done, n);
        }
        return matrix;
    }

    // Polyline rute antara dua titik; threw -> fallback garis lurus.
    async function fetchGeometry(from, to, profile) {
        const url = `${BASE}/route/v1/${profile}/${from.lon.toFixed(6)},${from.lat.toFixed(6)};${to.lon.toFixed(6)},${to.lat.toFixed(6)}?overview=full&geometries=polyline`;
        const res = await fetch(url);
        if (!res.ok) throw new Error('OSRM HTTP ' + res.status);
        const j = await res.json();
        if (j.code !== 'Ok') throw new Error('OSRM code: ' + j.code);
        return decodePolyline(j.routes[0].geometry);
    }

    // Matrix lengkap dengan cache + fallback haversine (dari WASM).
    // returns {matrix: Float64Array, source: 'cache'|'osrm'|'haversine', failed?: bool}
    async function getMatrix(coords, profile, progressCb) {
        const key = matrixCacheKey(coords, profile);
        const cached = await IDBCache.get(key);
        if (cached) {
            if (progressCb) progressCb(coords.length, coords.length);
            return { matrix: cached, source: 'cache' };
        }
        try {
            const matrix = await fetchMatrix(coords, profile, progressCb);
            IDBCache.set(key, matrix);
            return { matrix, source: 'osrm' };
        } catch (e) {
            console.warn('OSRM matrix gagal, fallback haversine:', e);
            const matrix = await haversineFromWasm(coords);
            return { matrix, source: 'haversine', failed: true };
        }
    }

    // Panggil WASM haversine_matrix_fill. coords -> Float64Array n*2 [lat, lon, ...]
    function haversineFromWasm(coords) {
        const n = coords.length;
const arr = new Float64Array(n * 2);
    coords.forEach((c, i) => { arr[i * 2] = c.lat; arr[i * 2 + 1] = c.lon; });
    return WorkerBridge.request('haversine', { coords: arr, n });
    }

    // Polyline per segmen dengan cache; null artinya pakai garis lurus.
    async function getGeometryCached(from, to, profile) {
        const key = polylineCacheKey(from, to, profile);
        const cached = await IDBCache.get(key);
        if (cached !== undefined) return cached;
        try {
            const geo = await fetchGeometry(from, to, profile);
            IDBCache.set(key, geo);
            return geo;
        } catch (e) {
            console.warn('Fetch polyline gagal:', e);
            return null;
        }
    }

    return { fetchMatrix, fetchGeometry, getMatrix, getGeometryCached, decodePolyline };
})();