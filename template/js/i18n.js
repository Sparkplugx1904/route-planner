/* i18n.js — engine i18n multi-bahasa.
 * Kamus per bahasa ada di file TERPISAH: js/lang/<code>.js (en = default/fallback).
 * Bahasa aktif ditentukan (urutan prioritas):
 *   1. URL param  ?lang=id / ?lang=en,es (daftar preferensi, dipilih yang tersedia)
 *   2. localStorage 'rp_lang'
 *   3. navigator.language(s) browser
 *   4. default 'en'
 *
 * Tombol bahasa di header berupa DROPDOWN: klik -> daftar bahasa -> pilih.
 */
'use strict';

const DEFAULT_LANG = 'en';
// Konfigurasi bahasa tersedia. Tambah bahasa baru:
//   1. Buat js/lang/<code>.js berisi window.I18N.<code> = { _meta:{native:'...'}, ... }
//   2. Muat file-nya di index.html (<script src="js/lang/<code>.js"></script>)
//   3. Daftarkan kode-nya di daftar ini + SHELL di sw.js
const AVAILABLE_LANGS = ['en', 'id', 'es', 'zh', 'hi', 'ar', 'fr', 'de', 'pt', 'ru', 'ja'];

function pickLang(codes) {
    for (const raw of codes) {
        const base = String(raw || '').toLowerCase().split(/[-_]/)[0];
        if (AVAILABLE_LANGS.includes(base)) return base;
    }
    return DEFAULT_LANG;
}

function langNativeName(code) {
    const dict = window.I18N && I18N[code];
    return (dict && dict._meta && dict._meta.native) || code.toUpperCase();
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

// Ganti bahasa saat runtime + persist.
function setLang(lang) {
    currentLang = pickLang([lang]);
    try { localStorage.setItem('rp_lang', currentLang); } catch (e) { /* ignore */ }
    applyLang();
    updateLangButtonLabel();
    refreshLangMenuActive();
    if (window.rpApp && rpApp.onLangChange) rpApp.onLangChange();
}

/* ---------- Dropdown picker bahasa ---------- */

function updateLangButtonLabel() {
    const label = document.getElementById('lang-current-label');
    if (label) label.textContent = langNativeName(currentLang);
}

function refreshLangMenuActive() {
    const menu = document.getElementById('lang-menu');
    if (!menu) return;
    menu.querySelectorAll('.lang-item').forEach(item => {
        const active = item.dataset.lang === currentLang;
        item.classList.toggle('active', active);
        item.setAttribute('aria-selected', active ? 'true' : 'false');
    });
}

function buildLangMenu() {
    const menu = document.getElementById('lang-menu');
    if (!menu) return;
    menu.innerHTML = '';
    AVAILABLE_LANGS.forEach(code => {
        const item = document.createElement('button');
        item.type = 'button';
        item.className = 'lang-item' + (code === currentLang ? ' active' : '');
        item.setAttribute('role', 'option');
        item.setAttribute('aria-selected', code === currentLang ? 'true' : 'false');
        item.dataset.lang = code;

        const nameEl = document.createElement('span');
        nameEl.className = 'lang-name';
        nameEl.textContent = langNativeName(code);

        const codeEl = document.createElement('span');
        codeEl.className = 'lang-code';
        codeEl.textContent = code.toUpperCase();

        item.append(nameEl, codeEl);
        item.addEventListener('click', () => {
            closeLangMenu();
            setLang(code);
        });
        menu.appendChild(item);
    });
}

function openLangMenu() {
    const btn = document.getElementById('lang-toggle');
    const menu = document.getElementById('lang-menu');
    if (!btn || !menu || !menu.classList.contains('hidden')) return;
    refreshLangMenuActive();
    menu.classList.remove('hidden');
    btn.setAttribute('aria-expanded', 'true');
}

function closeLangMenu() {
    const btn = document.getElementById('lang-toggle');
    const menu = document.getElementById('lang-menu');
    if (!menu || menu.classList.contains('hidden')) return;
    menu.classList.add('hidden');
    if (btn) btn.setAttribute('aria-expanded', 'false');
}

function isLangMenuOpen() {
    const menu = document.getElementById('lang-menu');
    return !!menu && !menu.classList.contains('hidden');
}

// Pasang interaksi dropdown: klik tombol buka/tutup, klik luar & Escape menutup.
function setupLangPicker() {
    const btn = document.getElementById('lang-toggle');
    const menu = document.getElementById('lang-menu');
    if (!btn || !menu) return;

    buildLangMenu();
    updateLangButtonLabel();

    btn.addEventListener('click', e => {
        e.stopPropagation();
        isLangMenuOpen() ? closeLangMenu() : openLangMenu();
    });
    document.addEventListener('click', e => {
        if (isLangMenuOpen() && !e.target.closest('.lang-picker')) closeLangMenu();
    });
    document.addEventListener('keydown', e => {
        if (e.key === 'Escape' && isLangMenuOpen()) {
            closeLangMenu();
            btn.focus();
        }
    });
}
