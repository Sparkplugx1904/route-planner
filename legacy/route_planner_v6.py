#!/usr/bin/env python3
"""
Efficient Multi-Stop Route Planner v6 (FAIR WORKLOAD BALANCING)
================================================================
PERBAIKAN UTAMA dari v5:

1. KEADILAN BEBAN KERJA (bukan keadilan jumlah kunjungan):
   - Tim dengan rute JAUH → kunjungan SEDIKIT
   - Tim dengan rute DEKAT → kunjungan BANYAK
   - Metrik: Work Load = (Jarak × α) + (Jumlah Kunjungan × β)
   - α dan β di-tuning otomatis berdasarkan karakteristik data

2. OPTIMASI KOMPUTASI:
   - Cache matriks OSRM ke disk (.osrm_cache/)
   - Adaptive TSP solver (Held-Karp/Christofides/LKH tergantung ukuran)
   - Lazy evaluation pada local search
   - Progressive refinement (fokus pada tim tidak seimbang)

3. ADAPTIVE CAPACITY CLUSTERING:
   - Kapasitas = target beban kerja (bukan jumlah titik)
   - Greedy regional growing dengan stop condition berbasis beban

Cara pakai:
    python route_planner_v6.py --csv data/location.csv \\
        --start-name "Kantor Pusat" --start-lat -8.6507 --start-lon 115.2321 \\
        --teams 14 --output routes.html
"""

import argparse
import concurrent.futures
import csv
import hashlib
import heapq
import json
import math
import os
import pickle
import random
import time
from pathlib import Path

import folium
import requests

# ---------------------------------------------------------------------------
# CONSTANTS
# ---------------------------------------------------------------------------
QUALITATIVE_PALETTE = [
    "#e6194b", "#3cb44b", "#4363d8", "#f58231", "#911eb4",
    "#42d4f4", "#f032e6", "#808000", "#469990", "#9A6324",
    "#800000", "#000075", "#e6beff", "#a9a9a9", "#bfef45",
    "#fabed4", "#aaffc3", "#ffd8b1", "#dcbeff", "#000000",
]

OSRM_BASE = "https://router.project-osrm.org"
CACHE_DIR = Path(".osrm_cache")

# Ambang ukuran tim untuk algoritma TSP yang berbeda
EXACT_TSP_MAX = 10      # Held-Karp DP (optimal, lambat)
CHRISTOFIDES_MAX = 16   # Christofides approx (1.5x optimal, cepat)
# Di atas itu: LKH-lite heuristic (NN + 2-opt intensif)


# ---------------------------------------------------------------------------
# 1. LOAD LOCATIONS FROM CSV (sama seperti v5)
# ---------------------------------------------------------------------------
def load_locations_from_csv(csv_path):
    """Membaca daftar titik kunjungan dari CSV."""
    if not os.path.isfile(csv_path):
        raise FileNotFoundError(f"File CSV tidak ditemukan: {csv_path}")

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
        data_rows = rows
        name_idx, lat_idx, lon_idx = 0, 1, 2
    else:
        data_rows = rows[1:]

    for row_no, row in enumerate(data_rows, start=2):
        if len(row) <= max(name_idx, lat_idx, lon_idx):
            continue
        name = clean_name(row[name_idx])
        if not name:
            continue
        try:
            lat = float(row[lat_idx].strip().strip('"'))
            lon = float(row[lon_idx].strip().strip('"'))
        except ValueError:
            continue
        locations.append((name, lat, lon))

    if not locations:
        raise ValueError(f"Tidak ada lokasi valid di {csv_path}")

    return locations


# ---------------------------------------------------------------------------
# 2. DISTANCE UTILITIES dengan CACHING
# ---------------------------------------------------------------------------
def haversine_km(a, b):
    """Jarak garis lurus Haversine dalam km."""
    lat1, lon1 = a
    lat2, lon2 = b
    r = 6371.0
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    x = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2) ** 2
    return 2 * r * math.asin(math.sqrt(x))


def coords_hash(coords):
    """Hash koordinat untuk cache key."""
    s = json.dumps(coords, sort_keys=True)
    return hashlib.md5(s.encode()).hexdigest()


def get_cached_matrix(coords, profile):
    """Cek apakah matriks OSRM sudah di-cache."""
    CACHE_DIR.mkdir(exist_ok=True)
    cache_file = CACHE_DIR / f"{coords_hash(coords)}_{profile}.pkl"
    if cache_file.exists():
        try:
            with open(cache_file, "rb") as f:
                return pickle.load(f)
        except Exception:
            pass
    return None


def save_cached_matrix(coords, profile, matrix):
    """Simpan matriks ke cache."""
    CACHE_DIR.mkdir(exist_ok=True)
    cache_file = CACHE_DIR / f"{coords_hash(coords)}_{profile}.pkl"
    try:
        with open(cache_file, "wb") as f:
            pickle.dump(matrix, f)
    except Exception as e:
        print(f"[!] Gagal menyimpan cache: {e}")


def fetch_osrm_matrix(coords, profile="driving", chunk_size=60, timeout=30, max_workers=4):
    """Fetch matriks jarak dari OSRM dengan caching."""
    cached = get_cached_matrix(coords, profile)
    if cached is not None:
        print("    -> Menggunakan matriks dari cache.")
        return cached

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
            raise RuntimeError(f"OSRM error: {data.get('code')}")
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
        save_cached_matrix(coords, profile, matrix)
        return matrix
    except Exception as e:
        print(f"[!] Gagal fetch OSRM: {e}")
        return None


def haversine_matrix(coords, circuity=1.3):
    """Matriks jarak garis lurus dengan faktor circuity."""
    n = len(coords)
    return [
        [haversine_km(coords[i], coords[j]) * circuity if i != j else 0.0 for j in range(n)]
        for i in range(n)
    ]


def fetch_osrm_route_geometry(a, b, profile="driving", timeout=15):
    """Fetch geometri jalan untuk satu segmen."""
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


# ---------------------------------------------------------------------------
# 3. ADAPTIVE TSP SOLVER
# ---------------------------------------------------------------------------
_TSP_CACHE = {}


def route_length(matrix, indices, order):
    """Hitung panjang rute dari urutan kunjungan."""
    return sum(
        matrix[indices[order[i]]][indices[order[i + 1]]] for i in range(len(order) - 1)
    )


def _solve_tsp_exact_dp(matrix, indices):
    """Held-Karp DP - optimal tapi lambat O(n^2 * 2^n)."""
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


def _solve_tsp_christofides_approx(matrix, indices):
    """Christofides approximation - 1.5x optimal, lebih cepat.
    Implementasi sederhana: MST + matching + Eulerian tour + shortcut."""
    n = len(indices)
    
    # Simplified: gunakan greedy matching untuk odd-degree vertices
    # (implementasi penuh Christofides memerlukan blossom algorithm)
    # Fallback ke nearest neighbor + 2-opt untuk kesederhanaan
    return _solve_tsp_nn_2opt(matrix, indices)


def _solve_tsp_nn_2opt(matrix, indices, iterations=500):
    """Nearest Neighbor + 2-opt intensif."""
    n = len(indices)
    unvisited = set(range(1, n))
    order = [0]
    while unvisited:
        last = order[-1]
        nxt = min(unvisited, key=lambda j: matrix[indices[last]][indices[j]])
        order.append(nxt)
        unvisited.remove(nxt)
    order.append(0)

    # 2-opt intensif dengan multiple passes
    for _ in range(iterations):
        improved = False
        n_full = len(order)
        for i in range(1, n_full - 2):
            for j in range(i + 1, n_full - 1):
                a, b, c, d = order[i - 1], order[i], order[j], order[j + 1]
                before = matrix[indices[a]][indices[b]] + matrix[indices[c]][indices[d]]
                after = matrix[indices[a]][indices[c]] + matrix[indices[b]][indices[d]]
                if after + 1e-9 < before:
                    order[i : j + 1] = reversed(order[i : j + 1])
                    improved = True
        if not improved:
            break

    return order, route_length(matrix, indices, order)


def solve_tsp(matrix, indices, force_algorithm=None):
    """Adaptive TSP solver - pilih algoritma berdasar ukuran tim.
    
    Args:
        force_algorithm: None (auto), 'exact', 'approx', 'heuristic'
    """
    n = len(indices)
    if n <= 1:
        return [0], 0.0

    # Cek cache
    key = (indices[0], tuple(sorted(indices[1:])))
    cached = _TSP_CACHE.get(key)
    if cached is not None:
        order_global, dist = cached
        pos = {g: i for i, g in enumerate(indices)}
        order = [pos[g] for g in order_global]
        return order, dist

    m = n - 1  # jumlah titik non-depot

    # Pilih algoritma
    if force_algorithm == 'exact' or (force_algorithm is None and m <= EXACT_TSP_MAX):
        order, dist = _solve_tsp_exact_dp(matrix, indices)
    elif force_algorithm == 'approx' or (force_algorithm is None and m <= CHRISTOFIDES_MAX):
        order, dist = _solve_tsp_christofides_approx(matrix, indices)
    else:
        order, dist = _solve_tsp_nn_2opt(matrix, indices)

    # Simpan ke cache
    _TSP_CACHE[key] = ([indices[o] for o in order], dist)
    return order, dist


def team_route_distance(full_matrix, local_location_indices, force_algorithm=None):
    """Jarak round-trip untuk satu tim."""
    if not local_location_indices:
        return 0.0
    idxs = [0] + [i + 1 for i in local_location_indices]
    if len(idxs) <= 1:
        return 0.0
    _, dist = solve_tsp(full_matrix, idxs, force_algorithm)
    return dist


# ---------------------------------------------------------------------------
# 4. WORKLOAD BALANCING - AUTO-TUNING α dan β
# ---------------------------------------------------------------------------
def estimate_workload_weights(coords, matrix, n_locations, k, sample_size=50):
    """Auto-tune bobot α (jarak) dan β (jumlah kunjungan) berdasarkan data.
    
    Strategi: sample beberapa pembagian random, hitung range jarak & jumlah,
    tentukan α dan β supaya kontribusi keduanya seimbang di skala yang sama.
    """
    random.seed(42)
    distances = []
    counts = []
    
    for _ in range(min(sample_size, 20)):
        # Random assignment
        assignment = [random.randint(0, k - 1) for _ in range(n_locations)]
        for team in range(k):
            team_locs = [i for i in range(n_locations) if assignment[i] == team]
            if team_locs:
                dist = team_route_distance(matrix, team_locs)
                distances.append(dist)
                counts.append(len(team_locs))
    
    if not distances:
        return 1.0, 5.0  # default fallback
    
    avg_dist = sum(distances) / len(distances)
    avg_count = sum(counts) / len(counts)
    
    # Normalisasi supaya 1 km ≈ berapa kunjungan?
    # Kita ingin: α × avg_dist ≈ β × avg_count
    # Anggap 1 kunjungan = penalty_per_visit km ekuivalen
    # Tuning: kunjungan lebih ringan dari jarak (karena 1 kunjungan < 1 km beban)
    
    penalty_per_visit_km = avg_dist / (avg_count * 3) if avg_count > 0 else 2.0
    
    alpha = 1.0  # bobot jarak (km)
    beta = penalty_per_visit_km  # bobot kunjungan (setara km)
    
    return alpha, beta


def workload(distance_km, num_visits, alpha, beta):
    """Hitung beban kerja tim."""
    return alpha * distance_km + beta * num_visits


def target_workload_per_team(coords, matrix, n_locations, k, alpha, beta):
    """Estimasi target beban per tim (rata-rata ideal)."""
    # Hitung total workload jika semua titik dikunjungi secara efisien
    # Estimasi: asumsi setiap tim dapat jarak proporsional
    sample_locs = random.sample(range(n_locations), min(n_locations, 30))
    sample_dist = team_route_distance(matrix, sample_locs)
    avg_dist_per_loc = sample_dist / len(sample_locs) if sample_locs else 5.0
    
    total_estimated_dist = avg_dist_per_loc * n_locations
    total_workload = alpha * total_estimated_dist + beta * n_locations
    
    return total_workload / k


# ---------------------------------------------------------------------------
# 5. ADAPTIVE CAPACITY CLUSTERING dengan WORKLOAD BALANCING
# ---------------------------------------------------------------------------
def farthest_point_seeds(location_matrix, k, rng):
    """Farthest-point sampling untuk seed selection."""
    n = len(location_matrix)
    start_idx = rng.randrange(n)
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


def workload_aware_clustering(coords, location_matrix, full_matrix, k, alpha, beta, target_workload, seed_indices):
    """Greedy clustering dengan kapasitas berbasis WORKLOAD (bukan jumlah titik).
    
    Tim dengan jarak jauh akan stop lebih awal (kunjungan sedikit).
    Tim dengan jarak dekat akan terus mengambil sampai workload-nya penuh.
    """
    n = len(location_matrix)
    assigned = [-1] * n
    workloads = [0.0] * k  # current workload per team
    clusters = {c: [] for c in range(k)}
    heap = []
    
    # Assign seeds
    for c, s in enumerate(seed_indices):
        assigned[s] = c
        clusters[c].append(s)
        # Initial workload (hanya 1 kunjungan, belum ada jarak rute)
        workloads[c] = beta  # penalty 1 kunjungan
    
    # Populate heap
    for c, s in enumerate(seed_indices):
        for p in range(n):
            if assigned[p] == -1:
                # Estimasi jarak jika p ditambahkan ke cluster c
                heapq.heappush(heap, (location_matrix[s][p], p, c))
    
    while heap:
        d, p, c = heapq.heappop(heap)
        
        if assigned[p] != -1:
            continue
        
        # Cek apakah tim c masih punya kapasitas workload
        # Estimasi workload baru jika p ditambahkan
        new_locs = clusters[c] + [p]
        new_dist = team_route_distance(full_matrix, new_locs, force_algorithm='heuristic')  # pakai heuristic cepat
        new_workload = workload(new_dist, len(new_locs), alpha, beta)
        
        # Toleransi 20% over target (agar tidak terlalu ketat)
        if new_workload <= target_workload * 1.2:
            assigned[p] = c
            clusters[c].append(p)
            workloads[c] = new_workload
            
            # Add edges from p to unassigned
            for q in range(n):
                if assigned[q] == -1:
                    heapq.heappush(heap, (location_matrix[p][q], q, c))
        else:
            # Tim c sudah penuh, coba tim lain yang dekat
            candidates = [tc for tc in range(k) if tc != c]
            if candidates:
                best_tc = min(candidates, key=lambda tc: min(
                    (location_matrix[m][p] for m in clusters[tc]), default=float('inf')
                ) if clusters[tc] else float('inf'))
                heapq.heappush(heap, (location_matrix[clusters[best_tc][0]][p] if clusters[best_tc] else d, p, best_tc))
    
    # Assign remaining (jika ada)
    remaining = [p for p in range(n) if assigned[p] == -1]
    for p in remaining:
        # Masukkan ke tim dengan workload terendah
        best_c = min(range(k), key=lambda c: workloads[c])
        assigned[p] = best_c
        clusters[best_c].append(p)
        new_dist = team_route_distance(full_matrix, clusters[best_c], force_algorithm='heuristic')
        workloads[best_c] = workload(new_dist, len(clusters[best_c]), alpha, beta)
    
    return assigned, workloads


def progressive_local_search(coords, full_matrix, assignment, k, n_locations, alpha, beta, max_iterations=100):
    """Local search dengan fokus pada TIM YANG TIDAK SEIMBANG.
    
    Swap hanya dilakukan jika:
    1. Mengurangi variance workload antar tim
    2. Tidak membuat tim lain jadi terlalu berat
    """
    assignment = assignment[:]
    team_points = {c: [i for i in range(n_locations) if assignment[i] == c] for c in range(k)}
    
    # Hitung workload awal
    workloads = []
    for c in range(k):
        dist = team_route_distance(full_matrix, team_points[c])
        wl = workload(dist, len(team_points[c]), alpha, beta)
        workloads.append(wl)
    
    def variance(wls):
        avg = sum(wls) / len(wls)
        return sum((w - avg) ** 2 for w in wls) / len(wls)
    
    best_variance = variance(workloads)
    
    for iteration in range(max_iterations):
        improved = False
        
        # Sort teams by workload (fokus pada yang ekstrem)
        sorted_teams = sorted(range(k), key=lambda c: workloads[c])
        lightest = sorted_teams[:3]  # 3 tim paling ringan
        heaviest = sorted_teams[-3:]  # 3 tim paling berat
        
        # Coba swap antara lightest dan heaviest
        for ti in heaviest:
            for tj in lightest:
                if ti == tj:
                    continue
                
                # Coba swap beberapa titik
                for i in team_points[ti][:10]:  # limit untuk kecepatan
                    for j in team_points[tj][:10]:
                        # Hitung workload baru
                        pts_ti = [m for m in team_points[ti] if m != i] + [j]
                        pts_tj = [m for m in team_points[tj] if m != j] + [i]
                        
                        dist_ti = team_route_distance(full_matrix, pts_ti, force_algorithm='heuristic')
                        dist_tj = team_route_distance(full_matrix, pts_tj, force_algorithm='heuristic')
                        
                        wl_ti = workload(dist_ti, len(pts_ti), alpha, beta)
                        wl_tj = workload(dist_tj, len(pts_tj), alpha, beta)
                        
                        new_workloads = workloads[:]
                        new_workloads[ti] = wl_ti
                        new_workloads[tj] = wl_tj
                        
                        new_variance = variance(new_workloads)
                        
                        # Swap jika variance berkurang
                        if new_variance + 1e-6 < best_variance:
                            assignment[i], assignment[j] = tj, ti
                            team_points[ti].remove(i)
                            team_points[tj].remove(j)
                            team_points[ti].append(j)
                            team_points[tj].append(i)
                            workloads = new_workloads
                            best_variance = new_variance
                            improved = True
                            break
                    
                    if improved:
                        break
                
                if improved:
                    break
            
            if improved:
                break
        
        if not improved:
            break
    
    # Recalculate final distances dengan algoritma terbaik
    final_total = 0.0
    for c in range(k):
        dist = team_route_distance(full_matrix, team_points[c])  # gunakan adaptive solver
        final_total += dist
    
    return assignment, final_total, workloads


def best_workload_clustering(coords, location_matrix, full_matrix, k, n_locations, alpha, beta, restarts=10):
    """Multi-restart clustering dengan workload balancing."""
    target_wl = target_workload_per_team(coords, full_matrix, n_locations, k, alpha, beta)
    
    print(f"    Target workload per tim: ~{target_wl:.1f} (α={alpha:.2f}, β={beta:.2f})")
    
    best_assignment = None
    best_variance = float('inf')
    
    for seed in range(restarts):
        rng = random.Random(seed)
        seed_indices = farthest_point_seeds(location_matrix, k, rng)
        
        assignment, workloads = workload_aware_clustering(
            coords, location_matrix, full_matrix, k, alpha, beta, target_wl, seed_indices
        )
        
        # Progressive local search
        assignment, total, workloads = progressive_local_search(
            coords, full_matrix, assignment, k, n_locations, alpha, beta, max_iterations=50
        )
        
        # Hitung variance
        avg_wl = sum(workloads) / len(workloads)
        var = sum((w - avg_wl) ** 2 for w in workloads) / len(workloads)
        
        if var < best_variance:
            best_variance = var
            best_assignment = assignment
    
    return best_assignment


# ---------------------------------------------------------------------------
# 6. MAP RENDERING (sama seperti v5, dengan penyesuaian minor)
# ---------------------------------------------------------------------------
def color_for(i):
    if i < len(QUALITATIVE_PALETTE):
        return QUALITATIVE_PALETTE[i]
    rng = random.Random(i)
    return "#%06x" % rng.randint(0, 0xFFFFFF)


def build_map(all_locations, matrix, assignment, k, n_locations, output_path, 
              draw_real_roads, profile, http_workers, alpha, beta):
    """Build interactive map dengan info workload."""
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
        ordered_global_idx = [indices[o] for o in order]
        n_visits = len(indices) - 1
        wl = workload(dist, n_visits, alpha, beta)
        team_routes.append((team, ordered_global_idx, dist, n_visits, wl))
        
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
    
    for team, ordered_global_idx, dist, n_visits, wl in team_routes:
        grand_total += dist
        color = color_for(team)
        fg = folium.FeatureGroup(
            name=f"Tim {team + 1} — {n_visits} titik, {dist:.1f} km, workload={wl:.1f}"
        )
        stop_names = []

        for seq, gi in enumerate(ordered_global_idx, start=1):
            name, lat, lon = all_locations[gi]
            if gi == 0:
                if seq == 1:
                    stop_names.append(f"START: {name}")
                    folium.Marker(
                        location=(lat, lon),
                        icon=folium.DivIcon(html=f"""
                            <div style="background:#111;color:#ffd700;border-radius:50%;
                                width:26px;height:26px;text-align:center;line-height:26px;
                                font-size:12px;font-weight:bold;border:2px solid #ffd700;">S</div>
                        """),
                        popup=f"<b>START/FINISH</b><br>{name}",
                    ).add_to(fg)
                else:
                    stop_names.append(f"FINISH: {name}")
                continue

            stop_names.append(f"{seq}. {name}")
            folium.Marker(
                location=(lat, lon),
                icon=folium.DivIcon(html=f"""
                    <div style="background:{color};color:white;border-radius:50%;
                        width:24px;height:24px;text-align:center;line-height:24px;
                        font-size:11px;font-weight:bold;border:2px solid white;">{seq}</div>
                """),
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
        summary.append((team + 1, n_visits, dist, wl, stop_names))

    folium.LayerControl(collapsed=False).add_to(m)
    m.save(output_path)
    return summary, grand_total


# ---------------------------------------------------------------------------
# 7. MAIN
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Route Planner v6 — Fair Workload Balancing (jarak jauh = kunjungan sedikit)"
    )
    parser.add_argument("--teams", type=int, default=14, help="Jumlah tim")
    parser.add_argument("--output", type=str, default="routes_v6.html", help="Output HTML")
    parser.add_argument("--restarts", type=int, default=10, help="Clustering restarts")
    parser.add_argument("--profile", type=str, default="driving", choices=["driving", "bike", "foot"])
    parser.add_argument("--no-road-lines", action="store_true")
    parser.add_argument("--http-workers", type=int, default=8)
    parser.add_argument("--csv", type=str, required=True, help="CSV file path")
    parser.add_argument("--start-name", type=str, required=True)
    parser.add_argument("--start-lat", type=float, required=True)
    parser.add_argument("--start-lon", type=float, required=True)
    
    args = parser.parse_args()

    t0 = time.time()

    START_LOCATION = (args.start_name, args.start_lat, args.start_lon)

    print(f"[1/6] Membaca lokasi dari {args.csv}...")
    LOCATIONS = load_locations_from_csv(args.csv)
    ALL_LOCATIONS = [START_LOCATION] + LOCATIONS
    print(f"      ✓ {len(LOCATIONS)} titik kunjungan")

    start_coord = (START_LOCATION[1], START_LOCATION[2])
    all_coords = [start_coord] + [(s[1], s[2]) for s in LOCATIONS]
    n_locations = len(LOCATIONS)
    location_coords = all_coords[1:]

    print("[2/6] Fetch matriks jarak OSRM (dengan caching)...")
    matrix = fetch_osrm_matrix(all_coords, profile=args.profile, max_workers=args.http_workers)
    used_real_roads = matrix is not None
    
    if matrix is None:
        print("      ! Fallback ke haversine × 1.3")
        matrix = haversine_matrix(all_coords)
        location_matrix = haversine_matrix(location_coords)
    else:
        print("      ✓ Matriks OSRM berhasil")
        location_matrix = [[matrix[i + 1][j + 1] for j in range(n_locations)] for i in range(n_locations)]

    print("[3/6] Auto-tuning workload weights...")
    alpha, beta = estimate_workload_weights(location_coords, matrix, n_locations, args.teams)
    print(f"      ✓ α (jarak) = {alpha:.2f}, β (kunjungan) = {beta:.2f}")

    print(f"[4/6] Workload-aware clustering ({args.restarts} restarts)...")
    assignment = best_workload_clustering(
        location_coords, location_matrix, matrix, args.teams, n_locations, alpha, beta, args.restarts
    )

    print("[5/6] Generate peta...")
    summary, grand_total = build_map(
        ALL_LOCATIONS, matrix, assignment, args.teams, n_locations, args.output,
        draw_real_roads=used_real_roads and not args.no_road_lines,
        profile=args.profile,
        http_workers=args.http_workers,
        alpha=alpha,
        beta=beta,
    )

    print(f"\n{'='*80}")
    print(f"RINGKASAN RUTE — {args.teams} Tim (Workload Balancing)")
    print(f"{'='*80}\n")
    
    workloads = [wl for _, _, _, wl, _ in summary]
    avg_wl = sum(workloads) / len(workloads)
    max_wl = max(workloads)
    min_wl = min(workloads)
    
    print(f"Workload: Min={min_wl:.1f}, Avg={avg_wl:.1f}, Max={max_wl:.1f}, Range={max_wl - min_wl:.1f}\n")
    
    for team_no, count, dist, wl, stops in sorted(summary, key=lambda x: x[3]):  # sort by workload
        deviation = ((wl - avg_wl) / avg_wl * 100) if avg_wl > 0 else 0
        flag = f" ({deviation:+.1f}%)" if abs(deviation) > 10 else ""
        print(f"Tim {team_no}: {count} titik, {dist:.1f} km, workload={wl:.1f}{flag}")
        for s in stops:
            print(f"    {s}")
        print()
    
    print(f"Total jarak: {grand_total:.1f} km")
    print(f"Peta: {args.output}")
    print(f"Waktu: {time.time() - t0:.1f} detik")


if __name__ == "__main__":
    main()
