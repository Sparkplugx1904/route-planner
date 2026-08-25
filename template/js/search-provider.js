/* search-provider.js — provider pencarian alamat.
 * Abstraksi: tambahkan GoogleProvider nanti (Places Autocomplete) tanpa ubah UI.
 * Default saat ini: OSM Nominatim (gratis, tanpa API key).
 */
'use strict';

const SearchProvider = (() => {
    let lastRequestAt = 0;

    async function nominatimSearch(query, lang) {
        // Kebijakan Nominatim: maks 1 request/detik
        const wait = Math.max(0, 1000 - (Date.now() - lastRequestAt));
        if (wait > 0) await new Promise(r => setTimeout(r, wait));
        lastRequestAt = Date.now();

        const url = 'https://nominatim.openstreetmap.org/search'
            + '?format=jsonv2&addressdetails=0&limit=8&countrycodes=id'
            + '&accept-language=' + encodeURIComponent(lang === 'id' ? 'id' : 'en')
            + '&q=' + encodeURIComponent(query);

        const res = await fetch(url, { headers: { 'Accept': 'application/json' } });
        if (!res.ok) throw new Error('Nominatim HTTP ' + res.status);
        const data = await res.json();

        return data.map(r => ({
            label: r.display_name.split(',')[0] || r.display_name,
            sublabel: r.display_name,
            lat: parseFloat(r.lat),
            lon: parseFloat(r.lon),
        }));
    }

    // Interface: search(query) -> Promise<[{label, sublabel, lat, lon}]>
    async function search(query, lang) {
        return nominatimSearch(query, lang);
    }

    // Reverse geocode: lat/lon -> nama tempat (atau null)
    async function reverse(lat, lon, lang) {
        const wait = Math.max(0, 1000 - (Date.now() - lastRequestAt));
        if (wait > 0) await new Promise(r => setTimeout(r, wait));
        lastRequestAt = Date.now();

        const url = 'https://nominatim.openstreetmap.org/reverse'
            + '?format=jsonv2&zoom=18'
            + '&accept-language=' + encodeURIComponent(lang === 'id' ? 'id' : 'en')
            + '&lat=' + lat + '&lon=' + lon;

        const res = await fetch(url, { headers: { 'Accept': 'application/json' } });
        if (!res.ok) return null;
        const j = await res.json();
        return (j && (j.name || j.display_name)) || null;
    }

    return { search, reverse };
})();