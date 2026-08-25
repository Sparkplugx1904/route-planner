/* sw.js — service worker: cache shell aplikasi agar tetap bisa dibuka offline */
const CACHE = 'rp-shell-v1';
const SHELL = [
    './',
    './index.html',
    './manifest.json',
    './css/style.css',
    './js/i18n.js',
    './js/search-provider.js',
    './js/idb-cache.js',
    './js/osrm.js',
    './js/upload.js',
    './js/map.js',
    './js/result-panel.js',
    './js/main.js',
    'https://unpkg.com/leaflet@1.9.4/dist/leaflet.css',
    'https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'
];

self.addEventListener('install', e => {
    e.waitUntil(
        caches.open(CACHE)
            .then(c => c.addAll(SHELL))
            .then(() => self.skipWaiting())
    );
});

self.addEventListener('activate', e => {
    e.waitUntil(
        caches.keys()
            .then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
            .then(() => self.clients.claim())
    );
});

self.addEventListener('fetch', e => {
    const req = e.request;
    const url = new URL(req.url);

    // Navigasi: network dulu, fallback ke shell saat offline
    if (req.mode === 'navigate') {
        e.respondWith(
            fetch(req).catch(() => caches.match('./index.html'))
        );
        return;
    }

    // Shell origin sendiri + asset leaflet CDN: cache-first, refresh di background
    if (url.origin === location.origin || url.origin === 'https://unpkg.com') {
        e.respondWith(
            caches.match(req).then(hit => {
                const net = fetch(req).then(res => {
                    if (res && res.ok) {
                        const clone = res.clone();
                        caches.open(CACHE).then(c => c.put(req, clone));
                    }
                    return res;
                }).catch(() => hit);
                return hit || net;
            })
        );
        return;
    }

    // Tile OSM & API (OSRM/Nominatim): biarkan network langsung
});