# PLAN: Web Version Route Planner v6 (C++ → WASM, Static)

> **PENTING:** File ini adalah sumber kebenaran selama proses build. Setiap kali terjadi
> compaction, BACA ULANG plan.md ini dari awal sampai akhir SEBELUM melanjutkan kerja.
> Update status setiap langkah selesai (tandai `[x]`).

## Misi (dari user, 20 pertanyaan sudah dijawab)

Buat versi web statis dari Route Planner v6 (C++ di-compile ke WASM) di folder **`template/`**,
agar user dapat dengan mudah menempatkan titik droppoint. Entry titik utama: user ketik alamat →
muncul deretan rekomendasi tempat dari OSM Nominatim (gratis, TANPA Google Maps Platform —
user tidak punya budget) → klik pilih → titik masuk daftar. Tetap ada upload CSV seperti aplikasi native.

## Keputusan arsitektur (jawaban user — JANGAN diubah tanpa konfirmasi)

| Aspek | Keputusan |
|---|---|
| Hosting | Static (GitHub Pages/Netlify). folder deploy = `template/` |
| Pencarian alamat | OSM Nominatim gratis (abstraksi provider, Google Places bisa ditambah nanti tanpa ubah UI) |
| Jarak jalan | router.project-osrm.org publik; cache IndexedDB; fallback haversine×1.3 |
| Input titik | Upload CSV + tambah droppoint manual via search picker |
| Depot | Default "Kantor Pusat" tersimpan, bisa diganti user via search |
| Jumlah tim | Input angka bebas, default 14 |
| Mode routing | Semua 6 mode (balanced, nearest, fuel-efficient, equal-distance, compact, balanced-distance) |
| Profil OSRM | Dropdown user pilih (driving/bike/foot) |
| Garis rute | Polyline jalan (default) + toggle ke garis lurus |
| Caching | Yes, IndexedDB (matrix + polyline), key = hash koordinat + profile |
| Bahasa | Bilingual toggle ID/EN |
| Output | Peta Leaflet + ringkasan workload per tim (min/avg/max, ±%) |
| Skenario | Multi-skenario (koran/media + sekolah, Gianyar Bali) |
| Perangkat | Responsif — HP penting |
| Skala | 100–300 titik/sesi |
| Toolchain | Boleh install Emscripten lokal (emsdk) |
| Struktur | Folder baru `template/` (C++ native TIDAK disentuh perilakunya) |
| UX komputasi | Progress bertahap (Web Worker + callback), UI tetap responsif |
| Google API | TIDAK dipakai (gratis saja) — provider abstrak |

## Konteks codebase (rekap, untuk orientasi cepat)

- Project: Route Planner v6, C++17, CMake. Root: `CMakeLists.txt`, `src/*.cpp`, `include/*.hpp`, `third_party/` (httplib.h, json.hpp, cxxopts.hpp).
- `src/main.cpp`: flow = load CSV → OSRM fetch matrix (chunk 60, cache disk `.osrm_cache`) → estimate weights → cluster_by_mode → MapBuilder::build_html_map (Leaflet HTML statis).
- Kelas: `Clustering::cluster_by_mode(mode, coords, location_matrix, full_matrix, teams, n, weights, restarts)`, `Clustering::estimate_weights(...)`, TSP via views: depot+sorted sets; `haversine_km/Matrix haversine_matrix`.
- Native memakai: `-march=native`, OpenSSL optional, pthread, ws2_32 (Windows), httplib (sockets — TIDAK bisa di browser).
- Ada juga Python pendahulu (`route_planner_v5/v6.py`) — tidak perlu disentuh.

## Status langkah

- [x] **L1. Install & verifikasi Emscripten** — emsdk 6.0.7 di C:\emsdk; `em++ --version` OK. Aktif via `emsdk_env.bat` / script build.
- [x] **L2. Refactor C++** — csv_parser (parse_locations_from_string + BOM strip), engine.hpp/cpp (compute_routes + build_team_routes + EngineProgress), main.cpp & map_builder.cpp pakai engine (`build_team_routes`), wasm_main.cpp (C-ABI: parse_csv, haversine_matrix_fill, compute_routes_wasm, free_ptr). Native build OK (89 lokasi, road distances jalan).
- [x] **L3. Build WASM** — template/wasm/CMakeLists.txt + build_wasm.bat/.sh; emcmake cmake -G Ninja (cmake 4.2.1 warning shared-lib diabaikan); artifact route_planner_wasm.js/.wasm di template/wasm/. Exports: _parse_csv,_haversine_matrix_fill,_compute_routes_wasm,_free_ptr,_malloc,_free + runtime HEAPF64,HEAPU8,UTF8ToString,stringToUTF8. Mode di-pass sbg int (0-5), untuk hindari string marshalling.
- [ ] **L4. Scaffold `template/`**: index.html + css/style.css (desktop: panel kiri + peta; mobile: bottom-sheet/drawer) + js/i18n.js (kamus ID/EN, persist localStorage, `tl()`)
- [ ] **L5. JS layer data**: js/search-provider.js, js/upload.js, js/idb-cache.js, js/osrm.js
- [ ] **L6. Worker + integrasi WASM**: js/wasm/worker-entry.js, protocol postMessage, progress
- [ ] **L7. UI peta & hasil**: js/map.js, js/result-panel.js, js/main.js, pipeline progress bar
- [ ] **L8. Uji end-to-end (Chrome MCP)**: serve template/ via python http.server; tes upload CSV sekolah.csv + search Nominatim + hitung; tes mobile; tes offline fallback
- [ ] **L9. README `template/README.md`**

## Catatan penting / perangkap

- `-march=native` hanya di CMakeLists root — CMakeLists WASM terpisah & TANPA itu.
- httplib.cpp tidak ikut WASM (sockets). Caching disk `.osrm_cache` tidak relevan di browser (pakai IndexedDB; hash cache boleh beda dari C++ native).
- Tree Roller: worker harus load wasm via path relatif dari worker location; pakai `new URL('route_planner_wasm.js', self.location.href)`.
- Nominatim policy: max 1 req/s, wajib atribusi OSM, header Accept-Language. Jangan spam.
- router.project-osrm.org: CORS umumnya OK; bila gagal → fallback haversine×1.3 (compute di WASM).
- i18n: semua string user-facing lewat `tl()`, tidak ada teks hardcoded bahasa.
- Fitur bonus (hanya jika waktu tersisa): drag marker, duplikat titik auto-tolak, export hasil CSV/JSON, peringatan jumlah tim > titik.

---

# FASE 2 — SOLVING KRITIK UX

> Sumber: `crtics_and_brainstorm.md` (kritik #1–#18, prioritas P0–P2).
> Aturan sesi: kerjakan berurutan S1→S16 (tandai `[x]` + catat hasil verifikasi di **L16**).
> Sebelum memulai fase/modul baru: BACA plan.md ini + `crtics_and_brainstorm.md` dari awal.
> Setiap S dianggap SELESAI hanya jika verifikasi headless (puppeteer, localhost:8123) lolos.

## Urutan eksekusi (alur solving)

```
Fondasi UX:   S1 -> S3 -> S4 -> S5 -> S6 -> S7 -> S8 -> S9 -> S10 -> S11 -> S12 -> S13 -> S15 -> S16
              (S2 paralel dekat S1)                          (S14 boleh paralel di S12)
```

- Fondasi UX: S1, S2, S3, S4 (wajib berurutan — saling bergantung).
- Nilai hasil: S5, S6, S7.
- Kontrol & edit data: S8, S9, S10, S11.
- Onboarding & offline: S12, S13, S14 (S14 boleh paralel).
- Pembersihan & regresi: S15, S16.

## Status Fase 2

- [ ] **S1. Input "Jumlah Kendaraan" ramah awam** — kritik #1
  - `index.html:82-84`: ganti `<input id="teams">` jadi stepper − / angka / + (min 1),
    default `1` (bukan 14); label → "Berapa kendaraan?" dengan sub-label "opsional".
  - `main.js` `runPipeline`: baca nilai dari stepper; bila `teams > titik` → saran dialog (S11).
  - `restarts` dipindah ke seksi Lanjutan (S2); tooltip: "Restart clustering = coba berulang
    agar pembagian makin adil, tapi makin lama."
  - Verifikasi: headless — klik +/− mengubah nilai; pipeline menerima nilai stepper.

- [ ] **S2. Mode Ringkas / Lanjutan (collapse)** — kritik #2
  - `index.html` tab Pengaturan: bungkus `mode`, `profile`, `road-lines`, `restarts`,
    `coord-row` (lat/lon) dalam akordeon `<details id="advanced">` judul
    "⚙️ Lanjutan (opsional)" — default **tertutup**.
  - Tambah i18n: `advanced_label`, `advanced_hint`, tooltip tiap field
    (mode → satu kalimat, profil → "jenis rute").
  - Verifikasi: headless — akordeon tertutup default; nilai field tetap terbaca oleh pipeline.

- [ ] **S3. Simpan sesi otomatis (reload aman)** — kritik #9
  - `main.js`: simpan `localStorage['rt_session_v1']` = `{ points, depot, teams, mode,
    profile, restarts, roadLines, lang }` — hook: `addPoint`, hapus titik, `setDepot`,
    `toggleLang`, dan event listener tiap field di `setupSettings`.
  - Saat `DOMContentLoaded`: restore sesi → render list + peta; tombol "🗑 Sesi Baru"
    (konfirmasi dialog in-page) di header panel Titik.
  - Verifikasi: headless — tambah titik+depot, reload halaman → masih ada; "Sesi Baru"
    mengosongkan.

- [ ] **S4. Wizard/checklist 3 langkah** — kritik #3
  - Strip status 3 chip di atas tab: `1 Titik ✓` `2 Mulai ✓` `3 Kendaraan ✓`
    (hijau saat siap, abu-abu belum).
  - `run-btn` **disabled** + tooltip "Lengkapi langkah dulu" bila belum lengkap;
    klik chip → buka tab terkait + fokus.
  - Dialog info awal (hanya sesi baru): "Langkah 1: cari alamat atau klik peta
    untuk menambah lokasi."
  - Verifikasi: headless — chip berubah sesuai state; run-btn enabled hanya saat lengkap.

- [ ] **S5. Peta ikut pengguna (fit bounds + pin hasil cari)** — kritik #6
  - `map.js`: setelah `renderPoints` dan `App.points.length > 0` → `map.fitBounds(bounds)`
    (kecuali sedang mode picking).
  - Hasil pencarian dipilih → marker sementara + `map.panTo`; popup "Titik ditambahkan".
  - Verifikasi: headless — bounds memuat semua titik; center berubah setelah pick search.

- [ ] **S6. Detail rute per tim (urutan kunjungan + estimasi)** — kritik #7, #8
  - `result-panel.js`: klik baris tim → detail expand: `1️⃣ Toko A → 2️⃣ Toko B → …`
    dari `tr.order[]` (depot = tanda S), jarak per segmen + kumulatif, **estimasi waktu**
    (km ÷ kecepatan profil: driving 30, bike 15, foot 5 km/j + 5 menit/titik).
  - Tooltip legenda badge: "+15% = beban di atas rata-rata tim; ok = seimbang."
  - Stats header: unit jelas — "beban ≈ jarak (km) + jumlah titik".
  - Verifikasi: headless — klik baris → 3+ item urutan tampil; total km konsisten.

- [ ] **S7. Export "lembar per pengemudi"** — kritik #11
  - Per tim: tombol `📋 Salin` (clipboard API) + `⬇ Unduh .txt` — format siap WA:
    `Rute Kurir 1 — 5 titik, 8.6 km, ±2 jam` lalu `1. Toko A`, `2. Toko B`, …
  - i18n: `copy_route`, `route_copied`, `route_downloaded`, `est_time`.
  - Verifikasi: headless — clipboard terbaca (grant permissions), download tercatat.

- [ ] **S8. Tombol Batal + progress granular** — kritik #13
  - `main.js`: flag `runAbort` (reset tiap run); tombol `Batal` tampil saat running
    (swap `run-btn`), klik → hentikan alur; hasil yang sudah terlanjur diabaikan.
  - `osrm.js` `fetchMatrix`: progress per chunk → "Mengambil jarak jalan… (12/50)".
  - Verifikasi: headless — run dengan 5 titik (network lambat via interception),
    klik Batal → progress berhenti, tombol kembali, UI responsif.

- [ ] **S9. Edit titik (drag marker + popup)** — kritik #10
  - `map.js`: marker `draggable: true`; `dragend` → update koordinat di `App.points` + list;
    obsoletkan cache matriks koordinat lama.
  - Popup marker: input nama (rename) + tombol hapus (konfirmasi dialog in-page).
  - Verifikasi: headless — drag via mousedown/move/up → koordinat list berubah;
    hapus via popup → list berkurang.

- [ ] **S10. Paste batch daftar titik** — kritik #12
  - Tombol `📋 Tempel daftar` di upload-box → dialog textarea: terima baris
    `nama,lat,lon` atau `lat,lon`; auto-skip header (`name`, `latitude`); laporan
    baris gagal per baris.
  - Verifikasi: headless — paste 3 baris valid + 1 invalid → 3 masuk, 1 gagal dilaporkan.

- [ ] **S11. Saran jumlah kendaraan** — kritik #4, #14 (parsial)
  - Saat `teams > titik`: dialog konfirmasi "X titik dengan Y kendaraan? Saran: pakai Z
    (≈8 titik/armada)" — tombol "Pakai saran" / "Tetap pakai Y".
  - Verifikasi: headless — 2 titik & teams=14 → saran muncul; "Tetap" → pipeline cap n=2.

- [ ] **S12. Onboarding 3 langkah + contoh data** — kritik #5, #18
  - First-run (flag localStorage): overlay panah → (1) titik, (2) depot, (3) tombol run;
    dismiss "Lewati".
  - Tombol `✨ Coba contoh data` (5 titik dekat Ubud + depot) — satu klik → langsung
    fit bounds, siap hitung.
  - Verifikasi: headless — flag kosong → overlay tampil; klik contoh → 5 titik + depot.

- [ ] **S13. Banner offline humanis + polish** — kritik #15, #17
  - `osrm.js`: saat fallback haversine → flag → chip di peta
    "⚠️ Luring — jarak diperkirakan garis lurus" (i18n) + saran aktifkan internet.
  - Tambah `template/favicon.svg` (atau data-URI di `<link rel="icon">`) — hilangkan 404.
  - Verifikasi: headless — intercept semua request eksternal → chip tampil;
    tidak ada error 404 di konsol.

- [ ] **S14. PWA offline shell** — kritik #16
  - Pindah Leaflet js/css ke `template/vendor/` (hapus CDN); `manifest.json` + `sw.js`
    (cache-first app shell + vendor).
  - Verifikasi: headless — total blokir jaringan → app tetap buka & hitung (WASM+haversine).

- [ ] **S15. i18n lengkap + help ringkas** — kritik #5, #8
  - Audit semua string user-facing (`alert(`, `textContent = '…'`, placeholder) → `tl()`;
    judul dialog pakai key `confirm_title`/`warning`.
  - Help modal → 3 langkah inti + bagian "detail teknis" (collapse).
  - Verifikasi: grep bebas string hardcoded di JS user-facing.

- [ ] **S16. Regresi menyeluruh & dokumen status** — seluruh kritik
  - Jalankan suite: `dlg_test.js`, `e2e_offline.js`, `e2e_v6.js`, `e2e_v3.js`,
    `e2e_421.js`, `e2e_mobile.js` — semua headless localhost:8123.
  - Catat hasil tiap suite di tabel L16.
  - Perbarui `crtics_and_brainstorm.md`: centang item selesai + tandai roadmap.

## L16. Hasil regresi (diisi saat S16 selesai)

| Suite | Status | Catatan |
|---|---|---|
| dlg_test | ⏳ | |
| e2e_offline | ⏳ | |
| e2e_v6 | ⏳ | |
| e2e_v3 | ⏳ | |
| e2e_421 | ⏳ | |
| e2e_mobile | ⏳ | |