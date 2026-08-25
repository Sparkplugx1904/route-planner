/* result-panel.js — ringkasan rute per tim + detail urutan kunjungan & export */
'use strict';

const ResultPanel = (() => {
    let teamRoutes = [];
    let opts = null;
    let expanded = -1;

    const el = id => document.getElementById(id);
    const SPEED_KMH = { driving: 30, bike: 15, foot: 5 };
    const STOP_MIN = 5;

    function fmtHr(min) {
        const m = Math.round(min);
        const h = Math.floor(m / 60), r = m % 60;
        if (h < 1) return `${r} mnt`;
        return h + ' jam' + (r ? ` ${r} mnt` : '');
    }

    function segKm(a, b) {
        const m = opts.matrix, n = opts.n;
        if (!m || !n) return null;
        const v = m[a * n + b];
        return Number.isFinite(v) ? v : null;
    }

    function estMinutes(km) {
        const speed = SPEED_KMH[opts.profile] || SPEED_KMH.driving;
        return km / speed * 60;
    }

    // Detail berurutan untuk satu tim: [{label, km}]
    function buildDetail(tr) {
        const out = [];
        for (let i = 0; i + 1 < tr.order.length; i++) {
            const a = tr.order[i], b = tr.order[i + 1];
            if (a === b) continue;
            const fromName = a === 0 ? 'S' : opts.allCoords[a].name;
            const toName = b === 0 ? 'S' : opts.allCoords[b].name;
            const km = segKm(a, b);
            out.push({ fromName, toName, km, toIdx: b });
        }
        return out;
    }

    function teamCircleText(tr) {
        const segs = buildDetail(tr);
        const km = tr.distanceKm;
        const min = estMinutes(km) + tr.numVisits * STOP_MIN;
        let txt = `${tl('result_team', { n: tr.teamId + 1 })} — ${tr.numVisits} titik, ${km.toFixed(1)} km, ±${fmtHr(min)}\n`;
        segs.forEach((s, i) => {
            txt += `${i + 1}. ${s.toName} (${s.km !== null ? s.km.toFixed(1) + ' km' : '? km'})\n`;
        });
        return txt;
    }

    function buildDetailHtml(tr, onSegClick) {
        const segs = buildDetail(tr);
        const color = RouteMap.teamColor(tr.teamId);
        const sChip = name => name === 'S' ? '<span class="depot-chip">S</span>' : escapeHtml(name);
        const rows = segs.map((s, i) => `
            <div class="rt-seg" data-b="${s.toIdx}" title="${tl('seg_pan')}">
                <span class="rt-seg-from" style="color:${color}">${sChip(s.fromName)}</span>
                <span class="rt-seg-arrow">→</span>
                <span class="rt-seg-to">${sChip(s.toName)}</span>
                <span class="rt-seg-km">${s.km !== null ? s.km.toFixed(1) + ' km' : '—'}</span>
            </div>`).join('');
        return `<div class="rt-detail" style="border-left:3px solid ${color}">
            ${rows}
            <div class="rt-detail-foot">±${fmtHr(estMinutes(tr.distanceKm) + tr.numVisits * STOP_MIN)} perjalanan</div>
        </div>`;
    }

    function attachSegClicks(wrapDiv, tr, onSegClick) {
        if (!onSegClick) return;
        wrapDiv.querySelectorAll('.rt-seg').forEach(seg => {
            seg.addEventListener('click', () => onSegClick(parseInt(seg.dataset.b)));
        });
    }

    function rowHtml(tr, stats, open) {
        const color = RouteMap.teamColor(tr.teamId);
        const dev = stats.avg > 0 ? (tr.workload - stats.avg) / stats.avg * 100 : 0;
        const badge = Math.abs(dev) > 10
            ? `<span class="badge ${dev >= 0 ? 'pos' : 'neg'}" title="${tl('badge_tip')}">${dev >= 0 ? '+' : ''}${Math.round(dev)}%</span>`
            : `<span class="badge ok" title="${tl('badge_tip')}">ok</span>`;
        return `
            <div class="rt-row ${open ? 'open' : ''}" data-ti="${tr.teamId}">
                <span class="color" style="background:${color}"></span>
                <span class="chevron">${open ? '▾' : '▸'}</span>
                <span class="r-name">${tl('result_team', { n: tr.teamId + 1 })}</span>
                <span class="r-meta">${tl('result_stops', { visits: tr.numVisits, dist: tr.distanceKm.toFixed(1) })}</span>
                <span class="r-work">${tr.workload.toFixed(1)} ${badge}</span>
                <span class="r-actions">
                    <button class="btn btn-sm" data-act="wa" title="${tl('wa_route')}" aria-label="${tl('wa_route')}">
                        <svg class="icon" aria-hidden="true"><use href="#icon-share"/></svg></button>
                    <button class="btn btn-sm" data-act="copy" title="${tl('copy_route')}" aria-label="${tl('copy_route')}">
                        <svg class="icon" aria-hidden="true"><use href="#icon-copy"/></svg></button>
                    <button class="btn btn-sm" data-act="dl" title="${tl('dl_route')}" aria-label="${tl('dl_route')}">
                        <svg class="icon" aria-hidden="true"><use href="#icon-download"/></svg></button>
                </span>
            </div>
            <div class="rt-detail-wrap ${open ? 'open' : ''}"></div>`;
    }

    function show(teamRoutes_, stats, onZoom, showOpts) {
        teamRoutes = teamRoutes_;
        opts = showOpts || {};
        expanded = -1;
        const wrap = el('results');
        wrap.classList.remove('hidden', 'hide');

        const nTeams = teamRoutes_.length;
        const nPoints = opts.allCoords ? opts.allCoords.length - 1 : 0;
        el('results-stats').textContent =
            `${tl('workload')}: Min=${stats.min.toFixed(1)}, Avg=${stats.avg.toFixed(1)}, Max=${stats.max.toFixed(1)} · ` +
            `${nTeams} ${tl('result_teams_word')} · ${nPoints} titik`;

        const table = el('results-table');
        table.innerHTML = '';
        const sorted = [...teamRoutes].sort((a, b) => a.workload - b.workload);
        for (const tr of sorted) {
            const frag = document.createElement('div');
            frag.innerHTML = rowHtml(tr, stats, false);
            const row = frag.firstElementChild;
            const wrapDiv = frag.children[1];
            row.addEventListener('click', e => {
                const act = e.target.closest('[data-act]');
                if (act && act.dataset.act === 'wa') {
                    shareTeam(tr);
                    return;
                }
                if (act && act.dataset.act === 'copy') {
                    copyTeam(tr);
                    return;
                }
                if (act && act.dataset.act === 'dl') {
                    downloadTeam(tr);
                    return;
                }
                if (e.target.closest('.badge')) return;
                // Sorot rute tim ini di peta: tim lain diredupkan ke ~30%.
                // Klik tim yang sama lagi -> semua rute kembali normal.
                RouteMap.highlightTeam(tr.teamId);
                const isOpen = expanded === tr.teamId;
                expanded = isOpen ? -1 : tr.teamId;
                wrapDiv.dataset.ti = tr.teamId;
                wrapDiv.innerHTML = isOpen ? '' : buildDetailHtml(tr, onZoom);
                attachSegClicks(wrapDiv, tr, onZoom);
                table.querySelectorAll('.rt-row').forEach(r => r.classList.toggle('open', r.dataset.ti === String(tr.teamId)));
                table.querySelectorAll('.rt-detail-wrap').forEach(w => w.classList.toggle('open', w.dataset.ti === String(tr.teamId)));
                if (!isOpen && onZoom) {
                    const first = tr.order[1];
                    if (first !== undefined) onZoom(tr, first);
                }
            });
            table.appendChild(frag);
        }
    }

    function shareTeam(tr) {
        const txt = teamCircleText(tr);
        const url = 'https://wa.me/?text=' + encodeURIComponent(txt);
        window.open(url, '_blank', 'noopener');
        el('results-stats').textContent = tl('route_wa');
    }

    function copyTeam(tr) {
        const txt = teamCircleText(tr);
        const done = () => el('results-stats').textContent = tl('route_copied');
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(txt).then(done).catch(() => fallbackCopy(txt, done));
        } else {
            fallbackCopy(txt, done);
        }
    }

    function fallbackCopy(txt, done) {
        const ta = document.createElement('textarea');
        ta.value = txt;
        ta.style.position = 'fixed'; ta.style.opacity = '0';
        document.body.appendChild(ta);
        ta.select();
        try { document.execCommand('copy'); done(); } catch (e) { }
        document.body.removeChild(ta);
    }

    function downloadTeam(tr) {
        const txt = teamCircleText(tr);
        const blob = new Blob(['\ufeff' + txt], { type: 'text/plain;charset=utf-8' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = `rute_tim_${tr.teamId + 1}.txt`;
        a.click();
        URL.revokeObjectURL(a.href);
        el('results-stats').textContent = tl('route_downloaded');
    }

    function hide() {
        el('results').classList.add('hide');
        if (RouteMap.clearHighlight) RouteMap.clearHighlight();
    }
    return { show, hide };
})();
