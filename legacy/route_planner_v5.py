#!/usr/bin/env python3
"""
Efficient Multi-Stop Route Planner v5 (RUTE JALAN, round-trip, greedy regional explorer)
===========================================================================================
Alat GENERIK untuk membagi daftar TITIK KUNJUNGAN APA SAJA (sekolah, toko,
pelanggan, gudang, titik survei, dsb — bebas, tinggal ganti isi CSV-nya)
menjadi N tim/kurir/kelompok, lalu mencari urutan kunjungan paling efisien
di dalam tiap tim — SEMUA jarak yang dipakai untuk pembagian & pengurutan
adalah JARAK RUTE JALAN SUNGGUHAN (OSRM).

Semua titik kunjungan (nama, latitude, longitude) dibaca dari file CSV
lewat --csv (tidak ada lagi data yang di-hardcode di file ini), dan titik
keberangkatan/depot (SATU titik start & finish untuk SEMUA tim) diberikan
lewat --start-name/--start-lat/--start-lon — jadi skrip yang sama bisa
dipakai untuk kunjungan sekolah, rute pengiriman, kunjungan sales,
survei lapangan, atau kasus lain yang butuh rute round-trip paling hemat.

STRATEGI:
    1. SETIAP TIM PULANG-PERGI (round-trip): berangkat dari titik
       depot, mengunjungi semua titik yang ditugaskan, lalu KEMBALI
       LAGI ke depot. Dihitung dengan Held-Karp DP versi closed-tour
       -> tetap solusi OPTIMAL (bukan aproksimasi) untuk ukuran tim
       wajar (<=12-13 titik/tim).

    2. Pembagian tim TIDAK pakai k-means/centroid, tapi memakai
       strategi GREEDY REGIONAL GROWING (mirip algoritma Prim multi-
       sumber): pilih beberapa titik "jangkar" yang tersebar di
       seluruh area (farthest-point sampling di atas jarak JALAN
       sungguhan), lalu tumbuhkan tiap klaster selangkah demi
       selangkah — di setiap langkah, titik BELUM-DITUGASKAN yang
       PALING DEKAT ke klaster mana pun langsung diberikan ke klaster
       itu. Ini persis strategi "kalau di titik A ada B & C yang
       berdekatan, datangi yang paling hemat dulu, lalu jelajahi area
       itu" sebelum pindah ke area lain -> tim benar-benar menjelajah
       satu wilayah dulu sampai wilayahnya habis, baru berpindah.

    3. KEADILAN kini diukur dari JUMLAH TITIK per tim (dibuat seadil
       mungkin, selisih maksimal 1 titik antar tim — seperti pembagian
       tugas manusiawi ke kurir), BUKAN dari menyamakan jarak tempuh.
       Jarak tempuh dibiarkan mengikuti kondisi geografis nyata (tim
       yang areanya lebih renggang wajar jaraknya lebih jauh).

    4. Objektif optimasi berubah jadi MURNI meminimalkan TOTAL jarak
       seluruh tim (paling hemat bahan bakar secara keseluruhan),
       dipoles dengan local search "tukar titik antar tim" yang selalu
       MENJAGA jumlah titik tiap tim tetap seadil di awal (operasi
       tukar tidak pernah mengubah jumlah anggota tim).

    5. Paralelisme dipertahankan (fetch matriks OSRM, restart
       clustering, fetch geometri jalan) — tidak mengubah hasil, hanya
       mempercepat.

Cara pakai:
    python route_planner_v5.py --csv data/location.csv \\
        --start-name "Kantor Pusat" --start-lat -8.6507 --start-lon 115.2321 \\
        --teams 14 --output routes.html
"""

import argparse
import concurrent.futures
import csv
import heapq
import math
import os
import random
import time

import folium
import requests

# ---------------------------------------------------------------------------
# 1. DATA TITIK KUNJUNGAN — dibaca dari file CSV (lihat load_locations_from_csv()).
#    Tidak ada koordinat yang di-hardcode di file ini; semua titik kunjungan
#    datang dari --csv, dan CSV-nya bebas berisi apa saja: sekolah, toko,
#    pelanggan, gudang, titik survei, dll.
#
#    Format CSV yang diharapkan (header, boleh ada spasi setelah koma):
#        location, latitude, longitude
#        Toko A, -8.540357485383625, 115.32286253810481
#        ...
# ---------------------------------------------------------------------------


def load_locations_from_csv(csv_path):
    """Membaca daftar titik kunjungan (nama, latitude, longitude) dari CSV.

    Toleran terhadap:
      - spasi ekstra di sekitar koma / nama kolom header (skipinitialspace)
      - variasi huruf besar/kecil pada nama kolom header
      - beberapa nama kolom umum: location/sekolah/nama/name (nama),
        latitude/lat, longitude/lon/lng
      - tanda kutip nyasar yang kadang ikut ter-copy-paste (mis. `Nama"`)
      - baris kosong di akhir file
    """
    if not os.path.isfile(csv_path):
        raise FileNotFoundError(
            f"File CSV titik kunjungan tidak ditemukan: {csv_path}\n"
            f"Siapkan file CSV dengan kolom: location, latitude, longitude, "
            f"lalu jalankan lagi dengan --csv {csv_path}"
        )

    def clean_name(raw):
        return raw.strip().strip('"').strip()

    locations = []
    with open(csv_path, newline="", encoding="utf-8-sig") as f:
        reader = csv.reader(f, skipinitialspace=True)
        rows = [row for row in reader if any(cell.strip() for cell in row)]

    if not rows:
        raise ValueError(f"File CSV kosong: {csv_path}")

    header = [h.strip().strip('"').lower() for h in rows[0]]
    name_candidates = {"location", "lokasi", "sekolah", "school", "nama", "name", "place", "site"}
    lat_candidates = {"latitude", "lat"}
    lon_candidates = {"longitude", "lon", "lng"}

    def find_col(candidates):
        for idx, h in enumerate(header):
            if h in candidates:
                return idx
        return None

    name_idx = find_col(name_candidates)
    lat_idx = find_col(lat_candidates)
    lon_idx = find_col(lon_candidates)

    if name_idx is None or lat_idx is None or lon_idx is None:
        # Header tidak dikenali -> anggap baris pertama sudah berisi data,
        # pakai urutan kolom default: nama, latitude, longitude.
        data_rows = rows
        name_idx, lat_idx, lon_idx = 0, 1, 2
    else:
        data_rows = rows[1:]

    for row_no, row in enumerate(data_rows, start=2):
        if len(row) <= max(name_idx, lat_idx, lon_idx):
            print(f"[!] Baris {row_no} di {csv_path} dilewati (kolom kurang): {row}")
            continue
        name = clean_name(row[name_idx])
        if not name:
            continue
        try:
            lat = float(row[lat_idx].strip().strip('"'))
            lon = float(row[lon_idx].strip().strip('"'))
        except ValueError:
            print(f"[!] Baris {row_no} di {csv_path} dilewati (lat/lon tidak valid): {row}")
            continue
        locations.append((name, lat, lon))

    if not locations:
        raise ValueError(f"Tidak ada baris titik kunjungan yang valid terbaca dari {csv_path}")

    return locations

QUALITATIVE_PALETTE = [
    "#e6194b", "#3cb44b", "#4363d8", "#f58231", "#911eb4",
    "#42d4f4", "#f032e6", "#808000", "#469990", "#9A6324",
    "#800000", "#000075", "#e6beff", "#a9a9a9", "#bfef45",
    "#fabed4", "#aaffc3", "#ffd8b1", "#dcbeff", "#000000",
]

OSRM_BASE = "https://router.project-osrm.org"

# Ambang jumlah titik non-start per tim yang masih dihitung EXACT lewat
# Held-Karp DP (closed tour). Di atas ini dipakai heuristik NN + 2-opt.
EXACT_TSP_MAX_M = 12


# ---------------------------------------------------------------------------
# 2. JARAK RUTE JALAN SUNGGUHAN (OSRM), dengan fallback garis lurus
# ---------------------------------------------------------------------------
def haversine_km(a, b):
    lat1, lon1 = a
    lat2, lon2 = b
    r = 6371.0
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    x = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2) ** 2
    return 2 * r * math.asin(math.sqrt(x))


def fetch_osrm_matrix(coords, profile="driving", chunk_size=60, timeout=30, max_workers=4):
    n = len(coords)
    matrix = [[0.0] * n for _ in range(n)]
    chunks = [list(range(start, min(start + chunk_size, n))) for start in range(0, n, chunk_size)]
    all_coord_str = ";".join(f"{coords[i][1]},{coords[i][0]}" for i in range(n))

    def fetch_chunk(chunk_idx):
        sources_param = ";".join(str(i) for i in chunk_idx)
        url = (
            f"{OSRM_BASE}/table/v1/{profile}/{all_coord_str}"
            f"?sources={sources_param}&annotations=distance"
        )
        resp = requests.get(url, timeout=timeout)
        resp.raise_for_status()
        data = resp.json()
        if data.get("code") != "Ok":
            raise RuntimeError(f"OSRM merespons dengan error: {data.get('code')} — {data.get('message', '')}")
        return chunk_idx, data["distances"]

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(max_workers, len(chunks))) as ex:
            futures = [ex.submit(fetch_chunk, c) for c in chunks]
            for fut in concurrent.futures.as_completed(futures):
                chunk_idx, distances = fut.result()
                for row_i, global_i in enumerate(chunk_idx):
                    row = distances[row_i]
                    for j in range(n):
                        d = row[j]
                        matrix[global_i][j] = (
                            (d / 1000.0) if d is not None else haversine_km(coords[global_i], coords[j]) * 1.3
                        )
        return matrix
    except Exception as e:
        print(f"[!] Gagal mengambil jarak rute jalan dari OSRM: {e}")
        return None


def fetch_osrm_route_geometry(a, b, profile="driving", timeout=15):
    try:
        coord_str = f"{a[1]},{a[0]};{b[1]},{b[0]}"
        url = f"{OSRM_BASE}/route/v1/{profile}/{coord_str}?overview=full&geometries=geojson"
        resp = requests.get(url, timeout=timeout)
        resp.raise_for_status()
        data = resp.json()
        if data.get("code") != "Ok":
            return None
        coords_lonlat = data["routes"][0]["geometry"]["coordinates"]
        return [(lat, lon) for lon, lat in coords_lonlat]
    except Exception:
        return None


def haversine_matrix(coords, circuity=1.3):
    n = len(coords)
    return [
        [haversine_km(coords[i], coords[j]) * circuity if i != j else 0.0 for j in range(n)]
        for i in range(n)
    ]


# ---------------------------------------------------------------------------
# 3. TSP ROUND-TRIP DI DALAM SATU TIM (Held-Karp DP, closed tour)
#    Titik 0 dalam `indices` SELALU = titik depot, dipaksa jadi
#    titik AWAL sekaligus AKHIR (pulang-pergi).
# ---------------------------------------------------------------------------
_TSP_CACHE = {}


def route_length(matrix, indices, order):
    return sum(
        matrix[indices[order[i]]][indices[order[i + 1]]] for i in range(len(order) - 1)
    )


def _solve_tsp_exact_dp_closed(matrix, indices):
    """Held-Karp DP untuk TOUR TERTUTUP (berangkat & kembali ke posisi 0).
    Hasil selalu OPTIMAL (bukan aproksimasi)."""
    n = len(indices)
    m = n - 1
    if m == 0:
        return [0], 0.0

    NEG = float("inf")
    full_mask = (1 << m) - 1
    dp = [[NEG] * m for _ in range(1 << m)]
    parent = [[-1] * m for _ in range(1 << m)]

    d_start = [matrix[indices[0]][indices[k + 1]] for k in range(m)]
    for j in range(m):
        dp[1 << j][j] = d_start[j]

    dmat = [[matrix[indices[a + 1]][indices[b + 1]] for b in range(m)] for a in range(m)]

    for mask in range(1, 1 << m):
        row = dp[mask]
        for j in range(m):
            cur = row[j]
            if cur == NEG or not (mask & (1 << j)):
                continue
            dj = dmat[j]
            for k in range(m):
                if mask & (1 << k):
                    continue
                nmask = mask | (1 << k)
                nd = cur + dj[k]
                if nd < dp[nmask][k]:
                    dp[nmask][k] = nd
                    parent[nmask][k] = j

    d_return = [matrix[indices[k + 1]][indices[0]] for k in range(m)]
    best_j = min(range(m), key=lambda j: dp[full_mask][j] + d_return[j])
    best_dist = dp[full_mask][best_j] + d_return[best_j]

    order_rest = []
    mask, j = full_mask, best_j
    while j != -1:
        order_rest.append(j + 1)
        pj = parent[mask][j]
        mask ^= (1 << j)
        j = pj
    order_rest.reverse()
    return [0] + order_rest + [0], best_dist


def _solve_tsp_heuristic_closed(matrix, indices):
    """Fallback NN + 2-opt untuk tim besar (jarang terjadi), versi tour
    tertutup: posisi awal & akhir sama-sama dipaksa = 0."""
    n = len(indices)
    unvisited = set(range(1, n))
    order = [0]
    while unvisited:
        last = order[-1]
        nxt = min(unvisited, key=lambda j: matrix[indices[last]][indices[j]])
        order.append(nxt)
        unvisited.remove(nxt)
    order.append(0)

    n_full = len(order)
    improved = True
    while improved:
        improved = False
        for i in range(1, n_full - 2):
            for j in range(i + 1, n_full - 1):
                a, b, c, d = order[i - 1], order[i], order[j], order[j + 1]
                before = matrix[indices[a]][indices[b]] + matrix[indices[c]][indices[d]]
                after = matrix[indices[a]][indices[c]] + matrix[indices[b]][indices[d]]
                if after + 1e-9 < before:
                    order[i : j + 1] = reversed(order[i : j + 1])
                    improved = True
    return order, route_length(matrix, indices, order)


def solve_tsp(matrix, indices):
    """indices: [0]+list titik kunjungan (indeks global). Return: (order
    pulang-pergi, jarak total round-trip km). Cache di-key oleh HIMPUNAN titik
    kunjungan, bukan urutan list, supaya cache hit sering saat local search."""
    n = len(indices)
    if n <= 1:
        return [0], 0.0

    key = (indices[0], tuple(sorted(indices[1:])))
    cached = _TSP_CACHE.get(key)
    if cached is not None:
        order_global, dist = cached
        pos = {g: i for i, g in enumerate(indices)}
        order = [pos[g] for g in order_global]
        return order, dist

    m = n - 1
    if m <= EXACT_TSP_MAX_M:
        order, dist = _solve_tsp_exact_dp_closed(matrix, indices)
    else:
        order, dist = _solve_tsp_heuristic_closed(matrix, indices)

    _TSP_CACHE[key] = ([indices[o] for o in order], dist)
    return order, dist


def team_route_distance(full_matrix, local_location_indices):
    """Jarak round-trip (berangkat & kembali ke titik depot) untuk satu
    tim, dari daftar indeks LOKAL titik kunjungan (0..n_locations-1)."""
    if not local_location_indices:
        return 0.0
    idxs = [0] + [i + 1 for i in local_location_indices]
    if len(idxs) <= 1:
        return 0.0
    _, dist = solve_tsp(full_matrix, idxs)
    return dist


# ---------------------------------------------------------------------------
# 4. PEMBAGIAN TIM — GREEDY REGIONAL GROWING (bukan k-means)
#    Strategi: pilih beberapa titik kunjungan "jangkar" yang tersebar
#    (farthest-point sampling di atas jarak JALAN sungguhan antar titik),
#    lalu tumbuhkan tiap klaster selangkah demi selangkah: titik BELUM
#    DITUGASKAN yang PALING DEKAT ke klaster mana pun selalu diambil
#    duluan (paling hemat bahan bakar), sampai kapasitas tim (jumlah
#    titik, dibuat SEADIL mungkin) terpenuhi.
# ---------------------------------------------------------------------------
def fair_team_sizes(n_locations, k):
    """Bagi n_locations ke k tim seadil mungkin (selisih maksimal 1)."""
    base = n_locations // k
    rem = n_locations % k
    return [base + 1] * rem + [base] * (k - rem)


def farthest_point_seeds(location_matrix, k, start_idx, rng):
    """Farthest-point sampling di atas jarak JALAN sungguhan antar titik
    kunjungan, supaya titik jangkar tiap tim tersebar merata ke seluruh
    penjuru area (bukan menumpuk di satu tempat)."""
    n = len(location_matrix)
    seeds = [start_idx]
    min_dist = [location_matrix[start_idx][j] for j in range(n)]
    while len(seeds) < k:
        seed_set = set(seeds)
        candidates = [j for j in range(n) if j not in seed_set]
        if not candidates:
            break
        next_idx = max(candidates, key=lambda j: min_dist[j])
        seeds.append(next_idx)
        for j in range(n):
            if location_matrix[next_idx][j] < min_dist[j]:
                min_dist[j] = location_matrix[next_idx][j]
    return seeds


def greedy_regional_clustering(location_matrix, k, sizes, seed_indices):
    """Greedy region-growing ala Prim multi-sumber: setiap langkah, titik
    kunjungan belum-ditugaskan yang PALING DEKAT ke klaster mana pun
    langsung diambil oleh klaster itu (kalau kapasitasnya masih ada)."""
    n = len(location_matrix)
    assigned = [-1] * n
    capacities = sizes[:]
    clusters = {c: [] for c in range(k)}
    heap = []

    for c, s in enumerate(seed_indices):
        assigned[s] = c
        capacities[c] -= 1
        clusters[c].append(s)

    for c, s in enumerate(seed_indices):
        for p in range(n):
            if assigned[p] == -1:
                heapq.heappush(heap, (location_matrix[s][p], p, c))

    while heap:
        d, p, c = heapq.heappop(heap)
        if assigned[p] != -1 or capacities[c] <= 0:
            continue
        assigned[p] = c
        capacities[c] -= 1
        clusters[c].append(p)
        for q in range(n):
            if assigned[q] == -1:
                heapq.heappush(heap, (location_matrix[p][q], q, c))

    # Jaga-jaga kalau ada sisa (mis. kapasitas tim yang paling dekat sudah
    # penuh semua) -> masukkan ke tim dengan anggota terdekat & kapasitas sisa.
    remaining = [p for p in range(n) if assigned[p] == -1]
    for p in remaining:
        cand = [c for c in range(k) if capacities[c] > 0] or list(range(k))
        best_c = min(
            cand,
            key=lambda c: min((location_matrix[m][p] for m in clusters[c]), default=float("inf")),
        )
        assigned[p] = best_c
        capacities[best_c] -= 1
        clusters[best_c].append(p)

    return assigned


def _centroid_of(coords, indices):
    if not indices:
        return None
    lat = sum(coords[i][0] for i in indices) / len(indices)
    lon = sum(coords[i][1] for i in indices) / len(indices)
    return (lat, lon)


def local_search_refine(coords, full_matrix, assignment, k, n_locations, max_passes=10, near_teams=5):
    """Local search TUKAR (swap) titik kunjungan antar tim untuk
    meminimalkan TOTAL jarak round-trip seluruh tim. Operasi tukar TIDAK
    PERNAH mengubah jumlah titik tiap tim -> keadilan jumlah tugas tetap
    terjaga persis seperti hasil pembagian awal."""
    assignment = assignment[:]
    team_points = {c: [i for i in range(n_locations) if assignment[i] == c] for c in range(k)}
    dists = [team_route_distance(full_matrix, team_points[c]) for c in range(k)]
    best_total = sum(dists)

    for _ in range(max_passes):
        improved = False
        centroids = {c: _centroid_of(coords, team_points[c]) for c in range(k)}

        for i in range(n_locations):
            ti = assignment[i]
            candidate_teams = sorted(
                (c for c in range(k) if c != ti and centroids[c] is not None),
                key=lambda c: haversine_km(coords[i], centroids[c]),
            )[:near_teams]

            swapped = False
            for tj in candidate_teams:
                for j in team_points[tj][:]:
                    pts_ti = [m for m in team_points[ti] if m != i] + [j]
                    pts_tj = [m for m in team_points[tj] if m != j] + [i]
                    new_dist_ti = team_route_distance(full_matrix, pts_ti)
                    new_dist_tj = team_route_distance(full_matrix, pts_tj)
                    old_pair = dists[ti] + dists[tj]
                    new_pair = new_dist_ti + new_dist_tj
                    if new_pair + 1e-9 < old_pair:
                        assignment[i], assignment[j] = tj, ti
                        team_points[ti].remove(i)
                        team_points[tj].remove(j)
                        team_points[ti].append(j)
                        team_points[tj].append(i)
                        dists[ti], dists[tj] = new_dist_ti, new_dist_tj
                        best_total += new_pair - old_pair
                        centroids[ti] = _centroid_of(coords, team_points[ti])
                        centroids[tj] = _centroid_of(coords, team_points[tj])
                        improved = True
                        swapped = True
                        break
                if swapped:
                    break

        if not improved:
            break

    return assignment, best_total


def _restart_worker(args):
    """Satu percobaan lengkap: pilih jangkar acak -> greedy region growing
    -> local search swap. Sepenuhnya independen dari restart lain, aman
    dijalankan paralel tanpa mengubah hasil (hasil akhir tetap dipilih
    berdasar total jarak terkecil di antara semua restart)."""
    seed, coords, location_matrix, full_matrix, k, base_sizes, n_locations = args
    rng = random.Random(seed)
    start_idx = rng.randrange(n_locations)
    seed_indices = farthest_point_seeds(location_matrix, k, start_idx, rng)
    sizes = base_sizes[:]
    rng.shuffle(sizes)
    assignment = greedy_regional_clustering(location_matrix, k, sizes, seed_indices)
    assignment, total = local_search_refine(coords, full_matrix, assignment, k, n_locations)
    return assignment, total


def best_greedy_clustering(coords, location_matrix, full_matrix, k, n_locations, restarts=10, max_workers=None):
    base_sizes = fair_team_sizes(n_locations, k)
    tasks = [
        (seed, coords, location_matrix, full_matrix, k, base_sizes, n_locations)
        for seed in range(restarts)
    ]

    if max_workers is None:
        max_workers = min(restarts, os.cpu_count() or 1)

    best_assignment, best_total = None, float("inf")

    if max_workers > 1 and restarts > 1:
        with concurrent.futures.ProcessPoolExecutor(max_workers=max_workers) as ex:
            for assignment, total in ex.map(_restart_worker, tasks):
                if total < best_total:
                    best_total, best_assignment = total, assignment
    else:
        for t in tasks:
            assignment, total = _restart_worker(t)
            if total < best_total:
                best_total, best_assignment = total, assignment

    return best_assignment, best_total


# ---------------------------------------------------------------------------
# 5. RENDER PETA HTML — round-trip (garis balik ke start ikut digambar)
# ---------------------------------------------------------------------------
def color_for(i):
    if i < len(QUALITATIVE_PALETTE):
        return QUALITATIVE_PALETTE[i]
    rng = random.Random(i)
    return "#%06x" % rng.randint(0, 0xFFFFFF)


def build_map(all_locations, matrix, assignment, k, n_locations, output_path, draw_real_roads, profile="driving", http_workers=8):
    center_lat = sum(s[1] for s in all_locations) / len(all_locations)
    center_lon = sum(s[2] for s in all_locations) / len(all_locations)
    m = folium.Map(location=[center_lat, center_lon], zoom_start=12, tiles="cartodbpositron")

    team_routes = []
    all_segments = set()
    for team in range(k):
        indices = [0] + [i + 1 for i in range(n_locations) if assignment[i] == team]
        if len(indices) <= 1:
            continue
        order, dist = solve_tsp(matrix, indices)
        ordered_global_idx = [indices[o] for o in order]  # round-trip: elemen pertama & terakhir = 0
        team_routes.append((team, ordered_global_idx, dist))
        if draw_real_roads:
            for a_idx, b_idx in zip(ordered_global_idx[:-1], ordered_global_idx[1:]):
                all_segments.add((a_idx, b_idx))

    geometry_cache = {}
    if draw_real_roads and all_segments:
        def fetch_seg(pair):
            a_idx, b_idx = pair
            a = (all_locations[a_idx][1], all_locations[a_idx][2])
            b = (all_locations[b_idx][1], all_locations[b_idx][2])
            return pair, fetch_osrm_route_geometry(a, b, profile=profile)

        with concurrent.futures.ThreadPoolExecutor(max_workers=http_workers) as ex:
            for pair, geom in ex.map(fetch_seg, all_segments):
                geometry_cache[pair] = geom

    summary = []
    grand_total = 0.0
    for team, ordered_global_idx, dist in team_routes:
        grand_total += dist
        color = color_for(team)
        n_stops = len(ordered_global_idx) - 1  # tidak menghitung dobel titik start di akhir
        fg = folium.FeatureGroup(
            name=f"Tim {team + 1} — {n_stops} titik (pulang-pergi) — ~{dist:.1f} km (rute jalan)"
        )
        stop_names = []

        for seq, gi in enumerate(ordered_global_idx, start=1):
            name, lat, lon = all_locations[gi]
            if gi == 0:
                if seq == 1:
                    stop_names.append(f"START: {name}")
                    folium.Marker(
                        location=(lat, lon),
                        icon=folium.DivIcon(
                            html=f"""
                            <div style="position:relative;">
                                <div style="
                                    background:#111;color:#ffd700;border-radius:50%;
                                    width:26px;height:26px;text-align:center;line-height:26px;
                                    font-size:12px;font-weight:bold;border:2px solid #ffd700;
                                    box-shadow:0 0 5px rgba(0,0,0,0.7);">
                                    S
                                </div>
                                <div style="
                                    position:absolute;left:30px;top:2px;white-space:nowrap;
                                    background:rgba(255,255,255,0.92);padding:1px 5px;border-radius:3px;
                                    font-size:11px;border:1px solid #111;color:#111;
                                    font-family:Arial, sans-serif;font-weight:bold;">
                                    START/FINISH — {name}
                                </div>
                            </div>"""
                        ),
                        popup=f"<b>START & FINISH (semua tim)</b><br>{name}",
                    ).add_to(fg)
                else:
                    stop_names.append(f"FINISH: kembali ke {name}")
                continue

            stop_names.append(f"{seq}. {name}")
            folium.Marker(
                location=(lat, lon),
                icon=folium.DivIcon(
                    html=f"""
                    <div style="position:relative;">
                        <div style="
                            background:{color};color:white;border-radius:50%;
                            width:24px;height:24px;text-align:center;line-height:24px;
                            font-size:11px;font-weight:bold;border:2px solid white;
                            box-shadow:0 0 3px rgba(0,0,0,0.6);">
                            {seq}
                        </div>
                        <div style="
                            position:absolute;left:28px;top:2px;white-space:nowrap;
                            background:rgba(255,255,255,0.88);padding:1px 5px;border-radius:3px;
                            font-size:11px;border:1px solid {color};color:#222;
                            font-family:Arial, sans-serif;">
                            {name}
                        </div>
                    </div>"""
                ),
                popup=f"<b>Tim {team + 1}</b><br>Stop {seq}: {name}",
            ).add_to(fg)

        if draw_real_roads:
            for a_idx, b_idx in zip(ordered_global_idx[:-1], ordered_global_idx[1:]):
                geometry = geometry_cache.get((a_idx, b_idx))
                a = (all_locations[a_idx][1], all_locations[a_idx][2])
                b = (all_locations[b_idx][1], all_locations[b_idx][2])
                if geometry:
                    folium.PolyLine(geometry, color=color, weight=4, opacity=0.85).add_to(fg)
                else:
                    folium.PolyLine([a, b], color=color, weight=3, opacity=0.5, dash_array="6,6").add_to(fg)
        else:
            latlon = [(all_locations[gi][1], all_locations[gi][2]) for gi in ordered_global_idx]
            folium.PolyLine(latlon, color=color, weight=4, opacity=0.85, dash_array="6,6").add_to(fg)

        fg.add_to(m)
        summary.append((team + 1, n_stops, dist, stop_names))

    folium.LayerControl(collapsed=False).add_to(m)
    m.save(output_path)
    return summary, grand_total


# ---------------------------------------------------------------------------
# 6. MAIN
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description=(
            "Efficient Multi-Stop Route Planner v5 — bagi titik kunjungan apa saja "
            "(sekolah, toko, pelanggan, gudang, dll) ke beberapa tim, round-trip dari "
            "satu titik depot, dengan strategi greedy regional explorer."
        )
    )
    parser.add_argument("--teams", type=int, default=14, help="Jumlah tim (default: 14)")
    parser.add_argument("--output", type=str, default="routes.html", help="Nama file HTML output (default: routes.html)")
    parser.add_argument("--restarts", type=int, default=10, help="Jumlah percobaan pemilihan titik jangkar acak")
    parser.add_argument(
        "--profile", type=str, default="driving", choices=["driving", "bike", "foot"],
        help="Moda transportasi untuk OSRM (default: driving/mobil-motor)",
    )
    parser.add_argument(
        "--no-road-lines", action="store_true",
        help="Kalau dipakai, garis rute di peta digambar lurus saja (lebih cepat)",
    )
    parser.add_argument("--workers", type=int, default=None, help="Jumlah proses paralel untuk clustering")
    parser.add_argument("--http-workers", type=int, default=8, help="Jumlah thread paralel untuk request OSRM")
    parser.add_argument(
        "--csv", type=str, required=True,
        help="Path file CSV berisi titik kunjungan (kolom: location, latitude, longitude). Wajib diisi.",
    )
    parser.add_argument(
        "--start-name", type=str, required=True,
        help="Nama titik depot/keberangkatan bersama untuk semua tim (mis. 'Kantor Pusat').",
    )
    parser.add_argument(
        "--start-lat", type=float, required=True,
        help="Latitude titik depot/keberangkatan.",
    )
    parser.add_argument(
        "--start-lon", type=float, required=True,
        help="Longitude titik depot/keberangkatan.",
    )
    args = parser.parse_args()

    t0 = time.time()

    START_LOCATION = (args.start_name, args.start_lat, args.start_lon)

    print(f"Membaca titik kunjungan dari CSV: {args.csv}")
    LOCATIONS = load_locations_from_csv(args.csv)
    ALL_LOCATIONS = [START_LOCATION] + LOCATIONS
    print(f"    -> {len(LOCATIONS)} titik kunjungan terbaca.")

    start_coord = (START_LOCATION[1], START_LOCATION[2])
    all_coords = [start_coord] + [(s[1], s[2]) for s in LOCATIONS]
    n_locations = len(LOCATIONS)
    location_coords = all_coords[1:]

    print("Mengambil matriks jarak RUTE JALAN dari OSRM (paralel, per-chunk)...")
    matrix = fetch_osrm_matrix(all_coords, profile=args.profile, max_workers=args.http_workers)
    used_real_roads = matrix is not None
    if matrix is None:
        print("    -> Gagal. Pakai estimasi garis-lurus x1.3 sebagai fallback.")
        matrix = haversine_matrix(all_coords)
        location_matrix = haversine_matrix(location_coords)
    else:
        print("    -> Berhasil, memakai jarak rute jalan sungguhan dari OSRM.")
        location_matrix = [[matrix[i + 1][j + 1] for j in range(n_locations)] for i in range(n_locations)]

    print(
        f"Membagi {n_locations} titik kunjungan ke {args.teams} tim dengan strategi GREEDY REGIONAL "
        f"(paralel, {args.restarts} percobaan) — semua tim pulang-pergi dari "
        f"{START_LOCATION[0]}..."
    )
    assignment, best_total = best_greedy_clustering(
        location_coords, location_matrix, matrix, args.teams, n_locations,
        restarts=args.restarts, max_workers=args.workers,
    )

    print("Menyusun peta & mengambil geometri jalan (paralel)...")
    summary, grand_total = build_map(
        ALL_LOCATIONS, matrix, assignment, args.teams, n_locations, args.output,
        draw_real_roads=used_real_roads and not args.no_road_lines,
        profile=args.profile,
        http_workers=args.http_workers,
    )

    max_dist = max((d for _, _, d, _ in summary), default=0.0)
    label = "jarak rute jalan sungguhan (pulang-pergi)" if used_real_roads else "estimasi garis-lurus x1.3 (pulang-pergi)"
    print(f"\n=== Ringkasan Rute — {args.teams} Tim ({n_locations} titik kunjungan, round-trip) — {label} ===\n")
    for team_no, count, dist, stops in sorted(summary):
        flag = "  <-- terjauh" if abs(dist - max_dist) < 1e-6 else ""
        print(f"Tim {team_no}: {count} titik, jarak pulang-pergi ~{dist:.1f} km{flag}")
        for s in stops:
            print(f"    {s}")
        print()
    print(f"Jarak tim terjauh : {max_dist:.1f} km")
    print(f"Total seluruh tim : {grand_total:.1f} km  (nilai yang diminimalkan)")
    if not used_real_roads:
        print("\n[!] PERINGATAN: angka di atas MASIH ESTIMASI garis-lurus x1.3, bukan rute jalan")
        print("    sungguhan, karena OSRM tidak bisa diakses saat script ini dijalankan.")
    print(f"\nPeta interaktif disimpan di: {args.output}")
    print(f"Total waktu eksekusi: {time.time() - t0:.1f} detik")


if __name__ == "__main__":
    main()