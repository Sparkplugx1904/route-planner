# Kritik & Brainstorm — Aplikasi Route Planner (Web/C++ WASM)

> File ini berisi penilaian jujur atas kondisi aplikasi saat ini dan ide-ide untuk
> membuatnya berguna serta mudah dipakai oleh pengguna biasa (awam), bukan hanya
> oleh pembuatnya. Ditulis setelah inspeksi `template/` (index.html, main.js,
> osrm.js, map.js, result-panel.js, i18n.js) dan alur WASM.

---

## 1. Ringkasan kondisi saat ini

Aplikasi sudah punya fondasi teknis yang bagus:

- Seluruh perhitungan (klaster tim + urutan kunjungan) dijalankan di perangkat
  lewat WASM (C++), cepat dan privat.
- Fallback jarak haversine (garis lurus) bila OSRM gagal/offline — sudah
  terverifikasi jalan.
- Pencarian tempat (Nominatim), klik-peta, upload CSV, export CSV.
- Jaringan rute digambar di Leaflet, hasil per tim dengan badge workload.
- i18n ID/EN, dialog in-page (bukan popup browser), cache matriks di IndexedDB.

Masalah utamanya bukan teknologi — melainkan **asumsi pemakainya**. Antarmuka
saat ini dibangun seperti "tool untuk engineer yang memakai mobil armada 14"
padahal pengguna biasa mungkin hanya ingin "bagi 5 alamat toko ke 2 kurir hari ini".

---

## 2. Kritik (hal-hal yang perlu diperbaiki)

### 2.1 UX untuk pengguna awam

1. **Default `Jumlah Tim = 14` dan `Restart clustering = 10`** (index.html:83,114).
   Angka ini tidak masuk akal untuk pengguna baru, terlihat seperti *jargon
   teknis*. Pengguna biasa tidak pernah memikirkan "restart clustering".
2. **Enam mode routing tampil langsung tanpa konteks** (index.html:88-95).
   `Balanced Workload`, `Nearest Neighbor`, `Fuel Efficient`, dst. adalah istilah
   riset. Lay user tidak tahu bedanya dan takut salah pilih.
3. **Tidak ada alur tutor / wizard.** Tiga langkah inti (tambah titik → atur
   depot → hitung) ada, tapi tersebar di 2 tab; pengguna baru harus menebak
   urutan dan membaca modal Bantuan yang panjang (9 bagian).
4. **Depot wajib dipahami dulu.** Dialog "Atur titik depot dulu" sudah bagus,
   tapi konsep "depot/start" belum tentu muncul di benak pengguna yang ingin
   "bikin rute antar toko".
5. **Pesan campur bahasa.** Beberapa teks masih hardcode ID (mis. judul dialog,
   status "Memuat WASM…", error `Error: ...` apa adanya), sebagian lain
   diterjemahkan. Di perangkat berbahasa lain terasa setengah jadi.
6. **Tidak ada umpan balik lokasi.** Setelah pilih hasil pencarian, peta tidak
   otomatis pindah/tampilkan pin — pengguna bisa bingung "titik tadi masuk mana?".
7. **Hasil tidak menampilkan urutan kunjungan.** Panel hasil (result-panel.js)
   hanya menampilkan ringkasan workload per tim. Urutan rute (baris
   `tr.order[]`) dihitung tapi tidak ditampilkan — padahal itu justru yang paling
   berguna untuk pengemudi.
8. **Statistik workload tanpa penjelasan.** `Min=13.2, Avg=18.7, Max=21.6` +
   badge `+15%`/`ok` — tidak ada legenda apa artinya, satuan apa, dan apakah
   angka itu jarak/waktu. Pengguna hanya paham km.

### 2.2 Data & alur kerja

9. **Semua hilang saat halaman ditutup.** Titik, depot, hasil — tidak ada
   penyimpanan sesi. Pengguna yang mengumpulkan daftar 30 alamat akan kehilangan
   semuanya karena salah tutup tab.
10. **Tidak bisa mengedit titik.** Titik hanya bisa ditambah/dihapus; tidak ada
    drag marker, tidak ada cara memperbaiki koordinat yang salah klik.
11. **Export hanya titik, bukan rute.** Konteks pengiriman butuh "daftar
    kunjungan per kendaraan" untuk dibagikan ke driver — sekarang tidak ada.
12. **Input hanya lewat satu-per-satu atau file CSV.** Copy-paste daftar dari
    Excel/WhatsApp (baris `nama,lat,lon` atau alamat-alamat yang sudah ada)
    tidak didukung.
13. **Tidak ada tombol batal / pembatalan proses.** Setelah tekan "Hitung Rute"
    dengan 100 titik, pengguna tidak bisa menghentikan proses yang lama.
14. **Perkiraan durasi tidak ada.** Driver butuh "mulai 08.00, selesai ±11.20",
    bukan hanya km.

### 2.3 Teknis / operasional

15. **Tergantung internet untuk peta & pencarian** (Leaflet CDN, Nominatim,
    OSRM). Di daerah dengan sinyal buruk — justru target pasar semacam ini —
    aplikasi hampir tak berfungsi walau WASM sudah lokal.
16. **Belum bisa diinstall sebagai PWA / offline shell.** App shell + library
    bisa di-cache Service Worker agar buka-paksa di daerah blank spot.
17. **404 favicon** setiap load (terlihat di test headless) — kecil tapi
    menimbulkan noise konsol saat debugging.
18. **Beberapa bagian masih berekspektasi "power user"**: angka `minimal 3 huruf`
    untuk pencarian, koordinat lat/lon manual di tab Pengaturan tampil
    mencolok padahal jarang dibutuhkan lay user (bisa disembunyikan di
    "lanjutan").

---

## 3. Brainstorm — membuatnya berguna & mudah dipakai

### 3.1 Prioritas P0 (harus ada untuk dipakai orang biasa)

1. **Wizard 3 langkah ala "delivery checklist"**
   - Langkah 1 "Tambah lokasi" → Langkah 2 "Pilih titik mulai (depot)"
   - Langkah 3 "Berapa kendaraan?" (angka besar dengan tombol +/- , default 1-3)
   - Urutan di-highlight, tanda centang hijau per langkah selesai. Tombol
     "Hitung Rute" hanya menyala saat 3 langkah hijau.
2. **Tampilkan urutan rute per tim** (fitur inti yang sudah dihitung!)
   - Klik baris tim → daftar berurutan: 1️⃣ Toko A → 2️⃣ Toko B → dst., dengan
     jarak per segmen, total jarak, dan estimasi waktu (asumsi kecepatan per
     profil: mobil 30 km/j, sepeda 15 km/j, jalan kaki 5 km/j + waktu berhenti
     default 5 menit/titik).
3. **Simpan sesi otomatis (localStorage/IndexedDB)**
   - Titik + depot + pengaturan tersimpan per sesi; tombol "Sesi Baru" untuk
     reset. Nyaris nol biaya, mencegah drama kehilangan data.
4. **Export "lembar per pengemudi"**
   - Tombol per tim: salin/unduh teks singkat untuk dikirim via WhatsApp:
     `Rute Kurir 1 (5 titik, 8.6 km, ±2 jam): 1. Toko A… 2. Toko B…`.
5. **Mode sederhana vs lanjutan**
   - Default layar "Ringkas": jumlah kendaraan + titik + depot +
     "mobil/sepeda/jalan kaki" saja. Mode lanjutan (6 mode routing, restart,
     road-lines, koordinat manual) disembunyikan di akordeon "⚙️ Lanjutan".
   - Setiap field lanjutan diberi tooltip bahasa manusia, mis. "Restart
     clustering = mencoba berulang agar pembagian lebih adil; makin sering makin
     lama."

### 3.2 Prioritas P1 (kenyamanan & nilai kerja nyata)

6. **Panduan saat pertama kali (onboarding 3 langkah)**
   - Overlay berpanah ke tombol sebenarnya ("Klik di sini untuk tambah titik
     pertama"). Lebih efektif daripada modal teks 9 bagian.
7. **Peta mundur & lebar otomatis**
   - Setelah titik ditambah / hasil jadi: `map.fitBounds(...)` semua marker.
   - Pin hasil pencarian ditandai, peta berpindah ke sana.
8. **Klik baris tim → tampilkan hanya rute tim itu** (sudah ada zoom ke titik
   pertama; perbarui jadi tampilkan polyline tim saja + legenda warna).
9. **Interaksi titik yang lebih ramah**
   - Drag marker untuk menggeser posisi; klik marker → popup berisi nama + tombol
     hapus/ubah nama; urutan titik di daftar bisa diurutkan.
10. **Tombol batal + status granular**
    - "Batal" saat progress berjalan; progress dua tahap jelas: "Mengambil jarak
      jalan… (12/50)" lalu "Menghitung pembagian… 40%".
11. **Paste batch**
    - Textarea "tempel daftar (nama, lat, lon)" — terima juga paste dari
      spreadsheet; otomatis deteksi format header.
12. **Rekomendasi jumlah tim**
    - Saran otomatis: `ceil(titik / 8)` ("untuk 17 titik, 3 kendaraan cukup")
      tapi tetap bisa diubah pengguna.
13. **Share-able link** sesi: encode titik+depot ke URL hash sehingga "rute
    hari ini" bisa dikirim ke rekan tanpa paste ulang.
14. **Status offline yang humanis**
    - Chip "Luring — jarak diperkirakan garis lurus" dengan penjelasan singkat,
    - dan Saran "aktifkan internet untuk jarak jalan asli", bukan sekadar
      `console.warn`.

### 3.3 Prioritas P2 (potensi besar, tapi nanti)

15. **PWA offline-first**: cache shell + Leaflet + JS (tanpa CDN) + data OSRM
    lokal untuk area terbatas (mis. Bali) di IndexedDB; bisa dipakai penuh
    saat pesawat/modus pesawat.
16. **Checklist pengiriman di aplikasi**: driver mencentang titik yang sudah
    dikunjungi saat itu juga (mode "hari-H"), progress live "5/12 dikunjungi".
17. **Sesi bernama / armada favorit**: simpan daftar "Rute Pasar Kamis" dan
    buka kembali minggu depan.
18. **Data contoh sekali sentuh**: tombol "Coba contoh data" (5 titik + depot)
    agar pengguna langsung melihat hasil 10 detik, tanpa mengisi apa pun.
19. **Estimasi dibuat dari data riwayat**: kecepatan rata-rata dihitung dari
    sesi-sesi sebelumnya (klaster per wilayah).
20. **Ekspor GeoJSON/KML** agar bisa dibuka di aplikasi navigasi lain.
21. **Umpan balik hasil**: bintang "apakah pembagian ini adil?" untuk menyetel
    default/parameter ke depan.
22. **Mode berbagi**: satu klik → pilihan "Kirim PDF/WA/print" lembar per
    kendaraan lengkap dengan rute berurutan.

---

## 4. Rekomendasi eksekusi (urutan kerja)

| # | Langkah | Effort | Dampak |
|---|---------|--------|--------|
| 1 | Default jumlah kendaraan 1-3 + toolbar +/- | Kecil | Tinggi |
| 2 | Simpan sesi otomatis (titik, depot, pengaturan) | Kecil | Tinggi |
| 3 | Panel hasil menampilkan urutan kunjungan + km per segmen + estimasi waktu | Sedang | Tinggi |
| 4 | Mode ringkas/lanjutan + tooltip istilah | Kecil | Tinggi |
| 5 | Onboarding 3 langkah berpanah + tombol contoh data | Sedang | Tinggi |
| 6 | Export tegks per pengemudi (copy/WA/unduh) | Kecil | Tinggi |
| 7 | Map fitBounds + pin hasil cari + drag marker | Sedang | Sedang |
| 8 | Tombol batal + progress granular | Kecil | Sedang |
| 9 | Paste batch | Kecil | Sedang |
| 10 | PWA offline + bundel lokal (tanpa CDN) | Besar | Sedang |

Catatan: langkah 1-6 saja sudah cukup mengubah aplikasi dari "demo teknis"
menjadi "alat kerja harian" untuk warung, kurir, atau koperasi kecil.

---

## 5. Prinsip panduan (ringkas untuk pengembangan ke depan)

1. **Satu alur utama untuk semua orang**: Titik → Mulai → Kendaraan → Hitung.
   Semua hal lain di bawah akordeon "Lanjutan".
2. **Bahasa manusia, bukan bahasa teknis**. Jika sebuah istilah butuh tooltip,
   ganti istilahnya.
3. **Jangan pernah menghilangkan data tanpa konfirmasi**; simpan otomatis.
4. **Setiap nomor di layar harus bisa dijelaskan dalam satu kalimat oleh
   non-ahli** (km, jam, jumlah titik — bukan "workload index").
5. **Offline = bukan error, tapi mode yang jelas & jujur.**
6. **Hasil yang bisa dibawa pulang**: apa pun yang dihitung harus bisa
   disalin/diunduh/dibagikan dalam format yang dibaca manusia.