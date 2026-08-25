/* main.js — bootstrap aplikasi: state, search picker, pipeline hitung rute */
'use strict';

// ---------- WorkerBridge: promise wrapper untuk worker WASM ----------
const WorkerBridge = (() => {
    let worker = null;
    let reqId = 0;
    const pending = new Map();
    const progressCbs = [];

    function init() {
        if (worker) return;
        worker = new Worker('js/wasm/worker-entry.js');
        worker.onmessage = ev => {
            const m = ev.data;
            if (m.type === 'ready') return;
            if (m.type === 'progress') {
                progressCbs.forEach(cb => cb(m.stage, m.pct));
                return;
            }
            const p = pending.get(m.requestId);
            if (!p) return;
            pending.delete(m.requestId);
            if (m.type === 'error') p.reject(new Error(m.message));
            else if (m.result !== undefined) p.resolve(m.result);
            else if (m.json !== undefined) p.resolve(m.json);
            else p.resolve(m.matrix);
        };
        worker.onerror = e => {
            pending.forEach(p => p.reject(new Error('Worker error: ' + e.message)));
            pending.clear();
        };
    }

    // request(type, payload) -> Promise. payload.requestId disuntikkan otomatis.
    function request(type, payload) {
        init();
        return new Promise((resolve, reject) => {
            const id = ++reqId;
            pending.set(id, { resolve, reject });
            worker.postMessage({ ...payload, type, requestId: id });
        });
    }

    function onProgress(cb) { progressCbs.push(cb); }
    return { request, onProgress };
})();

// ---------- State ----------
const App = {
    points: [],          // [{name, lat, lon}]
    depot: { name: '', lat: null, lon: null },
    pickingDepot: false,
    pickingPoint: false,
    computedRoutes: null,
    allCoords: null,
    running: false,
    runAbort: false,
};

const $ = id => document.getElementById(id);

// ---------- Dialog in-page (pengganti alert/confirm browser) ----------
const UiDialog = (() => {
    let resolving = null;

    function open(opts) {
        const modal = $('dialog-modal');
        $('dialog-title').textContent = opts.title;
        $('dialog-msg').textContent = opts.message;
        const okBtn = $('dialog-ok'), cancelBtn = $('dialog-cancel');
        cancelBtn.classList.toggle('hidden', !opts.confirm);
        cancelBtn.textContent = tl('cancel');
        okBtn.textContent = opts.confirm ? tl('ok') : tl('ok');
        modal.classList.remove('hidden');
        okBtn.focus();
        return new Promise(resolve => { resolving = resolve; });
    }

    function close(result) {
        const modal = $('dialog-modal');
        if (modal.classList.contains('hidden')) return;
        modal.classList.add('hidden');
        const r = resolving; resolving = null;
        if (r) r(result);
    }

    function setup() {
        $('dialog-ok').addEventListener('click', () => close(true));
        $('dialog-cancel').addEventListener('click', () => close(false));
        $('dialog-backdrop').addEventListener('click', () => close(false));
        document.addEventListener('keydown', e => {
            if ($('dialog-modal').classList.contains('hidden')) return;
            if (e.key === 'Escape') { e.preventDefault(); close(false); }
            else if (e.key === 'Enter') { e.preventDefault(); close(true); }
        });
    }

    return { open, close, setup };
})();

function uiAlert(message, title) {
    return UiDialog.open({ title: title || tl('warning'), message: String(message), confirm: false });
}
function uiConfirm(message, title) {
    return UiDialog.open({ title: title || tl('confirm_title'), message: String(message), confirm: true });
}

// Semua alert() di app jadi dialog in-page (bukan popup browser)
window.alert = uiAlert;

// ---------- Sesi otomatis (reload aman) ----------
const SESSION_KEY = 'rt_session_v1';

function saveSession() {
    try {
        localStorage.setItem(SESSION_KEY, JSON.stringify({
            points: App.points,
            depot: App.depot,
            teams: $('teams').value,
            mode: $('mode').value,
            profile: $('profile').value,
            restarts: $('restarts').value,
            roadLines: $('road-lines').checked,
            lang: currentLang
        }));
    } catch (e) { /* storage penuh/diblokir — abaikan */ }
}

function loadSession() {
    try {
        const raw = localStorage.getItem(SESSION_KEY);
        if (!raw) return false;
        const s = JSON.parse(raw);
        if (Array.isArray(s.points)) App.points = s.points;
        if (s.depot && typeof s.depot.lat === 'number') {
            App.depot = s.depot;
            $('depot-name').value = s.depot.name || '';
            $('depot-lat').value = s.depot.lat.toFixed(6);
            $('depot-lon').value = s.depot.lon.toFixed(6);
        }
        if (s.teams) $('teams').value = s.teams;
        if (s.mode) $('mode').value = s.mode;
        if (s.profile) $('profile').value = s.profile;
        if (s.restarts) $('restarts').value = s.restarts;
        if (typeof s.roadLines === 'boolean') $('road-lines').checked = s.roadLines;
        if (s.lang && s.lang !== currentLang) {
            currentLang = s.lang;
            localStorage.setItem('rp_lang', s.lang);
        }
        return true;
    } catch (e) { return false; }
}

function newSession() {
    uiConfirm(tl('new_session_confirm')).then(ok => {
        if (!ok) return;
        App.points = [];
        App.depot = { name: '', lat: null, lon: null };
        $('depot-name').value = '';
        $('depot-lat').value = '';
        $('depot-lon').value = '';
        $('teams').value = 1;
        try { localStorage.removeItem(SESSION_KEY); } catch (e) { }
        renderPointList();
        refreshMap();
        ResultPanel.hide();
        updateChecklist();
    });
}

function setupSession() {
    $('new-session-btn').addEventListener('click', newSession);
    // Simpan otomatis pada semua perubahan state
    const fields = [
        ['teams', 'change'], ['mode', 'change'], ['profile', 'change'],
        ['restarts', 'change'], ['road-lines', 'change'],
        ['depot-name', 'change'], ['depot-lat', 'change'], ['depot-lon', 'change']
    ];
    fields.forEach(([id, evt]) => $(id).addEventListener(evt, saveSession));
    const lnk = $('lang-toggle');
    const orig = lnk.onclick || (() => {});
    lnk.addEventListener('click', () => setTimeout(saveSession, 0));
}

document.addEventListener('DOMContentLoaded', () => {
    applyLang();
    if (loadSession()) {
        applyLang();
    }
    updateLangButtonLabel();
$('lang-toggle').addEventListener('click', cycleLang);
    window.rpApp = window.rpApp || {};
    window.rpApp.onLangChange = () => updateChecklist();

    // Map init (default: Bali)
    RouteMap.init(App.depot.lat || -8.65, App.depot.lon || 115.2321);
    refreshMap();

    // WorkerBridge progress -> progress bar
    WorkerBridge.onProgress(step => {
        if (step !== 11 && step !== 12 && step !== 13) return;
        const stageTxt = step === 11 ? tl('stage_compute') + ' (α/β)' : tl('stage_compute');
        setProgress(60 + (step - 11) * 20 / 3, stageTxt);
    });

    setupTabs();
    setupSearch();
    setupDepotSearch();
setupPoints();
    setupPaste();
    setupDepot();
    setupPointPick();
    setupRun();
    setupStepper();
    setupSession();
setupSteps();
    updateChecklist();
    setupHelp();
    setupOnboarding();
    setupOffline();
    setupPanelCollapse();
    setupBuildInfo();
    if ('serviceWorker' in navigator && (location.protocol === 'https:' || location.hostname === 'localhost' || location.hostname === '127.0.0.1')) {
        navigator.serviceWorker.register('sw.js').catch(err => console.warn('SW:', err));
    }
    UiDialog.setup();
    $('export-btn').addEventListener('click', exportCsv);
    Uploader.init($('csv-file'), $('csv-status'), locations => {
        const before = App.points.length;
        for (const l of locations) {
            App.points.push({ name: l.name, lat: l.lat, lon: l.lon });
        }
        $('csv-status').textContent = tl('point_added', { name: `${locations.length} lokasi` });
        renderPointList();
        refreshMap();
    });
});

// ---------- Tabs ----------
function setupTabs() {
    document.querySelectorAll('.tab').forEach(tab => {
        tab.addEventListener('click', () => {
            document.querySelectorAll('.tab').forEach(t => {
                t.classList.remove('active');
                t.setAttribute('aria-selected', 'false');
            });
            document.querySelectorAll('.tab-body').forEach(b => b.classList.remove('active'));
            tab.classList.add('active');
            tab.setAttribute('aria-selected', 'true');
            $('tab-' + tab.dataset.tab).classList.add('active');
        });
    });
}

// ---------- Search (Nominatim picker) ----------
// bindSearch: binding umum tombol cari -> dropdown hasil. onPick(it) dipanggil saat user memilih hasil.
function bindSearch(input, results, onPick) {
    let timer = null;
    let seq = 0;

    input.addEventListener('input', () => {
        clearTimeout(timer);
        const q = input.value.trim();
        if (q.length < 3) { results.classList.add('hidden'); return; }
        timer = setTimeout(async () => {
            const mySeq = ++seq;
            try {
                const items = await SearchProvider.search(q, currentLang);
                if (mySeq !== seq) return;
                results.innerHTML = '';
                if (!items.length) {
                    results.innerHTML = `<div class="sr-empty">${tl('sr_empty')}</div>`;
                }
                items.forEach(it => {
                    const div = document.createElement('div');
                    div.className = 'sr-item';
                    div.innerHTML = `<div class="sr-title">${escapeHtml(it.label)}</div><div class="sr-sub">${escapeHtml(it.sublabel)}</div>`;
                    div.addEventListener('click', () => {
                        onPick(it);
                        input.value = '';
                        results.classList.add('hidden');
                    });
                    results.appendChild(div);
                });
                results.classList.remove('hidden');
            } catch (e) {
                results.innerHTML = `<div class="sr-empty">${tl('error')}: ${escapeHtml(e.message)}</div>`;
                results.classList.remove('hidden');
            }
        }, 350);
    });

    document.addEventListener('click', e => {
        if (!results.contains(e.target) && e.target !== input) results.classList.add('hidden');
    });
}

function setupSearch() {
    bindSearch($('search-input'), $('search-results'), it => {
        // Tambah sebagai titik (kecuali jika sedang mode pilih depot)
        if (App.pickingDepot) {
            setDepot(it.label, it.lat, it.lon);
            exitDepotPick();
            alert(tl('depot_set_search'));
            RouteMap.centerOn(it.lat, it.lon, 15);
        } else {
            addPoint(it.label, it.lat, it.lon);
            RouteMap.highlightPoint(App.points.length - 1);
        }
    });
}

// Pencarian depot langsung di tab pengaturan (bawah koordinat depot)
function setupDepotSearch() {
    bindSearch($('depot-search-input'), $('depot-search-results'), it => {
        setDepot(it.label, it.lat, it.lon);
        $('depot-name').value = it.label;
        alert(tl('depot_set_search'));
        RouteMap.centerOn(it.lat, it.lon, 15);
    });
}

function setDepot(name, lat, lon) {
    App.depot = { name, lat, lon };
    $('depot-name').value = name;
    $('depot-lat').value = lat.toFixed(6);
    $('depot-lon').value = lon.toFixed(6);
    refreshMap();
    saveSession();
    updateChecklist();
}

function renderDepotPickBtn() {
    const pick = $('depot-pick');
    pick.innerHTML = App.pickingDepot
        ? `<svg class="icon" aria-hidden="true"><use href="#icon-x"/></svg><span>${tl('depot_cancel')}</span>`
        : `<svg class="icon" aria-hidden="true"><use href="#icon-locate"/></svg><span>${tl('depot_pick')}</span>`;
}

function exitDepotPick() {
    App.pickingDepot = false;
    renderDepotPickBtn();
    RouteMap.getMap().getContainer().classList.remove('crosshair');
}

// Mode "klik peta untuk menambah titik"
function setupPointPick() {
    const btn = $('point-pick');
    const map = RouteMap.getMap();

    const toggleOff = () => {
        App.pickingPoint = false;
        btn.classList.remove('active');
        map.getContainer().classList.remove('crosshair');
    };

    btn.addEventListener('click', () => {
        if (App.pickingPoint) { toggleOff(); return; }
if (App.pickingDepot) {
            App.pickingDepot = false;
            renderDepotPickBtn();
        }
        App.pickingPoint = true;
        btn.classList.add('active');
        map.getContainer().classList.add('crosshair');
        alert(tl('point_pick_hint'));
    });

    map.on('click', async e => {
        if (!App.pickingPoint) return;
        const { lat, lng } = e.latlng;
        let name = null;
        try { name = await SearchProvider.reverse(lat, lng, currentLang); } catch (err) { /* fallback */ }
        addPoint(name || `Titik ${App.points.length + 1}`, lat, lng);
        alert(tl('point_picked', { name: App.points[App.points.length - 1].name }));
        RouteMap.highlightPoint(App.points.length - 1);
    });
}

// ---------- Points list ----------
function refreshMap() {
    RouteMap.renderPoints(App.points, App.depot, {
        onPointMoved: (i, lat, lon) => {
            App.points[i].lat = lat; App.points[i].lon = lon;
            onGeoEdited();
        },
        onDepotMoved: (lat, lon) => {
            App.depot.lat = lat; App.depot.lon = lon;
            onGeoEdited();
        },
    });
}

function onGeoEdited() {
    if (App.computedRoutes) { App.computedRoutes = null; App.allCoords = null; ResultPanel.hide(); }
    $('depot-lat').value = Number.isFinite(App.depot.lat) ? App.depot.lat.toFixed(6) : '';
    $('depot-lon').value = Number.isFinite(App.depot.lon) ? App.depot.lon.toFixed(6) : '';
    renderPointList();
    saveSession();
    updateChecklist();
}

function addPoint(name, lat, lon) {
    // Tolak duplikat (jarak < 50 meter)
    const dup = App.points.find(p => Math.abs(p.lat - lat) < 0.0005 && Math.abs(p.lon - lon) < 0.0005);
    if (dup) { alert(tl('point_duplicate', { name: dup.name })); return; }
    App.points.push({ name, lat, lon });
    renderPointList();
    refreshMap();
    saveSession();
    updateChecklist();
}

function renderPointList() {
    const list = $('point-list');
    list.innerHTML = '';
    if (!App.points.length) {
        list.innerHTML = `<div class="empty-state">${tl('point_empty')}</div>`;
        return;
    }
App.points.forEach((p, i) => {
        const row = document.createElement('div');
        row.className = 'point-item';
        row.title = tl('point_pan');
        row.innerHTML = `
            <span class="dot" style="background:#64748b">${i + 1}</span>
            <span class="p-name" title="${escapeHtml(p.name)}">${escapeHtml(p.name)}</span>
            <span class="p-coords">${p.lat.toFixed(5)}, ${p.lon.toFixed(5)}</span>
            <button class="del" data-i="${i}" title="${tl('point_delete')}" aria-label="${tl('point_delete')} ${escapeHtml(p.name)}">
                <svg class="icon" aria-hidden="true"><use href="#icon-x"/></svg></button>`;
        row.querySelector('.del').addEventListener('click', e => {
            e.stopPropagation();
            App.points.splice(i, 1);
            renderPointList();
            RouteMap.renderPoints(App.points, App.depot);
            saveSession();
            updateChecklist();
        });
        row.addEventListener('click', () => {
            RouteMap.centerOn(p.lat, p.lon, 14);
        });
list.appendChild(row);
    });
}

function setupPoints() { renderPointList(); }

// ---------- Paste batch (tempel banyak titik koordinat) ----------
function setupPaste() {
    const input = $('paste-input');
    $('paste-example').addEventListener('click', () => {
        input.value = 'Warung Kopi; -8.510000, 115.290000\nToko Sembako; -8.541850, 115.332300\nMinimarket; -8.620000, 115.240000';
    });
    $('paste-add').addEventListener('click', () => {
        const lines = input.value.split(/\r?\n/);
        let added = 0, skipped = 0;
        for (const raw of lines) {
            const line = raw.trim();
            if (!line) continue;
            const m = /(-?\d{1,3}(?:\.\d+)?)\s*[,;\s]\s*(-?\d{1,3}(?:\.\d+)?)\s*$/.exec(line);
            if (!m) { skipped++; continue; }
            let lat = +m[1], lon = +m[2];
            if (lat > 90 || lat < -90) { const t = lat; lat = lon; lon = t; }
            if (!Number.isFinite(lat) || !Number.isFinite(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) { skipped++; continue; }
            const name = line.slice(0, m.index).replace(/[;,\s]+$/, '').trim();
            if (App.points.some(p => Math.abs(p.lat - lat) < 0.0005 && Math.abs(p.lon - lon) < 0.0005)) { skipped++; continue; }
            App.points.push({ name: name || `Titik ${App.points.length + 1}`, lat, lon });
            added++;
        }
        if (added) {
            renderPointList();
            refreshMap();
            saveSession();
            updateChecklist();
        }
        alert(added ? tl('paste_done', { added, skipped }) : tl('paste_none'));
        input.value = '';
    });
}

// ---------- Depot ----------
function setupDepot() {
const pick = $('depot-pick');
    pick.addEventListener('click', () => {
        App.pickingDepot = !App.pickingDepot;
        if (App.pickingPoint) { App.pickingPoint = false; $('point-pick').classList.remove('active'); }
        renderDepotPickBtn();
        if (App.pickingDepot) {
            alert(tl('depot_set_map'));
            const map = RouteMap.getMap();
            map.getContainer().classList.add('crosshair');
            map.once('click', e => {
                setDepot(App.depot.name, e.latlng.lat, e.latlng.lng);
                exitDepotPick();
            });
        } else {
            RouteMap.getMap().getContainer().classList.remove('crosshair');
        }
    });

    $('depot-name').addEventListener('change', e => { App.depot.name = e.target.value; });
    $('depot-lat').addEventListener('change', e => {
        const v = parseFloat(e.target.value);
        if (!isNaN(v)) { App.depot.lat = v; refreshMap(); }
    });
    $('depot-lon').addEventListener('change', e => {
        const v = parseFloat(e.target.value);
        if (!isNaN(v)) { App.depot.lon = v; refreshMap(); }
    });
}

function setupStepper() {
    const input = $('teams');
    const step = delta => {
        const v = Math.max(1, Math.min(50, (parseInt(input.value) || 1) + delta));
        input.value = v;
        input.dispatchEvent(new Event('change'));
    };
    $('teams-minus').addEventListener('click', () => step(-1));
    $('teams-plus').addEventListener('click', () => step(1));
}

// ---------- Checklist 3 langkah ----------
function suggestTeams() {
    const n = App.points.length;
    if (n <= 3) return 1;
    if (n <= 8) return 2;
    if (n <= 14) return 3;
    if (n <= 22) return 4;
    return Math.min(50, Math.ceil(n / 5));
}

function updateTeamsSuggest() {
    const btn = $('teams-suggest');
    const t = suggestTeams();
    if (App.points.length < 2) { btn.classList.add('hidden'); return; }
    btn.classList.remove('hidden');
    btn.innerHTML = `<svg class="icon" aria-hidden="true"><use href="#icon-help"/></svg><span>${tl('suggest_teams', { t, n: App.points.length })}</span>`;
}

function updateChecklist() {
    const hasPoints = App.points.length > 0;
    const hasDepot = Number.isFinite(App.depot.lat) && Number.isFinite(App.depot.lon) && !!App.depot.name;
    const steps = [hasPoints, hasDepot, true];
    document.querySelectorAll('#steps .step').forEach((el, i) => {
        el.classList.toggle('done', steps[i]);
    });
    // Penunjukkan langkah hanya tampil saat belum lengkap (navbar tunggal)
    $('steps').classList.toggle('hidden', steps.every(Boolean));
    const runBtn = $('run-btn');
    const ready = hasPoints && hasDepot;
    runBtn.disabled = !ready;
    runBtn.title = ready ? '' : tl('run_btn_tip');
    $('steps').title = tl('steps_tip');
    updateTeamsSuggest();
}

function setupSteps() {
    document.querySelectorAll('#steps .step').forEach(el => {
        el.addEventListener('click', () => {
            const s = parseInt(el.dataset.step);
            if (s === 1) {
                document.querySelector('[data-tab="points"]').click();
                setTimeout(() => $('search-input').focus(), 50);
            } else {
                document.querySelector('[data-tab="settings"]').click();
                setTimeout(() => (s === 2 ? $('depot-name') : $('teams')).focus(), 50);
            }
        });
    });
    $('teams-suggest').addEventListener('click', () => {
        $('teams').value = suggestTeams();
        $('teams').dispatchEvent(new Event('change'));
    });
}

// ---------- Offline banner ----------
function setupOffline() {
    const banner = $('offline-banner');
    const updater = () => {
        const off = !navigator.onLine;
        banner.classList.toggle('hidden', !off);
        banner.textContent = off ? tl('offline_banner') : '';
    };
    window.addEventListener('online', updater);
    window.addEventListener('offline', updater);
    updater();
}

// ---------- Badge commit build (fetch GitHub API, cache 30 menit) ----------
async function setupBuildInfo() {
    const el = $('build-commit');
    if (!el) return;
    const CACHE_KEY = 'rp_commit_sha';
    const TTL = 30 * 60 * 1000;

    function render(sha) {
        el.innerHTML = '';
        const a = document.createElement('a');
        a.href = 'https://github.com/Sparkplugx1904/route-planner/commit/' + sha;
        a.target = '_blank';
        a.rel = 'noopener';
        a.textContent = 'build ' + sha.slice(0, 7);
        el.appendChild(a);
        el.classList.remove('hidden');
    }

    try {
        const cached = JSON.parse(localStorage.getItem(CACHE_KEY) || 'null');
        if (cached && cached.sha && Date.now() - cached.t < TTL) {
            render(cached.sha);
            return;
        }
        const res = await fetch('https://api.github.com/repos/Sparkplugx1904/route-planner/commits/main');
        if (!res.ok) return;
        const j = await res.json();
        if (!j || !j.sha) return;
        try { localStorage.setItem(CACHE_KEY, JSON.stringify({ t: Date.now(), sha: j.sha })); } catch (e) { }
        render(j.sha);
    } catch (e) { /* offline / rate-limit — biarkan tersembunyi */ }
}

// ---------- Onboarding + contoh data ----------
function loadSampleData() {
    setDepot('Pasar Ubud (Depot)', -8.506944, 115.262500);
    [['Pasar Ubud', -8.506944, 115.262500],
     ['Istana Ubud', -8.509190, 115.265120],
     ['Goa Gajah', -8.523427, 115.287367],
     ['Campuhan Ridge Walk', -8.514164, 115.253879],
     ['Hutan Monyet Ubud', -8.518746, 115.259098],
     ['Tegallalang Rice Terrace', -8.431712, 115.278746],
     ['Pantai Sanur', -8.684040, 115.264261],
     ['Toko Kain Gianyar', -8.541873, 115.328601]].forEach(([name, lat, lon]) => {
        if (App.points.some(p => Math.abs(p.lat - lat) < 0.0005 && Math.abs(p.lon - lon) < 0.0005)) return;
        App.points.push({ name, lat, lon });
    });
    renderPointList();
    refreshMap();
    saveSession();
    updateChecklist();
}

function setupOnboarding() {
    const modal = $('onboard-modal');
    const close = () => {
        modal.classList.add('hidden');
        try { localStorage.setItem('rp_onboarded', '1'); } catch (e) { }
    };
    try {
        if (localStorage.getItem('rp_onboarded') === '1') return;
    } catch (e) { return; }
    modal.classList.remove('hidden');
    $('onboard-start').addEventListener('click', close);
    $('onboard-sample').addEventListener('click', () => { loadSampleData(); close(); });
    modal.querySelector('.modal-backdrop').addEventListener('click', close);
}

// ---------- Panel mobile: lipat / buka ----------
function setupPanelCollapse() {
    const panel = $('panel');
    const toggle = $('panel-toggle');
    const chev = toggle.querySelector('use');
    const apply = () => {
        const collapsed = panel.classList.toggle('collapsed');
        chev.setAttribute('href', collapsed ? '#icon-chevron-up' : '#icon-chevron-down');
        toggle.setAttribute('aria-expanded', String(!collapsed));
    };
    toggle.addEventListener('click', apply);
}

// ---------- Help / tutorial ----------
function setupHelp() {
    const open = () => $('help-modal').classList.remove('hidden');
    const close = () => $('help-modal').classList.add('hidden');
    $('help-btn').addEventListener('click', open);
    $('help-close').addEventListener('click', close);
    $('help-modal').querySelector('.modal-backdrop').addEventListener('click', close);
    document.addEventListener('keydown', e => {
        if (e.key === 'Escape' && !$('help-modal').classList.contains('hidden')) close();
    });
}

// Export daftar titik ke CSV (format sama dengan import: name, latitude, longitude)
function exportCsv() {
    if (!App.points.length) { alert(tl('export_empty')); return; }
    const esc = s => /[",\n]/.test(s) ? '"' + String(s).replace(/"/g, '""') + '"' : s;
    const lines = ['name, latitude, longitude'];
    App.points.forEach(p => lines.push(`${esc(p.name)}, ${p.lat}, ${p.lon}`));
    const blob = new Blob(['\ufeff' + lines.join('\n')], { type: 'text/csv;charset=utf-8' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'rute_titik_' + new Date().toISOString().slice(0, 10) + '.csv';
    a.click();
    URL.revokeObjectURL(a.href);
    $('csv-status').textContent = tl('export_done', { n: App.points.length });
}

// ---------- Progress UI ----------
function setProgress(pct, text) {
    $('progress-wrap').classList.remove('hidden');
    $('progress-fill').style.width = pct + '%';
    if (text) $('progress-text').textContent = text;
}

// ---------- Run pipeline ----------
function setRunning(on) {
    App.running = on;
    $('run-btn').classList.toggle('hidden', on);
    $('cancel-btn').classList.toggle('hidden', !on);
    if (!on) {
        $('run-btn').disabled = false;
        updateChecklist();
    }
}

function setupRun() {
    $('run-btn').addEventListener('click', runPipeline);
    $('cancel-btn').addEventListener('click', () => { App.runAbort = true; });
    $('results-close').addEventListener('click', () => ResultPanel.hide());
}

function abortIfNeeded() {
    if (App.runAbort) throw new Error('ABORTED');
}

async function runPipeline() {
    const n = App.points.length;
    if (!n) { alert(tl('no_points')); return; }
    if (!Number.isFinite(App.depot.lat) || !Number.isFinite(App.depot.lon) || !App.depot.name) {
        alert(tl('no_depot'));
        return;
    }
    const teams = Math.max(1, parseInt($('teams').value) || 1);
    if (teams > 50) { } // dibatasi di input

    if (!(await uiConfirm(tl('confirm_run', { n, t: teams })))) return;

    const profile = $('profile').value;
    const modeId = parseInt($('mode').value);
    const restarts = Math.max(1, parseInt($('restarts').value) || 10);
    const drawRoad = $('road-lines').checked;

    App.runAbort = false;
    setRunning(true);
    setProgress(0, tl('works'));

    try {
        // 1. Susun koordinat: depot pertama + titik
        const allCoords = [
            { name: App.depot.name, lat: App.depot.lat, lon: App.depot.lon },
            ...App.points
        ];

        // 2. Matrix jarak (cache IndexedDB / OSRM / fallback haversine WASM)
        setProgress(2, tl('stage_matrix') + '…');
        const { matrix, source, failed } = await OSRM.getMatrix(allCoords, profile, (done, total) => {
            abortIfNeeded();
            setProgress(2 + Math.round(done / total * 55), `${tl('stage_matrix')}… ${done}/${total}`);
        });
        if (failed) console.warn(tl('using_fallback'));
        abortIfNeeded();

        // 3. Komputasi WASM (clustering + TSP)
        setProgress(58, tl('stage_compute') + '…');
        const coordsArr = new Float64Array(n * 2 + 2); // [depot + points] -> [lat, lon,...]
        allCoords.forEach((c, i) => { coordsArr[i * 2] = c.lat; coordsArr[i * 2 + 1] = c.lon; });

        const result = await WorkerBridge.request('compute', {
            coords: coordsArr,
            matrix,
            n: allCoords.length,
            teams,
            modeId,
            restarts,
        });
        if (result.error) throw new Error(result.error);
        abortIfNeeded();

        // 4. Render peta + tabel
        App.computedRoutes = result;
        App.allCoords = allCoords;
        App.lastMatrix = matrix;
        App.lastProfile = profile;
        RouteMap.renderRoutes(allCoords, result.teamRoutes);
        ResultPanel.show(result.teamRoutes, result.stats, (tr, firstIdx) => {
            const loc = allCoords[firstIdx];
            RouteMap.centerOn(loc.lat, loc.lon, 14);
        }, { allCoords, matrix, n: allCoords.length, profile });

        // 5. Ambil polyline jalan (jika diaktifkan)
        if (drawRoad) {
            setProgress(75, tl('stage_polyline') + '…');
            abortIfNeeded();
            const tasks = [];
            for (const tr of result.teamRoutes) {
                const segs = [];
                for (let i = 0; i + 1 < tr.order.length; i++) {
                    const a = allCoords[tr.order[i]], b = allCoords[tr.order[i + 1]];
                    tasks.push(async () => {
                        const geo = await OSRM.getGeometryCached(a, b, profile);
                        return { teamId: tr.teamId, geo, straight: [[a.lat, a.lon], [b.lat, b.lon]] };
                    });
                }
            }
            // Jalankan dengan concurrency 6
            const results = await runConcurrent(tasks, 6, done => {
                setProgress(78 + Math.round(done / Math.max(1, tasks.length) * 20), tl('stage_polyline') + `… ${done}/${tasks.length}`);
            });

            // Kelompokkan per tim
            const byTeam = {};
            results.forEach(r => {
                (byTeam[r.teamId] = byTeam[r.teamId] || []).push(r);
            });
            for (const teamId in byTeam) {
                const lines = byTeam[teamId].map(r => r.geo);
                const straights = byTeam[teamId].map(r => r.straight);
                RouteMap.setTeamLines(parseInt(teamId), lines.map((l, i) => l || straights[i]), straights);
            }
        }

        setProgress(100, `${tl('compute_done', { teams: result.teamRoutes.length, points: n })}`);
    } catch (e) {
        if (e && e.message === 'ABORTED') {
            App.runAbort = false;
            setProgress(0, tl('cancelled'));
            setRunning(false);
            return;
        }
        console.error(e);
        setProgress(0, `${tl('error')}: ${e.message}`);
        alert(`${tl('error')}: ${e.message}`);
    } finally {
        if (App.running) setRunning(false);
        $('run-btn').disabled = false;
        updateChecklist();
    }
}

async function runConcurrent(tasks, limit, onProgress) {
    const results = new Array(tasks.length);
    let next = 0, done = 0;
    const workers = Array.from({ length: Math.min(limit, tasks.length) }, async () => {
        while (next < tasks.length) {
            if (App.runAbort) return;
            const i = next++;
            try { results[i] = await tasks[i](); }
            catch (e) { results[i] = { teamId: -1, geo: null, straight: [[0, 0], [0, 0]] }; }
            done++;
            if (onProgress) onProgress(done);
        }
    });
    await Promise.all(workers);
    return results.filter(r => r && r.teamId !== -1);
}
