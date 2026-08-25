/* i18n.js — engine i18n multi-bahasa.
 * Kamus per bahasa ada di file TERPISAH: js/lang/<code>.js (en = default/fallback).
 * Bahasa aktif ditentukan (urutan prioritas):
 *   1. URL param  ?lang=id / ?lang=en,es (daftar preferensi, dipilih yang tersedia)
 *   2. localStorage 'rp_lang'
 *   3. navigator.language(s) browser
 *   4. default 'en'
 */
'use strict';

const DEFAULT_LANG = 'en';
const AVAILABLE_LANGS = ['en', 'id', 'es']; // tambah bahasa baru: buat js/lang/<code>.js + daftar di sini

function pickLang(codes) {
    for (const raw of codes) {
        const base = String(raw || '').toLowerCase().split(/[-_]/)[0];
        if (AVAILABLE_LANGS.includes(base)) return base;
    }
    return DEFAULT_LANG;
}

// --- Deteksi bahsa awal: ?lang= > localStorage > browser ---
let currentLang = (() => {
    try {
        const q = new URLSearchParams(location.search).get('lang');
        if (q) {
            const wanted = q.split(',').map(s => s.trim()).filter(Boolean);
            const chosen = pickLang(wanted);
            localStorage.setItem('rp_lang', chosen);
            return chosen;
        }
    } catch (e) { /* ignore */ }
    try {
        const stored = localStorage.getItem('rp_lang');
        if (stored) return pickLang([stored]);
    } catch (e) { /* ignore */ }
    try {
        const nav = [...(navigator.languages || []), navigator.language].filter(Boolean);
        return pickLang(nav);
    } catch (e) { return DEFAULT_LANG; }
})();

// --- Akses kamus dengan fallback EN ---
const tl = (key, vars) => {
    let dict = window.I18N && I18N[currentLang];
    let s = (dict && dict[key]);
    if (s === undefined) {
        const en = window.I18N && I18N[DEFAULT_LANG];
        s = en ? en[key] : undefined;
    }
    if (s === undefined) s = key;
    if (vars) {
        for (const k in vars) s = s.replace(new RegExp('\\{' + k + '\\}', 'g'), vars[k]);
    }
    return s;
};

function applyLang() {
    document.querySelectorAll('[data-i18n]').forEach(el => {
        el.textContent = tl(el.dataset.i18n);
    });
    document.querySelectorAll('[data-i18n-ph]').forEach(el => {
        el.placeholder = tl(el.dataset.i18nPh);
    });
    document.querySelectorAll('input[placeholder]').forEach(el => {
        if (el.dataset.i18n) el.placeholder = tl(el.dataset.i18n);
    });
    document.querySelectorAll('[data-i18n-tip]').forEach(el => {
        el.title = tl(el.dataset.i18nTip);
    });
    document.documentElement.lang = currentLang;
}

// Ganti bahasa saat runtime + persist. Tidak dipakai tombol toggle lama —
// taxan di sini agar bisa dipanggil dari picker bahasa.
function setLang(lang) {
    currentLang = pickLang([lang]);
    try { localStorage.setItem('rp_lang', currentLang); } catch (e) { /* ignore */ }
    applyLang();
    updateLangButtonLabel();
    if (window.rpApp && rpApp.onLangChange) rpApp.onLangChange();
}

function updateLangButtonLabel() {
    const btn = document.getElementById('lang-toggle');
    if (!btn) return;
    const next = pickLang([...AVAILABLE_LANGS].filter(l => l !== currentLang));
    btn.textContent = `${currentLang.toUpperCase()} → ${next.toUpperCase()}`;
}

// Tombol header berputar ke bahasa berikutnya.
function cycleLang() {
    const others = AVAILABLE_LANGS.filter(l => l !== currentLang);
    setLang(others.length ? others[0] : DEFAULT_LANG);
}

// Label "Bahasa: X" lama sudah tidak ada; switch_lang tidak lagi dibutuhkan,
// tapi tetap disediakan agar pemanggil lama tidak error.
const toggleLang = cycleLang;
