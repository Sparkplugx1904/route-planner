/* map.js — inisialisasi Leaflet, layer titik, render hasil rute */
'use strict';

const RouteMap = (() => {
    let map = null;
    let pointLayer = null;      // layer titik yang dimasukkan user (belum dirute)
    let routeLayer = null;      // hasil rute per tim
    let depotMarker = null;
    let pointMarkers = [];      // [marker,...] sejajar index App.points
    let dimTeamId = null;       // tim yang di-sorot (rute lain diredupkan), null = normal

    const TEAM_COLORS = [
        '#e6194b', '#3cb44b', '#4363d8', '#f58231', '#911eb4', '#42d4f4',
        '#f032e6', '#808000', '#469990', '#9a6324', '#800000', '#000075',
        '#e6beff', '#a9a9a9', '#bfef45', '#fabed4', '#aaffc3', '#ffd8b1',
        '#dcbeff', '#000000'
    ];

    const DIM_OPACITY = 0.3 * 0.85 / 0.85; // 30% dari opacity normal 0.85
    function dimmed(op) { return +(op * 0.3).toFixed(3); }

    function applyDim(layer, isDepotLayer) {
        if (!layer.eachLayer) return;
        layer.eachLayer(child => {
            if (child.setOpacity) {
                child.setOpacity(isDepotLayer || !dimTeamId ? child.options._baseOp ?? 1 : dimmed(child.options._baseOp ?? 1));
            }
            if (child.setStyle && child instanceof L.Path) {
                const base = child.options._baseOp ?? 0.85;
                child.setStyle({ opacity: isDepotLayer || !dimTeamId ? base : dimmed(base) });
            }
        });
    }

    function init(centerLat, centerLon) {
        map = L.map('map').setView([centerLat, centerLon], 11);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            maxZoom: 19,
            attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
        }).addTo(map);
        pointLayer = L.layerGroup().addTo(map);
        routeLayer = L.layerGroup();
        return map;
    }

    function teamColor(teamId) {
        return TEAM_COLORS[teamId % TEAM_COLORS.length];
    }

    function numberIcon(color, num, cls) {
        const big = num > 99;
        const size = big ? 32 : 26;
        return L.divIcon({
            className: '',
            html: `<div class="${cls || 'team-marker'}" style="width:${size}px;height:${size}px;font-size:${big ? 13 : 11}px;background:${color};">${num}</div>`,
            iconSize: [size, size],
            iconAnchor: [size / 2, size / 2]
        });
    }

    // Tampilkan titik yang dimasukkan user (belum dirute) dengan nomor urut.
    // Pin bisa digeser untuk memindahkan titik/depot; cbs.onPointMoved(i,lat,lon) & cbs.onDepotMoved(lat,lon)
    function renderPoints(points, depot, cbs) {
        if (!map) return;
        pointLayer.clearLayers();
        pointMarkers = [];
        cbs = cbs || {};

        const depotOk = !!(depot && Number.isFinite(depot.lat) && Number.isFinite(depot.lon));

        // Depot marker
        if (depotMarker) { depotMarker.remove(); depotMarker = null; }
        if (depotOk) {
            const depotPopup = () => `<b>START &amp; FINISH</b><br>${escapeHtml(depot.name || '')}<br><span class="popup-hint">${tl('drag_hint_depot')}</span>`;
            depotMarker = L.marker([depot.lat, depot.lon], {
                icon: L.divIcon({
                    className: '',
                    html: '<div class="depot-marker" style="width:28px;height:28px;">S</div>',
                    iconSize: [28, 28], iconAnchor: [14, 14]
                }),
                draggable: true
            }).addTo(map).bindPopup(depotPopup());
            depotMarker.on('dragend', () => {
                const { lat, lng } = depotMarker.getLatLng();
                depotMarker.setPopupContent(depotPopup());
                if (cbs.onDepotMoved) cbs.onDepotMoved(lat, lng);
            });
        }

        points.forEach((p, i) => {
            const m = L.marker([p.lat, p.lon], { icon: numberIcon('#64748b', i + 1), draggable: true });
            const popupHtml = () => `<b>${escapeHtml(p.name)}</b><br>${p.lat.toFixed(5)}, ${p.lon.toFixed(5)}<br><span class="popup-hint">${tl('drag_hint_point')}</span>`;
            m.bindPopup(popupHtml());
            m.on('dragend', () => {
                const { lat, lng } = m.getLatLng();
                m.setPopupContent(`<b>${escapeHtml(p.name)}</b><br>${lat.toFixed(5)}, ${lng.toFixed(5)}<br><span class="popup-hint">${tl('drag_hint_point')}</span>`);
                if (cbs.onPointMoved) cbs.onPointMoved(i, lat, lng);
            });
            pointLayer.addLayer(m);
            pointMarkers.push(m);
        });

        // Fit bounds (hanya koordinat valid: titik user + depot bila terisi)
        const valid = [...points, ...(depotOk ? [depot] : [])]
            .filter(p => Number.isFinite(p.lat) && Number.isFinite(p.lon));
        if (valid.length) map.fitBounds(valid.map(p => [p.lat, p.lon]), { padding: [40, 40], maxZoom: 14 });
    }

    // Render hasil: rute per tim + marker bernomor + titik kembali ke depot.
    // allCoords: [{name, lat, lon}] index 0 = depot. teamRoutes: [{teamId, order, distanceKm, numVisits, workload}]
    function renderRoutes(allCoords, teamRoutes) {
        if (!map) return;
        routeLayer.clearLayers();
        pointLayer.clearLayers();
        dimTeamId = null;

        const depot = allCoords[0];
        if (depotMarker) { depotMarker.remove(); depotMarker = null; }
        depotMarker = L.marker([depot.lat, depot.lon], {
            icon: L.divIcon({
                className: '',
                html: '<div class="depot-marker" style="width:28px;height:28px;">S</div>',
                iconSize: [28, 28], iconAnchor: [14, 14]
            })
        }).addTo(map).bindPopup(`<b>START &amp; FINISH</b><br>${escapeHtml(depot.name)}`);

        const bounds = [[depot.lat, depot.lon]];

        for (const tr of teamRoutes) {
            const color = teamColor(tr.teamId);
            const layer = L.layerGroup();
            let seq = 1;

            for (let i = 0; i < tr.order.length; i++) {
                const gi = tr.order[i];
                const loc = allCoords[gi];
                bounds.push([loc.lat, loc.lon]);
                if (gi === 0) continue; // depot

                const m = L.marker([loc.lat, loc.lon], { icon: numberIcon(color, seq) })
                    .bindPopup(`<b>${tl('result_team', { n: tr.teamId + 1 })}</b><br>Stop ${seq}: ${escapeHtml(loc.name)}`);
                layer.addLayer(m);
                seq++;
            }

            // Polyline (ditambahkan async oleh main.js via updateRouteLines)
            layer._routeData = {
                teamId: tr.teamId,
                order: tr.order,
                color,
            };
            routeLayer.addLayer(layer);
        }

        map.fitBounds(bounds, { padding: [40, 40], maxZoom: 12 });
        if (!map.hasLayer(routeLayer)) routeLayer.addTo(map);
    }

    // Tambahkan garis polyline ke layer tim (dipanggil setelah fetch geometry)
    function setTeamLines(teamId, lines, straightFallback) {
        routeLayer.eachLayer(layer => {
            const data = layer._routeData;
            if (!data || data.teamId !== teamId) return;
            lines.forEach(pairs => {
                if (pairs && pairs.length >= 2) {
                    L.polyline(pairs, { color: data.color, weight: 4, opacity: 0.85 }).addTo(layer);
                } else {
                    // fallback garis lurus antar dua titik
                    L.polyline(straightFallback, { color: data.color, weight: 4, opacity: 0.85, dashArray: '5,10' }).addTo(layer);
                }
            });
        });
        // garis baru harus ikut status dim saat ini
        refreshDim();
    }

    // Sorot satu tim: semua rute tim lain diredupkan ke ~30% opacity.
    // Klik tim sama lagi (atau highlight(null)) -> kembali normal.
    function highlightTeam(teamId) {
        dimTeamId = (dimTeamId === teamId) ? null : teamId;
        refreshDim();
        return dimTeamId; // null berarti sekarang normal
    }

    function clearHighlight() {
        dimTeamId = null;
        refreshDim();
    }

    function refreshDim() {
        if (!routeLayer) return;
        routeLayer.eachLayer(layer => {
            const data = layer._routeData;
            if (!data) return;
            const active = !dimTeamId || data.teamId === dimTeamId;
            layer.eachLayer(child => {
                if (child instanceof L.Path) {
                    // polyline rute
                    child.setStyle({ opacity: active ? 0.85 : 0.255 }); // 0.85 x 30%
                } else if (child.setOpacity) {
                    // divIcon marker bernomor
                    child.setOpacity(active ? 1.0 : 0.3);
                }
            });
        });
        // depot selalu tampak penuh (titik start/finish bersama)
        if (depotMarker && depotMarker.setOpacity) depotMarker.setOpacity(1.0);
    }

    function centerOn(lat, lon, zoom) {
        map.setView([lat, lon], zoom || 14);
    }

    // Sorot titik terbaru: pan ke marker + buka popup (umpan balik setelah cari/klik)
    function highlightPoint(i) {
        const m = pointMarkers[i];
        if (!m) return;
        map.panTo(m.getLatLng());
        m.openPopup();
    }

    function getMap() { return map; }

    // Sederhanakan: dapatkan pasangan koordinat untuk garis lurus dari semua rute
    function straightLinesFor(tr, allCoords) {
        const segs = [];
        for (let i = 0; i + 1 < tr.order.length; i++) {
            const a = allCoords[tr.order[i]], b = allCoords[tr.order[i + 1]];
            segs.push([[a.lat, a.lon], [b.lat, b.lon]]);
        }
        return segs;
    }

    return { init, renderPoints, renderRoutes, setTeamLines, centerOn, highlightPoint, getMap, teamColor, straightLinesFor, highlightTeam, clearHighlight };
})();

function escapeHtml(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}
